#include <vita3k_ios/GERExecutableAnalyzer.h>
#include <vita3k_ios/GERNativeLogger.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace ger::ios {
namespace {

std::uint16_t u16le(const std::uint8_t *p) {
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t u32le(const std::uint8_t *p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t u64le(const std::uint8_t *p) {
    return static_cast<std::uint64_t>(u32le(p)) |
           (static_cast<std::uint64_t>(u32le(p + 4)) << 32);
}

std::string machine_name(std::uint16_t machine) {
    switch (machine) {
    case 3: return "Intel 80386";
    case 8: return "MIPS";
    case 20: return "PowerPC";
    case 40: return "ARM";
    case 62: return "x86-64";
    case 183: return "AArch64";
    default: return "Unknown (EM_" + std::to_string(machine) + ")";
    }
}

std::string program_type_name(std::uint32_t type) {
    switch (type) {
    case 0: return "NULL";
    case 1: return "LOAD";
    case 2: return "DYNAMIC";
    case 3: return "INTERP";
    case 4: return "NOTE";
    case 5: return "SHLIB";
    case 6: return "PHDR";
    case 7: return "TLS";
    default: return "TYPE_" + std::to_string(type);
    }
}

std::string section_type_name(std::uint32_t type) {
    switch (type) {
    case 0: return "NULL";
    case 1: return "PROGBITS";
    case 2: return "SYMTAB";
    case 3: return "STRTAB";
    case 4: return "RELA";
    case 5: return "HASH";
    case 6: return "DYNAMIC";
    case 7: return "NOTE";
    case 8: return "NOBITS";
    case 9: return "REL";
    case 11: return "DYNSYM";
    default: return "TYPE_" + std::to_string(type);
    }
}

std::string hex_u64(std::uint64_t value, unsigned width = 0) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setfill('0');
    if (width)
        out << std::setw(width);
    out << value;
    return out.str();
}

bool starts_with(const std::vector<std::uint8_t> &data, std::initializer_list<std::uint8_t> magic) {
    if (data.size() < magic.size())
        return false;
    std::size_t i = 0;
    for (const auto byte : magic) {
        if (data[i++] != byte)
            return false;
    }
    return true;
}

void collect_elf_details(const std::vector<std::uint8_t> &data, std::size_t elf_offset, ExecutableReport &report) {
    const auto available = [&]() -> std::size_t {
        return elf_offset <= data.size() ? data.size() - elf_offset : 0;
    };
    if (elf_offset == std::string::npos || available() < 0x34) {
        NativeLogger::write("analyzer: deep ELF skipped: truncated ELF header");
        return;
    }

    const auto *e = data.data() + elf_offset;
    if (e[4] != 1 || e[5] != 1) {
        NativeLogger::write("analyzer: deep ELF skipped: unsupported class/data");
        return;
    }

    const std::uint32_t phoff = u32le(e + 0x1C);
    const std::uint32_t shoff = u32le(e + 0x20);
    const std::uint16_t phentsize = u16le(e + 0x2A);
    const std::uint16_t phnum = u16le(e + 0x2C);
    const std::uint16_t shentsize = u16le(e + 0x2E);
    const std::uint16_t shnum = u16le(e + 0x30);
    const std::uint16_t shstrndx = u16le(e + 0x32);

    NativeLogger::write("analyzer: ELF header phoff=" + hex_u64(phoff, 8) +
                        " phentsize=" + std::to_string(phentsize) +
                        " phnum=" + std::to_string(phnum) +
                        " shoff=" + hex_u64(shoff, 8) +
                        " shentsize=" + std::to_string(shentsize) +
                        " shnum=" + std::to_string(shnum) +
                        " shstrndx=" + std::to_string(shstrndx));

    // Program headers are authoritative for loading even when a release ELF has
    // no section table. Parse them independently from sections.
    if (phnum != 0 && phentsize >= 0x20) {
        const std::uint64_t ph_end = static_cast<std::uint64_t>(phoff) +
            static_cast<std::uint64_t>(phnum) * phentsize;
        if (phoff < available() && ph_end <= available()) {
            std::ostringstream ph;
            std::size_t valid = 0;
            for (std::uint16_t i = 0; i < phnum; ++i) {
                const auto *p = e + static_cast<std::uint64_t>(phoff) +
                    static_cast<std::uint64_t>(i) * phentsize;
                const auto type = u32le(p + 0x00);
                const auto file_offset = u32le(p + 0x04);
                const auto vaddr = u32le(p + 0x08);
                const auto paddr = u32le(p + 0x0C);
                const auto filesz = u32le(p + 0x10);
                const auto memsz = u32le(p + 0x14);
                const auto pflags = u32le(p + 0x18);
                const auto align = u32le(p + 0x1C);
                ph << "#" << i << " " << program_type_name(type)
                   << " file=" << hex_u64(file_offset, 8)
                   << " vaddr=" << hex_u64(vaddr, 8)
                   << " paddr=" << hex_u64(paddr, 8)
                   << " fileSize=" << hex_u64(filesz, 8)
                   << " memSize=" << hex_u64(memsz, 8)
                   << " flags=" << hex_u64(pflags, 2)
                   << " align=" << hex_u64(align, 8) << "\n";
                ++valid;
            }
            report.program_header_details = ph.str();
            NativeLogger::write("analyzer: program headers parsed=" + std::to_string(valid));
        } else {
            NativeLogger::write("analyzer: program headers invalid/truncated");
        }
    } else {
        NativeLogger::write("analyzer: no usable program header table");
    }

    if (shoff == 0 || shnum == 0) {
        NativeLogger::write("analyzer: no section table (shoff/shnum empty); continuing with program headers");
        return;
    }
    if (shentsize < 0x28) {
        NativeLogger::write("analyzer: section table rejected: shentsize=" + std::to_string(shentsize));
        return;
    }

    const std::uint64_t sh_end = static_cast<std::uint64_t>(shoff) +
        static_cast<std::uint64_t>(shnum) * shentsize;
    if (shoff >= available() || sh_end > available()) {
        NativeLogger::write("analyzer: section table rejected: outside embedded ELF");
        return;
    }
    if (shstrndx >= shnum) {
        NativeLogger::write("analyzer: section name table index invalid: " + std::to_string(shstrndx));
        return;
    }

    struct Section {
        std::uint32_t name = 0, type = 0, flags = 0, addr = 0, offset = 0, size = 0;
        std::uint32_t link = 0, info = 0, align = 0, entsize = 0;
        std::string name_text;
    };
    std::vector<Section> sections;
    sections.reserve(shnum);

    const auto section_at = [&](std::uint16_t i) -> const std::uint8_t * {
        return e + static_cast<std::uint64_t>(shoff) + static_cast<std::uint64_t>(i) * shentsize;
    };

    const auto *shstr = section_at(shstrndx);
    const std::uint32_t shstr_offset = u32le(shstr + 0x10);
    const std::uint32_t shstr_size = u32le(shstr + 0x14);
    const char *shstr_data = nullptr;
    if (static_cast<std::uint64_t>(shstr_offset) + shstr_size <= available())
        shstr_data = reinterpret_cast<const char *>(e + shstr_offset);

    for (std::uint16_t i = 0; i < shnum; ++i) {
        const auto *s = section_at(i);
        Section x;
        x.name = u32le(s + 0x00);
        x.type = u32le(s + 0x04);
        x.flags = u32le(s + 0x08);
        x.addr = u32le(s + 0x0C);
        x.offset = u32le(s + 0x10);
        x.size = u32le(s + 0x14);
        x.link = u32le(s + 0x18);
        x.info = u32le(s + 0x1C);
        x.align = u32le(s + 0x20);
        x.entsize = u32le(s + 0x24);
        if (shstr_data && x.name < shstr_size) {
            const char *p = shstr_data + x.name;
            std::size_t n = 0;
            while (x.name + n < shstr_size && p[n] != '\0')
                ++n;
            x.name_text.assign(p, n);
        }
        sections.emplace_back(std::move(x));
    }

    std::ostringstream sh;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const auto &s = sections[i];
        sh << "#" << i << " " << (s.name_text.empty() ? "<unnamed>" : s.name_text)
           << " " << section_type_name(s.type)
           << " addr=" << hex_u64(s.addr, 8)
           << " file=" << hex_u64(s.offset, 8)
           << " size=" << hex_u64(s.size, 8)
           << " flags=" << hex_u64(s.flags, 8)
           << " link=" << s.link << " info=" << s.info
           << " entsize=" << s.entsize << "\n";
    }
    report.section_details = sh.str();

    auto safe_string = [&](const Section &strtab, std::uint32_t off) -> std::string {
        if (strtab.type != 3 || static_cast<std::uint64_t>(strtab.offset) + strtab.size > available() || off >= strtab.size)
            return {};
        const char *base = reinterpret_cast<const char *>(e + strtab.offset + off);
        std::size_t n = 0;
        while (off + n < strtab.size && base[n] != '\0')
            ++n;
        return std::string(base, n);
    };

    std::ostringstream dyn;
    std::size_t dynamic_count = 0;
    for (const auto &s : sections) {
        if (s.type != 6 || s.entsize < 8 || s.size == 0 ||
            static_cast<std::uint64_t>(s.offset) + s.size > available())
            continue;
        const Section *strtab = s.link < sections.size() ? &sections[s.link] : nullptr;
        dyn << "Section " << (s.name_text.empty() ? "<dynamic>" : s.name_text) << "\n";
        const std::uint32_t count = s.size / s.entsize;
        for (std::uint32_t n = 0; n < count; ++n) {
            const auto *d = e + s.offset + static_cast<std::uint64_t>(n) * s.entsize;
            const std::int32_t tag = static_cast<std::int32_t>(u32le(d));
            const std::uint32_t value = u32le(d + 4);
            dyn << "  tag=" << tag << " value=" << hex_u64(value, 8);
            if (tag == 1 && strtab)
                dyn << " name=" << safe_string(*strtab, value);
            dyn << "\n";
            ++dynamic_count;
        }
    }
    report.dynamic_info = dyn.str();

    std::ostringstream symbols;
    std::size_t symbol_count = 0;
    for (const auto &s : sections) {
        if ((s.type != 2 && s.type != 11) || s.entsize < 16 || s.size == 0 ||
            static_cast<std::uint64_t>(s.offset) + s.size > available())
            continue;
        const Section *strtab = s.link < sections.size() ? &sections[s.link] : nullptr;
        const std::uint32_t count = s.size / s.entsize;
        symbols << "Section " << (s.name_text.empty() ? "<symbols>" : s.name_text)
                << " count=" << count << "\n";
        for (std::uint32_t n = 0; n < count && symbol_count < 2048; ++n) {
            const auto *sym = e + s.offset + static_cast<std::uint64_t>(n) * s.entsize;
            const std::uint32_t name = u32le(sym);
            const std::uint32_t value = u32le(sym + 4);
            const std::uint32_t size = u32le(sym + 8);
            const std::uint8_t info = sym[12];
            const std::uint16_t shndx = u16le(sym + 14);
            symbols << "  [" << n << "] " << (strtab ? safe_string(*strtab, name) : std::string{})
                    << " value=" << hex_u64(value, 8)
                    << " size=" << size
                    << " bind=" << static_cast<unsigned>((info >> 4) & 0xF)
                    << " type=" << static_cast<unsigned>(info & 0xF)
                    << " shndx=" << shndx << "\n";
            ++symbol_count;
        }
    }
    if (symbol_count >= 2048)
        symbols << "  ... symbol output capped at 2048 entries ...\n";
    report.symbol_details = symbols.str();

    std::ostringstream relocs;
    std::size_t relocation_count = 0;
    for (const auto &s : sections) {
        if ((s.type != 9 && s.type != 4) || s.entsize < 8 || s.size == 0 ||
            static_cast<std::uint64_t>(s.offset) + s.size > available())
            continue;
        const std::uint32_t count = s.size / s.entsize;
        relocs << "Section " << (s.name_text.empty() ? "<relocation>" : s.name_text)
               << " type=" << section_type_name(s.type) << " count=" << count << "\n";
        for (std::uint32_t n = 0; n < count && relocation_count < 4096; ++n) {
            const auto *r = e + s.offset + static_cast<std::uint64_t>(n) * s.entsize;
            const std::uint32_t offset = u32le(r);
            const std::uint32_t info = u32le(r + 4);
            relocs << "  [" << n << "] offset=" << hex_u64(offset, 8)
                   << " sym=" << (info >> 8)
                   << " type=" << (info & 0xFF);
            if (s.type == 4 && s.entsize >= 12)
                relocs << " addend=" << static_cast<std::int32_t>(u32le(r + 8));
            relocs << "\n";
            ++relocation_count;
        }
    }
    if (relocation_count >= 4096)
        relocs << "  ... relocation output capped at 4096 entries ...\n";
    report.relocation_details = relocs.str();

    std::ostringstream notes;
    for (const auto &s : sections) {
        if (s.type != 7 || s.size == 0 ||
            static_cast<std::uint64_t>(s.offset) + s.size > available())
            continue;
        notes << "Section " << (s.name_text.empty() ? "<note>" : s.name_text)
              << " size=" << s.size << " bytes\n";
        const auto *p = e + s.offset;
        std::uint32_t cursor = 0;
        while (cursor + 12 <= s.size) {
            const std::uint32_t namesz = u32le(p + cursor);
            const std::uint32_t descsz = u32le(p + cursor + 4);
            const std::uint32_t type = u32le(p + cursor + 8);
            notes << "  namesz=" << namesz << " descsz=" << descsz << " type=" << type << "\n";
            const std::uint32_t name_end = cursor + 12 + ((namesz + 3) & ~3U);
            const std::uint32_t desc_end = name_end + ((descsz + 3) & ~3U);
            if (name_end > s.size || desc_end > s.size || desc_end <= cursor)
                break;
            cursor = desc_end;
        }
    }
    report.note_details = notes.str();

    NativeLogger::write("analyzer: deep ELF details sections=" + std::to_string(sections.size()) +
                        " dynamic=" + std::to_string(dynamic_count) +
                        " symbols=" + std::to_string(symbol_count) +
                        " relocations=" + std::to_string(relocation_count));
}

void collect_api_strings(const std::vector<std::uint8_t> &data, std::vector<std::string> &out) {
    static constexpr std::array<const char *, 19> needles = {
        "sceKernel", "sceIo", "sceGxm", "sceAudio", "sceCtrl",
        "sceTouch", "sceDisplay", "sceAppMgr", "sceCommonDialog",
        "sceSysmodule", "sceNet", "sceHttp",
        "sceFios", "scePvf", "sceMotion", "sceRtc",
        "sceLibc", "sceClib", "sceKernelAllocMemBlock"
    };

    NativeLogger::write("analyzer: collecting known Vita API strings");
    for (const char *needle : needles) {
        if (needle == nullptr)
            continue;
        const std::size_t length = std::char_traits<char>::length(needle);
        if (std::search(data.begin(), data.end(), needle, needle + length) != data.end())
            out.emplace_back(needle);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

} // namespace

ExecutableReport analyze_executable(const std::filesystem::path &path) {
    ExecutableReport report;
    NativeLogger::write("analyzer: start " + path.string());

    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        report.message = "Unable to open eboot.bin.";
        return report;
    }

    const auto end = stream.tellg();
    if (end < 0) {
        report.message = "Unable to determine eboot.bin size.";
        return report;
    }

    const auto size = static_cast<std::uint64_t>(end);
    report.file_size = size;
    NativeLogger::write("analyzer: eboot size=" + std::to_string(size));
    report.readable = true;

    // Read the complete executable because GE:R's eboot is small enough for the
    // analyzer's diagnostic pass. No bytes are written or modified.
    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        report.message = "Executable is too large for the native analyzer.";
        report.readable = false;
        return report;
    }

    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty())
        stream.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!stream && !data.empty()) {
        report.message = "Unable to read the complete eboot.bin.";
        report.readable = false;
        return report;
    }

    report.format = "Unknown binary";
    report.architecture = "Unknown";
    report.endianness = "Unknown";

    const bool is_self = starts_with(data, {0x53, 0x43, 0x45, 0x00});
    const bool is_elf = starts_with(data, {0x7F, 0x45, 0x4C, 0x46});
    NativeLogger::write(std::string("analyzer: outer format self=") + (is_self ? "yes" : "no") + " elf=" + (is_elf ? "yes" : "no"));

    if (is_self) {
        report.format = "SCE SELF";
        report.message = "Vita SCE SELF container detected.";
    } else if (is_elf) {
        report.format = "ELF";
        report.message = "ELF executable detected.";
    } else {
        report.message = "Executable loaded, but its outer format is not recognized by this analyzer.";
    }

    std::size_t elf_offset = is_elf ? 0 : std::string::npos;
    if (!is_elf) {
        static constexpr std::array<std::uint8_t, 4> elf_magic = {0x7F, 0x45, 0x4C, 0x46};
        const auto begin = data.begin() + std::min<std::size_t>(data.size(), 4);
        const auto it = std::search(begin, data.end(), elf_magic.begin(), elf_magic.end());
        if (it != data.end())
            elf_offset = static_cast<std::size_t>(std::distance(data.begin(), it));
    }

    if (elf_offset != std::string::npos) {
        const auto *e = data.data() + elf_offset;
        if (data.size() - elf_offset >= 0x34) {
            const std::uint8_t elf_class = e[4];
            const std::uint8_t elf_data = e[5];
            if (elf_class == 1) {
                report.architecture = "ELF32";
                if (elf_data == 1) {
                    report.endianness = "Little-endian";
                    if (data.size() - elf_offset >= 0x34) {
                        const auto entry = u32le(e + 0x18);
                        const auto phoff = u32le(e + 0x1C);
                        const auto shoff = u32le(e + 0x20);
                        const auto flags = u32le(e + 0x24);
                        const auto phentsize = u16le(e + 0x2A);
                        const auto phnum = u16le(e + 0x2C);
                        const auto shentsize = u16le(e + 0x2E);
                        const auto shnum = u16le(e + 0x30);
                        const auto shstrndx = u16le(e + 0x32);
                        const auto machine = u16le(e + 0x12);
                        report.machine = machine_name(machine);
                        report.elf_flags = hex_u64(flags, 8);
                        report.entry_point = hex_u64(entry, 8);
                        report.program_headers = std::to_string(phnum) + " entries @ " + hex_u64(phoff, 8) +
                            " (entry size " + std::to_string(phentsize) + ")";
                        report.sections = std::to_string(shnum) + " entries @ " + hex_u64(shoff, 8) +
                            " (entry size " + std::to_string(shentsize) + ")";

                        std::ostringstream ph;
                        for (std::uint16_t i = 0; i < phnum; ++i) {
                            const std::uint64_t offset = static_cast<std::uint64_t>(phoff) +
                                static_cast<std::uint64_t>(i) * phentsize;
                            if (phentsize < 0x20 || offset + 0x20 > data.size() - elf_offset)
                                break;
                            const auto *p = e + offset;
                            const auto type = u32le(p + 0x00);
                            const auto file_offset = u32le(p + 0x04);
                            const auto vaddr = u32le(p + 0x08);
                            const auto filesz = u32le(p + 0x10);
                            const auto memsz = u32le(p + 0x14);
                            const auto pflags = u32le(p + 0x18);
                            ph << "#" << i << " " << program_type_name(type)
                               << " file=" << hex_u64(file_offset, 8)
                               << " vaddr=" << hex_u64(vaddr, 8)
                               << " fileSize=" << hex_u64(filesz, 8)
                               << " memSize=" << hex_u64(memsz, 8)
                               << " flags=" << hex_u64(pflags, 2) << "\n";
                        }
                        report.program_header_details = ph.str();

                        if (shoff != 0 && shentsize >= 0x28 &&
                            static_cast<std::uint64_t>(shoff) +
                            static_cast<std::uint64_t>(shnum) * shentsize <= data.size() - elf_offset &&
                            shstrndx < shnum) {
                            const auto *strhdr = e + static_cast<std::uint64_t>(shoff) + static_cast<std::uint64_t>(shstrndx) * shentsize;
                            const auto str_offset = u32le(strhdr + 0x10);
                            const auto str_size = u32le(strhdr + 0x14);
                            const char *strtab = nullptr;
                            if (static_cast<std::uint64_t>(str_offset) + str_size <= data.size() - elf_offset)
                                strtab = reinterpret_cast<const char *>(e + str_offset);

                            std::ostringstream sh;
                            for (std::uint16_t i = 0; i < shnum; ++i) {
                                const auto *s = e + static_cast<std::uint64_t>(shoff) + static_cast<std::uint64_t>(i) * shentsize;
                                const auto name_offset = u32le(s + 0x00);
                                const auto type = u32le(s + 0x04);
                                const auto addr = u32le(s + 0x0C);
                                const auto file_offset = u32le(s + 0x10);
                                const auto section_size = u32le(s + 0x14);
                                std::string name;
                                if (strtab && name_offset < str_size) {
                                    const char *begin = strtab + name_offset;
                                    const std::size_t max = str_size - name_offset;
                                    std::size_t length = 0;
                                    while (length < max && begin[length] != '\\0')
                                        ++length;
                                    name.assign(begin, length);
                                }
                                sh << "#" << i << " " << (name.empty() ? "<unnamed>" : name)
                                   << " " << section_type_name(type)
                                   << " addr=" << hex_u64(addr, 8)
                                   << " file=" << hex_u64(file_offset, 8)
                                   << " size=" << hex_u64(section_size, 8) << "\n";
                            }
                            report.section_details = sh.str();
                        }
                    }
                } else if (elf_data == 2) {
                    report.endianness = "Big-endian";
                    report.machine = machine_name(u16le(e + 0x12));
                }
            } else if (elf_class == 2) {
                report.architecture = "ELF64";
                if (elf_data == 1) {
                    report.endianness = "Little-endian";
                    if (data.size() - elf_offset >= 0x40) {
                        const auto machine = u16le(e + 0x12);
                        report.machine = machine_name(machine);
                        const auto entry = u64le(e + 0x18);
                        const auto phoff = u64le(e + 0x20);
                        const auto shoff = u64le(e + 0x28);
                        const auto phnum = u16le(e + 0x38);
                        const auto shnum = u16le(e + 0x3C);
                        report.entry_point = hex_u64(entry, 16);
                        report.program_headers = std::to_string(phnum) + " entries @ " + hex_u64(phoff, 16);
                        report.sections = std::to_string(shnum) + " entries @ " + hex_u64(shoff, 16);
                    }
                } else if (elf_data == 2) {
                    report.endianness = "Big-endian";
                }
            }
            report.embedded_elf = elf_offset == 0
                ? "ELF header at file offset 0"
                : "ELF header found at file offset " + hex_u64(elf_offset, 8);
        }
    }

    if (elf_offset != std::string::npos) {
        if (is_self) {
            report.self_info = "SELF container bytes before embedded ELF: " + std::to_string(elf_offset) +
                "; embedded ELF offset: " + hex_u64(elf_offset, 8) +
                "; embedded ELF bytes: " + std::to_string(data.size() - elf_offset);
            NativeLogger::write("analyzer: SELF envelope bytes=" + std::to_string(elf_offset) +
                                " embedded ELF offset=" + std::to_string(elf_offset));
        }
        collect_elf_details(data, elf_offset, report);
    }

    collect_api_strings(data, report.api_strings);
    NativeLogger::write("analyzer: API string scan complete, matches=" + std::to_string(report.api_strings.size()));
    NativeLogger::write("analyzer: ELF machine=" + report.machine + " flags=" + report.elf_flags);
    NativeLogger::write("analyzer: complete format=" + report.format + " arch=" + report.architecture + " machine=" + report.machine);
    return report;
}

} // namespace ger::ios
