"""Exercise production disk loading and rejected-module recovery with a controlled driver."""
import pathlib
import subprocess
import sys
import tempfile
repo = pathlib.Path(__file__).resolve().parents[3]

def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

pipeline = (repo / 'vita3k/renderer/src/vulkan/pipeline_cache.cpp').read_text()
shaders = (repo / 'vita3k/renderer/src/shaders.cpp').read_text()
code = r'''
#include <vkutil/vulkan.h>
#include <renderer/spirv_cache.h>
#include <renderer/single_flight.h>
#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>
#define LOG_WARN(...) ((void)0)
#define LOG_WARN_ONCE(...) ((void)0)
#define LOG_INFO(...) ((void)0)
namespace fs { using namespace std::filesystem; using ifstream = std::ifstream; }
using Sha256Hash = std::array<uint8_t, 32>;
std::string hex_string(const Sha256Hash &h) { return std::to_string(h[0]); }
struct MemState {};
struct SceGxmProgram { bool is_frag_color_used() const { return false; } };
struct Features { bool should_use_shader_interlock() const { return false; } };
namespace shader { constexpr int CURRENT_VERSION = 1; struct Hints {}; namespace usse { using SpirvCode = std::vector<uint32_t>; } }
const vk::SpecializationInfo srgb_info_true{}, srgb_info_false{};
const std::vector<uint32_t> good{0x07230203, 0x00010000, 42, 1, 0, 0x00010000};
int regenerated = 0;
std::vector<uint32_t> load_spirv_shader(const SceGxmProgram &, const Features &, bool,
    const shader::Hints &, bool, const fs::path &, const fs::path &, const std::string &, bool read_cache) {
    assert(!read_cache); ++regenerated; return good;
}
namespace renderer {
'''
code += function(shaders, 'std::vector<uint32_t> pre_load_shader_spirv(') + '\n}\n'
code += r'''
struct State {
    Features features;
    fs::path shaders_path, shaders_log_path;
    struct Device {
        int calls = 0;
        vk::Result failure = vk::Result::eErrorUnknown;
        vk::ShaderModule createShaderModule(const vk::ShaderModuleCreateInfo &info) {
            ++calls;
            if (info.pCode[2] == 0xBAD)
                throw vk::SystemError(vk::make_error_code(failure), "synthetic driver rejection");
            assert(info.pCode[2] == 42);
            return std::bit_cast<vk::ShaderModule>(uint64_t{123} + calls);
        }
    } device;
    struct Hashes { Sha256Hash vert, frag; };
    std::vector<Hashes> shaders_cache_hashs;
};
struct PipelineCache {
    State &state;
    std::mutex shaders_mutex;
    renderer::SingleFlight<Sha256Hash> shader_generation;
    std::map<Sha256Hash, vk::ShaderModule> shaders;
    std::vector<vk::ShaderModule> retired_shaders;
    vk::ShaderModule precompile_shader(const Sha256Hash &, bool);
    vk::PipelineShaderStageCreateInfo retrieve_shader(const SceGxmProgram *, const Sha256Hash &, bool, bool, MemState &, const shader::Hints &, bool, bool = false);
};
'''
code += function(pipeline, 'vk::ShaderModule PipelineCache::precompile_shader(')
code += function(pipeline, 'vk::PipelineShaderStageCreateInfo PipelineCache::retrieve_shader(')
code += r'''
void write(const fs::path &path, const std::vector<uint32_t> &words) {
    std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char *>(words.data()), words.size() * 4);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    State state; state.shaders_path = argv[1];
    PipelineCache cache{state};
    const fs::path file = state.shaders_path / "vk1-1.spv";
    Sha256Hash hash{}; hash[0] = 1;
    SceGxmProgram program; MemState mem; shader::Hints hints;
    write(file, good);
    assert(renderer::pre_load_shader_spirv(file) == good);
    for (auto bad : std::vector<std::vector<uint32_t>>{
        {}, {0x07230203}, {0x07230203, 0x10000, 42, 1, 0},
        {0x07230203, 0x10000, 42, 1, 0, 0},
        {0x07230203, 0x10000, 42, 1, 0, 0x00020000},
        {0xBAD, 0x10000, 42, 1, 0, 0x00010000}}) {
        write(file, bad);
        assert(renderer::pre_load_shader_spirv(file).empty());
    }
    { std::ofstream f(file, std::ios::binary); f << "abc"; }
    assert(renderer::pre_load_shader_spirv(file).empty());
    // Truncated cache regenerates on the required draw.
    assert(cache.retrieve_shader(&program, hash, true, false, mem, hints, false).module);
    assert(regenerated == 1);
    assert(cache.retrieve_shader(&program, hash, true, false, mem, hints, false).module);
    assert(regenerated == 1); // In-memory cache avoids duplicate translation.
    cache.shaders.clear();
    auto rejected = good; rejected[2] = 0xBAD; write(file, rejected);
    assert(!cache.precompile_shader(hash, true)); // Warmup survives rejected cache.
    assert(cache.retrieve_shader(&program, hash, false, false, mem, hints, false).module);
    assert(regenerated == 2); // Rejected disk module never silently removes an effect.
    assert(cache.retrieve_shader(&program, hash, false, false, mem, hints, false, true).module);
    assert(regenerated == 3 && cache.retired_shaders.size() == 1);
    assert(cache.retired_shaders[0] != cache.shaders.at(hash));
    cache.shaders.clear();
    state.device.failure = vk::Result::eErrorOutOfDeviceMemory;
    bool failed = false;
    try { cache.retrieve_shader(&program, hash, true, false, mem, hints, false); }
    catch (const vk::SystemError &) { failed = true; }
    assert(failed && regenerated == 3); // OOM must not become an infinite retry.
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.cpp').write_text(code)
    subprocess.run([sys.argv[1], '-std=c++20', '-pthread', '-UNDEBUG', str(path/'test.cpp'),
        '-I'+str(repo/'vita3k/renderer/include'), '-I'+str(repo/'vita3k/vkutil/include'),
        '-I'+str(repo/'external/fmt/include'), '-I'+sys.argv[2], '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test'), tmp], check=True, timeout=10)
print('Production shader cache framing and GXP regeneration checks passed')
