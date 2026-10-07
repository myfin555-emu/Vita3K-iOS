#import <Foundation/Foundation.h>

#include <vita3k_ios/GERNativeInstaller.h>

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <system_error>

namespace ger::ios {
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kTitleId = "PCSE00801";
constexpr std::uint64_t kSafetyMargin = 128ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumInstallSize = 32ULL * 1024ULL * 1024ULL * 1024ULL;

bool is_directory(const fs::path &path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

bool is_file(const fs::path &path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

bool valid_game_root(const fs::path &root) {
    return is_directory(root / "sce_sys")
        && is_file(root / "sce_sys/param.sfo")
        && (is_file(root / "eboot.bin")
            || is_file(root / "EBOOT.BIN")
            || is_file(root / "eboot.bin.self"));
}

fs::path locate_game_root(const fs::path &selected) {
    if (valid_game_root(selected))
        return selected;

    const std::array<fs::path, 6> candidates = {
        selected / "PCSE00801",
        selected / "app/PCSE00801",
        selected / "ux0/app/PCSE00801",
        selected / "vita/app/PCSE00801",
        selected / "vita/ux0/app/PCSE00801",
        selected / "vita/ux0/app/PCSE00801/"
    };

    for (const auto &candidate : candidates) {
        if (valid_game_root(candidate))
            return candidate;
    }

    std::error_code ec;
    if (!is_directory(selected))
        return {};

    fs::recursive_directory_iterator it(
        selected,
        fs::directory_options::skip_permission_denied,
        ec);
    const fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        if (it->is_directory(ec) && it->path().filename() == "PCSE00801" && valid_game_root(it->path()))
            return it->path();
        it.increment(ec);
    }
    return {};
}

bool safe_archive_path(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\'
        || path.find('\\') != std::string_view::npos
        || path.find(':') != std::string_view::npos)
        return false;

    std::size_t offset = 0;
    while (offset < path.size()) {
        const auto separator = path.find('/', offset);
        const auto end = separator == std::string_view::npos ? path.size() : separator;
        const auto component = path.substr(offset, end - offset);
        if (component.empty() || component == "." || component == "..")
            return false;
        if (separator == std::string_view::npos)
            break;
        offset = separator + 1;
    }
    return true;
}

std::string archive_name(mz_zip_archive &zip, mz_uint index) {
    const auto size = mz_zip_reader_get_filename(&zip, index, nullptr, 0);
    if (!size || size > 8192)
        return {};
    std::string result(size, '\0');
    if (!mz_zip_reader_get_filename(&zip, index, result.data(), size))
        return {};
    result.resize(size > 0 ? size - 1 : 0);
    return result;
}

std::string find_archive_root(mz_zip_archive &zip) {
    const auto count = mz_zip_reader_get_num_files(&zip);
    const std::string suffix = "PCSE00801/sce_sys/param.sfo";

    for (mz_uint i = 0; i < count; ++i) {
        auto name = archive_name(zip, i);
        if (name.size() >= suffix.size()
            && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return name.substr(0, name.size() - std::string("sce_sys/param.sfo").size());
        }
    }
    return {};
}

bool archive_has_eboot(mz_zip_archive &zip, const std::string &root) {
    const auto count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        const auto name = archive_name(zip, i);
        if (name == root + "eboot.bin" || name == root + "EBOOT.BIN" || name == root + "eboot.bin.self")
            return true;
    }
    return false;
}

std::uint64_t archive_size(mz_zip_archive &zip, const std::string &root) {
    std::uint64_t total = 0;
    const auto count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        const auto name = archive_name(zip, i);
        if (!name.starts_with(root) || mz_zip_reader_is_file_a_directory(&zip, i))
            continue;
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            return 0;
        if (stat.m_uncomp_size > std::numeric_limits<std::uint64_t>::max() - total)
            return 0;
        total += stat.m_uncomp_size;
        if (total > kMaximumInstallSize)
            return total;
    }
    return total;
}

