#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace ger::ios {

struct InstallResult {
    bool success = false;
    std::string message;
    std::filesystem::path install_root;
    std::uint64_t bytes_installed = 0;
};

using InstallProgress = std::function<void(double fraction, const std::string &message)>;

class NativeInstaller final {
public:
    static std::filesystem::path documents_root();
    static std::filesystem::path game_root();
    static bool ensure_storage(std::string &error);

    static InstallResult install_folder(const std::filesystem::path &selected_path,
        const std::filesystem::path &game_source,
        const InstallProgress &progress);

    static InstallResult install_archive(const std::filesystem::path &archive_path,
        const InstallProgress &progress);
};

} // namespace ger::ios
