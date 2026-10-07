#include <vita3k_ios/GERNativeRuntime.h>
#include <vita3k_ios/GERNativeLogger.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace ger::ios {
namespace fs = std::filesystem;

namespace {

constexpr const char *kTitleId = "PCSE00801";

bool regular_file(const fs::path &path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

bool directory(const fs::path &path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

std::string read_param_title_id(const fs::path &param) {
    std::ifstream stream(param, std::ios::binary);
    if (!stream)
        return {};

    std::array<std::uint8_t, 0x800> bytes{};
    stream.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
    const auto count = static_cast<std::size_t>(stream.gcount());

    const std::string needle = kTitleId;
    for (std::size_t i = 0; i + needle.size() <= count; ++i) {
        if (std::equal(needle.begin(), needle.end(), bytes.begin() + i))
            return needle;
    }
    return {};
}

fs::path find_eboot(const fs::path &root) {
    const std::array<const char *, 3> eboots = {"eboot.bin", "EBOOT.BIN", "eboot.bin.self"};
    for (const auto *name : eboots) {
        const auto path = root / name;
        if (regular_file(path))
            return path;
    }
    return {};
}

} // namespace

std::vector<fs::path> candidate_game_roots() {
    std::vector<fs::path> roots;
    roots.emplace_back("/ux0/app/PCSE00801");

    if (const char *home = std::getenv("HOME"); home != nullptr) {
        roots.emplace_back(fs::path(home) / "Documents/PCSE00801");
        roots.emplace_back(fs::path(home) / "Documents/Games/PCSE00801");
        roots.emplace_back(fs::path(home) / "Library/Application Support/GER-iOS/PCSE00801");
    }

    return roots;
}

GameInstall inspect_game(const fs::path &root) {
    GameInstall game;
    game.root = root;
    game.title_id = kTitleId;
    game.has_sce_sys = directory(root / "sce_sys");
    game.has_param_sfo = regular_file(root / "sce_sys/param.sfo");
    game.has_eboot = !find_eboot(root).empty();
    return game;
}

NativeRuntime::NativeRuntime() = default;

RuntimeStatus NativeRuntime::scan() {
    NativeLogger::write("runtime: scan begin");
    status_ = {};
    status_.game.title_id = kTitleId;

    for (const auto &root : candidate_game_roots()) {
        NativeLogger::write("runtime: checking root " + root.string());
        if (!directory(root))
            continue;

        auto game = inspect_game(root);
        if (!game.has_sce_sys && !game.has_param_sfo && !game.has_eboot)
            continue;

        status_.game = std::move(game);
        const auto param_id = read_param_title_id(status_.game.root / "sce_sys/param.sfo");
        const bool identity_ok = param_id.empty() || param_id == kTitleId;
        status_.ready = identity_ok && status_.game.has_param_sfo && status_.game.has_eboot;

        if (status_.ready) {
            status_.message = "GE:R native runtime: game data found.";
        } else {
            status_.message =
                "GE:R data directory found, but sce_sys/param.sfo and eboot.bin are required.";
            if (!param_id.empty() && param_id != kTitleId)
                status_.message += " param.sfo title ID is not PCSE00801.";
        }

        NativeLogger::write(
            "runtime: selected root=" + status_.game.root.string() +
            " ready=" + std::string(status_.ready ? "yes" : "no"));
        return status_;
    }

    NativeLogger::write("runtime: no game root found");
    status_.message =
        "GE:R not installed. Expected /ux0/app/PCSE00801/ or the app Documents fallback.";
    return status_;
}

bool NativeRuntime::start(std::string &error) {
    if (!status_.ready)
        scan();

    if (!status_.ready) {
        error = status_.message;
        return false;
    }

    const auto eboot = find_eboot(status_.game.root);
    if (eboot.empty()) {
        error = "GE:R executable disappeared after scan.";
        return false;
    }

    NativeExecutableImage image;
    std::string loader_error;
    if (!executable_loader_.load(eboot, image, loader_error)) {
        status_.executable_loaded = false;
        status_.loader_message = loader_error;
        status_.message = "GE:R data found, but native executable image loading failed: " + loader_error;
        error = status_.message;
        NativeLogger::write("runtime: loader failed: " + loader_error);
        return false;
    }

    status_.executable = std::move(image);
    status_.executable_loaded = true;
    status_.loader_message =
        "ARM32 image prepared; entry=0x" + [&] {
            std::ostringstream out;
            out << std::hex << status_.executable.entry;
            return out.str();
        }() +
        " relocations=" + std::to_string(status_.executable.relocations.size()) +
        " applied=" + std::to_string(status_.executable.relocations_applied) +
        " unsupported=" + std::to_string(status_.executable.relocations_unsupported);

    // This is intentionally an image-construction boundary, not a claim that
    // iOS can execute the Vita ARM32 image directly. Native ARM64 execution
    // still requires the recovered/reimplemented GE:R runtime and ARM64 code.
    elapsed_ = 0.0;
    running_ = true;
    NativeLogger::write("runtime: executable image prepared; native ARM64 execution layer not yet attached");
    return true;
}

void NativeRuntime::stop() {
    running_ = false;
    elapsed_ = 0.0;
}

void NativeRuntime::tick(double delta_seconds) {
    if (!running_)
        return;
    elapsed_ += std::max(0.0, delta_seconds);
}

} // namespace ger::ios