std::uint64_t directory_size(const fs::path &root) {
    std::uint64_t total = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        if (it->is_regular_file(ec)) {
            const auto size = it->file_size(ec);
            if (ec || size > std::numeric_limits<std::uint64_t>::max() - total)
                return 0;
            total += size;
        }
        it.increment(ec);
    }
    return total;
}

bool enough_space(const fs::path &documents, std::uint64_t bytes, std::string &error) {
    std::error_code ec;
    const auto space = fs::space(documents, ec);
    if (ec) {
        error = "Cannot query free storage: " + ec.message();
        return false;
    }
    if (bytes > kMaximumInstallSize || bytes > space.available
        || kSafetyMargin > space.available - bytes) {
        error = "Not enough free storage for the GE:R installation.";
        return false;
    }
    return true;
}

InstallResult fail_result(std::string message) {
    InstallResult result;
    result.message = std::move(message);
    return result;
}

InstallResult replace_transactionally(const fs::path &temporary,
    const fs::path &destination) {
    InstallResult result;
    std::error_code ec;

    const auto backup = destination.parent_path() / ".PCSE00801.backup";
    fs::remove_all(backup, ec);
    ec.clear();

    if (is_directory(destination)) {
        fs::rename(destination, backup, ec);
        if (ec)
            return fail_result("Could not prepare the existing GE:R installation for replacement: " + ec.message());
    }

    fs::rename(temporary, destination, ec);
    if (ec) {
        if (is_directory(backup)) {
            std::error_code restore_ec;
            fs::rename(backup, destination, restore_ec);
        }
        return fail_result("Could not finalize the GE:R installation: " + ec.message());
    }

    fs::remove_all(backup, ec);
    result.success = true;
    result.install_root = destination;
    return result;
}

} // namespace

fs::path NativeInstaller::documents_root() {
    NSString *path = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    return path ? fs::path(path.UTF8String) : fs::path{};
}

fs::path NativeInstaller::game_root() {
    return documents_root() / "PCSE00801";
}

bool NativeInstaller::ensure_storage(std::string &error) {
    const auto documents = documents_root();
    if (documents.empty()) {
        error = "iOS Documents directory is unavailable.";
        return false;
    }

    std::error_code ec;
    fs::create_directories(game_root(), ec);
    if (ec) {
        error = "Could not create the GE:R storage directory: " + ec.message();
        return false;
    }
    return true;
}

InstallResult NativeInstaller::install_folder(
    const fs::path &selected_path,
    const fs::path &game_source,
    const InstallProgress &progress) {
    const auto root = locate_game_root(game_source);
    if (root.empty())
        return fail_result("The selected location does not contain a complete PCSE00801 game folder (sce_sys/param.sfo + eboot.bin).");

    const auto size = directory_size(root);
    std::string error;
    if (!enough_space(documents_root(), size, error))
        return fail_result(error);

    const auto destination = game_root();
    const auto temporary = destination.parent_path() / ".PCSE00801.installing";
    std::error_code ec;
    fs::remove_all(temporary, ec);

    if (progress)
        progress(0.02, "Preparing GE:R installation…");

    __block BOOL coordinated = NO;
    __block NSError *coordinationError = nil;

    NSURL *sourceURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:root.string().c_str()]
                                   isDirectory:YES];
    NSURL *temporaryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:temporary.string().c_str()]
                                      isDirectory:YES];

    NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    [coordinator coordinateReadingItemAtURL:sourceURL
                                    options:0
                         writingItemAtURL:temporaryURL
                                    options:0
                                      error:&coordinationError
                                 byAccessor:^(NSURL *newSourceURL, NSURL *newTemporaryURL) {
        NSError *copyError = nil;
        [[NSFileManager defaultManager] createDirectoryAtURL:newTemporaryURL
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:&copyError];
        if (!copyError)
            [[NSFileManager defaultManager] copyItemAtURL:newSourceURL
                                                     toURL:newTemporaryURL
                                                     error:&copyError];
        coordinated = (copyError == nil);
        if (copyError)
            coordinationError = copyError;
    }];

    (void)selected_path;
    if (!coordinated) {
        fs::remove_all(temporary, ec);
        return fail_result(coordinationError.localizedDescription.UTF8String ?: "Could not copy the GE:R game folder.");
    }

    if (progress)
        progress(0.95, "Finalizing GE:R installation…");

    auto result = replace_transactionally(temporary, destination);
    result.bytes_installed = size;
    if (result.success && progress)
        progress(1.0, "GE:R installation complete.");
    return result;
}

