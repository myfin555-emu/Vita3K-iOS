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
                                    const std::size_t length = std::strnlen(begin, max);
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

    collect_api_strings(data, report.api_strings);
    NativeLogger::write("analyzer: API string scan complete, matches=" + std::to_string(report.api_strings.size()));
    NativeLogger::write("analyzer: complete format=" + report.format + " arch=" + report.architecture);
    return report;
}

} // namespace ger::ios
