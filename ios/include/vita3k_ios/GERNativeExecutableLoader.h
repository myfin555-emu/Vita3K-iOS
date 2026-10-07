#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ger::ios {

struct NativeExecutableSegment {
    std::uint32_t type = 0;
    std::uint32_t flags = 0;
    std::uint32_t vaddr = 0;
    std::uint32_t filesz = 0;
    std::uint32_t memsz = 0;
    std::uint64_t self_offset = 0;
    std::uint64_t self_size = 0;
    std::uint32_t compression = 0;
    std::uint32_t encryption = 0;
};

struct NativeRelocation {
    bool is_short = false;
    std::uint8_t code = 0;
    std::uint8_t sym_segment = 0;
    std::uint8_t data_segment = 0;
    std::uint32_t offset = 0;
    std::int32_t addend = 0;
    std::uint8_t second_code = 0;
    std::uint8_t second_distance = 0;
};

struct NativeExecutableImage {
    std::uint32_t module_base = 0;
    std::uint32_t entry = 0;
    std::uint32_t entry_rva = 0;
    std::size_t memory_size = 0;
    std::vector<NativeExecutableSegment> segments;
    std::vector<NativeRelocation> relocations;
    std::vector<std::uint8_t> memory;
    std::size_t relocations_applied = 0;
    std::size_t relocations_unsupported = 0;
};

class NativeExecutableLoader final {
public:
    bool load(const std::filesystem::path &self_path, NativeExecutableImage &image, std::string &error) const;
};

} // namespace ger::ios
