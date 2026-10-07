#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ger::ios {

struct ExecutableReport {
    bool readable = false;
    std::uint64_t file_size = 0;
    std::string format;
    std::string architecture;
    std::string endianness;
    std::string entry_point;
    std::string program_headers;
    std::string sections;
    std::string embedded_elf;
    std::vector<std::string> api_strings;
    std::string message;
};

ExecutableReport analyze_executable(const std::filesystem::path &path);

} // namespace ger::ios
