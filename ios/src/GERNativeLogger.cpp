#include <vita3k_ios/GERNativeLogger.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace ger::ios {
namespace {
std::mutex g_log_mutex;

std::string documents_path() {
    const char *home = std::getenv("HOME");
    if (home == nullptr || *home == '\0')
        return {};
    return (std::filesystem::path(home) / "Documents").string();
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&tt, &tm);

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}
} // namespace

std::string NativeLogger::log_directory() {
    const auto documents = documents_path();
    if (documents.empty())
        return {};
    return (std::filesystem::path(documents) / "log").string();
}

std::string NativeLogger::log_file() {
    const auto directory = log_directory();
    if (directory.empty())
        return {};
    return (std::filesystem::path(directory) / "ger-native.log").string();
}

bool NativeLogger::ensure_storage() {
    try {
        const auto directory = log_directory();
        if (directory.empty())
            return false;
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        return !ec;
    } catch (...) {
        return false;
    }
}

void NativeLogger::write(const std::string &message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    try {
        if (!ensure_storage())
            return;

        std::ofstream stream(log_file(), std::ios::app);
        if (!stream)
            return;

        stream << "[" << timestamp() << "] " << message << "\n";
        stream.flush();
    } catch (...) {
        // Logging must never take the application down.
    }
}

} // namespace ger::ios
