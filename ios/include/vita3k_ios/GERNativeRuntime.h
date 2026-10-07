#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ger::ios {

struct GameInstall {
    std::filesystem::path root;
    std::string title_id;
    bool has_param_sfo = false;
    bool has_eboot = false;
    bool has_sce_sys = false;
};

struct RuntimeStatus {
    bool ready = false;
    std::string message;
    GameInstall game;
};

class NativeRuntime final {
public:
    NativeRuntime();

    RuntimeStatus scan();
    bool start(std::string &error);
    void stop();
    void tick(double delta_seconds);

    bool running() const { return running_; }
    const RuntimeStatus &status() const { return status_; }

private:
    RuntimeStatus status_;
    bool running_ = false;
    double elapsed_ = 0.0;
};

std::vector<std::filesystem::path> candidate_game_roots();
GameInstall inspect_game(const std::filesystem::path &root);

} // namespace ger::ios
