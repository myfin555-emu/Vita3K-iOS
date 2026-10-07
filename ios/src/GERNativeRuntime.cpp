#include <vita3k_ios/GERNativeRuntime.h>

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
    // param.sfo is intentionally only used as an identity hint here.
    // The authoritative GE:R install identifier remains PCSE00801.
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

    const std::array<const char *, 3> eboots = {"eboot.bin", "EBOOT.BIN", "eboot.bin.self"};
    for (const auto *name : eboots) {
        if (regular_file(root / name)) {
            game.has_eboot = true;
            break;
        }
    }

    return game;
}

NativeRuntime::NativeRuntime() = default;

RuntimeStatus NativeRuntime::scan() {
    status_ = {};
    status_.game.title_id = kTitleId;

    for (const auto &root : candidate_game_roots()) {
        if (!directory(root))
            continue;

        auto game = inspect_game(root);
        if (!game.has_sce_sys && !game.has_param_sfo && !game.has_eboot)
            continue;

        status_.game = std::move(game);
        const bool identity_ok = status_.game.title_id == kTitleId;
        status_.ready = identity_ok && status_.game.has_param_sfo && status_.game.has_eboot;

        if (status_.ready) {
            status_.message = "GE:R native runtime: game data found.";
        } else {
            status_.message =
                "GE:R data directory found, but sce_sys/param.sfo and eboot.bin are required.";
        }
        return status_;
    }

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

    // Deliberately no Vita CPU/GXM interpreter is started here.
    // This runtime owns a native game loop; GE:R game-system implementations
    // are added here as they are recovered/reimplemented.
    elapsed_ = 0.0;
    running_ = true;
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
