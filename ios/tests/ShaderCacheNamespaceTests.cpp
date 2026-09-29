#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <renderer/shader_cache_namespace.h>
#include <string>

static void require(bool ok) {
    if (!ok)
        std::abort();
}
int main(int argc, char **argv) {
    require(argc == 2);
    const std::filesystem::path root(argv[1]);
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    // Old format and a force-quit shader without a hashes manifest must never
    // be mistaken for the same shader compiled for the other accuracy mode.
    std::ofstream(root / "program.spv") << "legacy";
    for (uint32_t mask : { 0U, 1U, 3U, 7U, 15U, 31U, UINT32_MAX }) {
        auto path = root / renderer::ios_shader_cache_namespace(mask);
        require(!std::filesystem::exists(path / "program.spv"));
        std::filesystem::create_directories(path);
        std::ofstream(path / "program.spv") << mask;
        std::ofstream(path / "pipeline.dat") << mask;
    }
    for (uint32_t mask : { 0U, 1U, 3U, 7U, 15U, 31U, UINT32_MAX }) {
        auto path = root / renderer::ios_shader_cache_namespace(mask);
        uint32_t shader = 0, pipeline = 0;
        std::ifstream(path / "program.spv") >> shader;
        std::ifstream(path / "pipeline.dat") >> pipeline;
        require(shader == mask && pipeline == mask);
    }
    std::filesystem::remove_all(root);
}
