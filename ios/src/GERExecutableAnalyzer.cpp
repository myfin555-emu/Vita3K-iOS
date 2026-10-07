#include <vita3k_ios/GERExecutableAnalyzer.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace ger::ios {
namespace {

template <typename T>
bool read_at(const std::vector<std::uint8_t> &data, std::size_t offset, T &value) {
    if (offset > data.size() || sizeof(T) > data.size() - offset)
        return false;
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return true;
}

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
    static constexpr std::array<const char *, 20> needles = {
        "sceKernel", "sceIo", "sceGxm", "sceAudio", "sceCtrl",
        "sceTouch", "sceDisplay", "sceAppMgr", "sceCommonDialog",
        "sceSysmodule", "sceNet", "sceHttp",
        "sceFios", "scePvf", "sceMotion", "sceRtc",
        "sceLibc", "sceClib", "sceKernelAllocMemBlock"
    };

    for (const char *needle : needles) {
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
                        const auto phnum = u16le(e + 0x2C);
                        const auto shnum = u16le(e + 0x30);
                        report.entry_point = hex_u64(entry, 8);
                        report.program_headers = std::to_string(phnum) + " entries @ " + hex_u64(phoff, 8);
                        report.sections = std::to_string(shnum) + " entries @ " + hex_u64(shoff, 8);
                    }
                } else if (elf_data == 2) {
                    report.endianness = "Big-endian";
                }
            } else if (elf_class == 2) {
                report.architecture = "ELF64";
                if (elf_data == 1) {
                    report.endianness = "Little-endian";
                    if (data.size() - elf_offset >= 0x40) {
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

    return report;
}

} // namespace ger::ios
