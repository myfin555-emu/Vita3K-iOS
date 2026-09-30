"""Exercise menu copy reuse, Metal index upload and precompile presentation code."""
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[3]
surface = (repo / 'vita3k/renderer/src/vulkan/surface_cache.cpp').read_text()
scene = (repo / 'vita3k/renderer/src/vulkan/scene.cpp').read_text()
pipeline = (repo / 'vita3k/renderer/src/vulkan/pipeline_cache.cpp').read_text()
batch = (repo / 'vita3k/renderer/src/batch.cpp').read_text()

code = r'''
#include <renderer/surface_copy_reuse.h>
#include <renderer/strip_indices.h>
#include <vkutil/vulkan.h>
#include <atomic>
#include <cassert>
#include <optional>
#include <vector>
using namespace renderer;
#define __APPLE__ 1
struct TextureLookupResult { int view, layout; vk::Format format; };
struct CastedTexture {
    uint32_t cropped_height = 8, cropped_width = 8, cropped_y = 0, cropped_x = 0;
    int format = 1;
    struct { int view = 42, layout = 1; vk::Format format = vk::Format::eR8G8B8A8Unorm; } texture;
    vk::ComponentMapping components{};
    uint64_t scene_timestamp = 1, source_generation = 1;
    bool copied_from_active_target = false;
};
std::optional<TextureLookupResult> lookup(std::vector<CastedTexture> &casted_vec,
    uint64_t source_generation, uint64_t scene_timestamp, bool is_same_image,
    vk::Format vk_format = vk::Format::eR8G8B8A8Unorm,
    vk::ComponentMapping resulting_swizzle = {}) {
    struct { uint64_t content_generation; } info{source_generation};
    uint32_t height = 8, width = 8, start_sourced_line = 0, start_x = 0;
    int base_format = 1;
    CastedTexture *casted = nullptr;
'''
start = surface.index('        for (size_t i = 0; i < casted_vec.size();)')
end = surface.index('        // use prerender cmd', start)
code += surface[start:end] + '\nreturn std::nullopt;\n}\n'
# Execute the source generation increment used on every cached target binding.
start = surface.index('            info.last_frame_rendered = context->frame_timestamp;')
end = surface.index('\n\n', start)
code += 'struct Source { uint64_t last_frame_rendered = 0, content_generation = 1; };\n'
code += 'void rebind(Source &info) { struct { uint64_t frame_timestamp = 1; } ctx; auto *context = &ctx;\n'
code += surface[start:end] + '\n}\n'
code += 'void record_copy(CastedTexture *casted, Source &info, uint64_t scene_timestamp, bool is_same_image) {\n'
start = surface.index('        casted->scene_timestamp = scene_timestamp;')
end = surface.index('\n#ifdef', start)
code += surface[start:end] + '\n}\n'
code += r'''
enum SceGxmPrimitiveType { SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_PRIMITIVE_TRIANGLE_FAN };
enum SceGxmIndexFormat { SCE_GXM_INDEX_FORMAT_U16, SCE_GXM_INDEX_FORMAT_U32 };
vk::PrimitiveTopology translate_primitive(SceGxmPrimitiveType type) {
    return type == SCE_GXM_PRIMITIVE_TRIANGLE_STRIP ? vk::PrimitiveTopology::eTriangleStrip
        : type == SCE_GXM_PRIMITIVE_TRIANGLE_FAN ? vk::PrimitiveTopology::eTriangleFan : vk::PrimitiveTopology::eTriangleList;
}
bool restart(SceGxmPrimitiveType type, bool mapping) {
    struct { struct { bool enable_memory_mapping; } features; } state{{mapping}};
'''
start = pipeline.index('    const vk::PipelineInputAssemblyStateCreateInfo input_assembly{')
end = pipeline.index('\n    };', start) + len('\n    };')
code += pipeline[start:end] + '\nreturn input_assembly.primitiveRestartEnable;\n}\n'
code += r'''
template <typename Index>
void upload(std::vector<Index> input, bool fan, std::vector<uint32_t> expected) {
    SceGxmPrimitiveType type = fan ? SCE_GXM_PRIMITIVE_TRIANGLE_FAN : SCE_GXM_PRIMITIVE_TRIANGLE_STRIP;
    SceGxmIndexFormat format = sizeof(Index) == 2 ? SCE_GXM_INDEX_FORMAT_U16 : SCE_GXM_INDEX_FORMAT_U32;
    struct { struct { struct { bool enable_memory_mapping = false; } features; } state; } context;
    const void *indices_ptr = input.data();
    size_t count = input.size();
'''
start = scene.index('#ifdef __APPLE__\n    std::vector<uint32_t> expanded_indices;')
end = scene.index('\n#endif', start) + len('\n#endif')
code += scene[start:end]
code += r'''
    if (expected.empty()) {
        assert(count == input.size() && indices_ptr == input.data());
        assert(restart(type, false));
    } else {
        assert(type == SCE_GXM_PRIMITIVE_TRIANGLES && format == SCE_GXM_INDEX_FORMAT_U32);
        assert(!restart(type, false) && count == expected.size());
        assert(std::equal(expected.begin(), expected.end(), static_cast<const uint32_t *>(indices_ptr)));
    }
}
// Deterministic clock at the renderer boundary: each present costs 16 ms.
namespace fake { namespace chrono {
struct milliseconds { int n; };
int tick = 0;
struct steady_clock { static int now() { return tick; } };
int operator+(int t, milliseconds m) { return t + m.n; }
} }
struct Overlay { int last = 0; void set_progress(int n, int) { last = n; } };
struct State {
    std::vector<int> precompile_queue = std::vector<int>(106);
    std::atomic<bool> render_abort{false};
    int precompile_progress = 0, compiled = 0, presented = 0, abort_after = 0, compile_ms = 0;
    bool set_current() { return true; }
    void precompile_shader(int) { fake::chrono::tick += compile_ms; ++compiled; if (compiled == abort_after) render_abort = true; }
    void render_frame(int, int, int) {}
    void swap_window() { ++presented; fake::chrono::tick += 16; }
};
void precompile(State &state, Overlay *progress_overlay) {
    int total = state.precompile_queue.size(), display = 0, gxm = 0, mem = 0;
'''
start = batch.index('        auto next_progress_frame =')
end = batch.index('        state.precompile_queue.clear();', start)
code += batch[start:end].replace('std::chrono::', 'fake::chrono::')
code += r'''
}
int main() {
    std::vector<CastedTexture> copies(1);
    Source source;
    // One cached copy serves repeated menu passes/frames while its source is unchanged.
    for (uint64_t scene = 1; scene <= 1000; ++scene)
        assert(lookup(copies, source.content_generation, scene, false)->view == 42);
    rebind(source);
    assert(source.content_generation == 2 && !lookup(copies, source.content_generation, 1001, false));
    rebind(source); // second write in the SAME frame must invalidate too
    assert(source.content_generation == 3);
    record_copy(&copies[0], source, 1002, false);
    assert(lookup(copies, 3, 1003, false));
    record_copy(&copies[0], source, 1004, true);
    assert(!lookup(copies, 3, 1005, false));
    copies[0] = CastedTexture{};
    assert(!lookup(copies, 1, 2, false, vk::Format::eR8G8B8A8Srgb));
    vk::ComponentMapping bgra{vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eA};
    assert(!lookup(copies, 1, 2, false, vk::Format::eR8G8B8A8Unorm, bgra));
    copies[0].copied_from_active_target = true;
    assert(lookup(copies, 1, 1, true));
    assert(!lookup(copies, 1, 2, true) && !lookup(copies, 1, 2, false));
    copies[0].copied_from_active_target = false;
    assert(!lookup(copies, 1, 2, true));
    copies[0].scene_timestamp = 0;
    assert(!lookup(copies, 1, 1, false));
    upload<uint16_t>({0, 1, 2, 3}, false, {});
    upload<uint16_t>({0, 1, 65535, 3}, false, {0, 1, 65535, 1, 3, 65535});
    upload<uint16_t>({65535, 1, 2, 3}, true, {1, 2, 65535, 2, 3, 65535});
    upload<uint32_t>({0, 1, 0xffffffff, 3}, false, {0, 1, 0xffffffff, 1, 3, 0xffffffff});
    upload<uint16_t>({}, false, {});
    upload<uint16_t>({65535, 1}, false, {});
    assert(!restart(SCE_GXM_PRIMITIVE_TRIANGLES, false));
    assert(!restart(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, true));
    Overlay overlay;
    State state;
    precompile(state, &overlay);
    assert(state.compiled == 106 && state.precompile_progress == 106 && overlay.last == 106);
    assert(state.presented == 2); // previously 106 vsync waits for these cached entries
    State slow;
    slow.compile_ms = 5;
    precompile(slow, &overlay);
    assert(slow.compiled == 106 && slow.presented > 2 && slow.presented < 20 && overlay.last == 106);
    State aborted;
    aborted.abort_after = 5;
    precompile(aborted, &overlay);
    assert(aborted.compiled == 5);
    State no_overlay;
    precompile(no_overlay, nullptr);
    assert(no_overlay.compiled == 106 && no_overlay.presented == 0);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.cpp').write_text(code)
    subprocess.run([sys.argv[1], '-std=c++20', '-UNDEBUG', str(path / 'test.cpp'),
                    '-I' + str(repo / 'vita3k/renderer/include'),
                    '-I' + str(repo / 'vita3k/vkutil/include'), '-I' + sys.argv[2],
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Menu copy reuse, Metal indices and shader loading regressions passed')
