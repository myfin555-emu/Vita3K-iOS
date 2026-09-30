"""Exercise the actual bounded driver-cache and shader-regeneration retry path."""
import pathlib
import subprocess
import sys
import tempfile
repo = pathlib.Path(__file__).resolve().parents[3]
source = (repo/'vita3k/renderer/src/vulkan/pipeline_cache.cpp').read_text()
start = source.index('    // Use the non-throwing overload:')
end = source.index('\n}\n\nvk::Pipeline PipelineCache::retrieve_pipeline', start)
code = r'''
#include <vkutil/vulkan.h>
#include <bit>
#include <cassert>
#include <vector>
#define LOG_WARN(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
struct State {
    struct Device {
        std::vector<vk::Result> results;
        unsigned calls = 0, destroyed = 0;
        vk::Result createGraphicsPipelines(vk::PipelineCache cache, int,
            const vk::GraphicsPipelineCreateInfo *info, void *, vk::Pipeline *result) {
            assert(calls < results.size());
            if (calls > 0) assert(!cache);
            if (info->pStages[0].module) {
                assert(info->pStages[0].module == std::bit_cast<vk::ShaderModule>(uint64_t{1}));
                if (info->stageCount == 2)
                    assert(info->pStages[1].module == std::bit_cast<vk::ShaderModule>(uint64_t{2}));
            }
            *result = std::bit_cast<vk::Pipeline>(uint64_t{99});
            return results[calls++];
        }
        void destroyPipeline(vk::Pipeline pipeline) { assert(pipeline); ++destroyed; }
    } device;
    unsigned regenerated = 0;
};
struct Mem {};
struct Program {};
struct Hints {};
vk::Pipeline compile(State &state, bool fragment = true) {
    Mem mem; Program program; Hints hints;
    struct { struct { Program *p; Program *get(Mem &) { return p; } } program; } vertex_program_gxm{{&program}};
    struct { bool is_maskupdate = false; } fragment_program_gxm;
    Program *gxm_fragment_shader = &program;
    struct { int hash = 0; } vertex_program, fragment_program;
    struct { bool is_gamma_corrected = false; } record;
    const uint32_t shader_stage_count = fragment ? 2 : 1;
    vk::PipelineShaderStageCreateInfo shader_stages[2]{};
    vk::PipelineVertexInputStateCreateInfo vertex_input{};
    vk::GraphicsPipelineCreateInfo pipeline_info{.stageCount = shader_stage_count, .pStages = shader_stages};
    vk::PipelineCache pipeline_cache = std::bit_cast<vk::PipelineCache>(uint64_t{10});
    const int type = 0;
    auto retrieve_shader = [&](Program *, int, bool vertex, bool, Mem &, const Hints &, bool, bool regenerate) {
        assert(regenerate); ++state.regenerated;
        return vk::PipelineShaderStageCreateInfo{.module = std::bit_cast<vk::ShaderModule>(uint64_t{vertex ? 1U : 2U})};
    };
'''
code += source[start:end] + '\n}\n'
code += r'''
int main() {
    using R = vk::Result;
    State normal{{{R::eSuccess}}};
    assert(compile(normal) && normal.device.calls == 1 && normal.regenerated == 0);
    State recovered{{{R::eErrorInitializationFailed, R::eErrorInitializationFailed, R::eSuccess}}};
    assert(compile(recovered));
    assert(recovered.device.calls == 3 && recovered.device.destroyed == 2 && recovered.regenerated == 2);
    State unknown{{{R::eErrorUnknown, R::eSuccess}}};
    assert(compile(unknown, false) && unknown.regenerated == 1);
    State oom{{{R::eErrorOutOfDeviceMemory}}};
    assert(!compile(oom) && oom.device.calls == 1 && oom.regenerated == 0);
    State permanent{{{R::eErrorInitializationFailed, R::eErrorInitializationFailed, R::eErrorUnknown}}};
    assert(!compile(permanent));
    assert(permanent.device.calls == 3 && permanent.device.destroyed == 3 && permanent.regenerated == 2);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path/'test.cpp').write_text(code)
    subprocess.run([sys.argv[1], '-std=c++20', '-UNDEBUG', str(path/'test.cpp'),
        '-I'+str(repo/'vita3k/vkutil/include'), '-I'+sys.argv[2], '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True, timeout=5)
print('Production pipeline regeneration, resource cleanup and bounded failure checks passed')