InstallResult NativeInstaller::install_archive(
    const fs::path &archive_path,
    const InstallProgress &progress) {
    mz_zip_archive zip{};
    const auto archiveText = archive_path.string();
    if (!mz_zip_reader_init_file(&zip, archiveText.c_str(), 0))
        return fail_result("Could not open the selected ZIP/VPK archive.");

    const auto root = find_archive_root(zip);
    if (root.empty()) {
        mz_zip_reader_end(&zip);
        return fail_result("The archive does not contain PCSE00801/sce_sys/param.sfo.");
    }

    if (!archive_has_eboot(zip, root)) {
        mz_zip_reader_end(&zip);
        return fail_result("The archive contains PCSE00801 but no eboot.bin.");
    }

    const auto total = archive_size(zip, root);
    if (total == 0 || total > kMaximumInstallSize) {
        mz_zip_reader_end(&zip);
        return fail_result("The archive has an invalid or unsupported installation size.");
    }

    std::string error;
    if (!enough_space(documents_root(), total, error)) {
        mz_zip_reader_end(&zip);
        return fail_result(error);
    }

    const auto destination = game_root();
    const auto temporary = destination.parent_path() / ".PCSE00801.installing";
    std::error_code ec;
    fs::remove_all(temporary, ec);
    fs::create_directories(temporary, ec);
    if (ec) {
        mz_zip_reader_end(&zip);
        return fail_result("Could not create the temporary installation directory: " + ec.message());
    }

    const auto count = mz_zip_reader_get_num_files(&zip);
    std::uint64_t extracted = 0;
    std::size_t extractedFiles = 0;

    for (mz_uint i = 0; i < count; ++i) {
        const auto name = archive_name(zip, i);
        if (!name.starts_with(root) || mz_zip_reader_is_file_a_directory(&zip, i))
            continue;

        const auto relative = name.substr(root.size());
        if (!safe_archive_path(relative) || !mz_zip_reader_is_file_supported(&zip, i)) {
            fs::remove_all(temporary, ec);
            mz_zip_reader_end(&zip);
            return fail_result("Installation rejected an unsafe or unsupported archive entry.");
        }

        const auto output = temporary / fs::path(relative);
        fs::create_directories(output.parent_path(), ec);
        if (ec || !mz_zip_reader_extract_to_file(&zip, i, output.string().c_str(), 0)) {
            fs::remove_all(temporary, ec);
            mz_zip_reader_end(&zip);
            return fail_result("Failed to extract a GE:R archive entry.");
        }

        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            fs::remove_all(temporary, ec);
            mz_zip_reader_end(&zip);
            return fail_result("Failed to read GE:R archive metadata.");
        }

        extracted += stat.m_uncomp_size;
        ++extractedFiles;

        if (progress) {
            const double fraction = total == 0 ? 0.0 : static_cast<double>(extracted) / static_cast<double>(total);
            progress(std::min(0.92, fraction * 0.92), "Extracting GE:R…");
        }
    }

    mz_zip_reader_end(&zip);

    if (extractedFiles == 0 || !valid_game_root(temporary)) {
        fs::remove_all(temporary, ec);
        return fail_result("Extraction finished, but the resulting PCSE00801 installation is incomplete.");
    }

    if (progress)
        progress(0.95, "Finalizing GE:R installation…");

    auto result = replace_transactionally(temporary, destination);
    result.bytes_installed = extracted;
    if (result.success && progress)
        progress(1.0, "GE:R installation complete.");
    return result;
}

} // namespace ger::ios
