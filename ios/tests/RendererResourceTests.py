"""Exercise production staging reclamation and logging state without Apple/Vulkan SDKs."""
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]


def function(path, signature):
    source = (repo / path).read_text()
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


source = r'''
#include <cassert>
#include <cstdint>
#include <mutex>
#include <renderer/vulkan/frame_lifetime.h>
using renderer::vulkan::should_release_staging_buffer;
struct Buffer {
    uint64_t size = 0;
    int destroyed = 0;
    void destroy() { ++destroyed; }
};
struct Staging {
    Buffer buffer;
    uint32_t used_so_far = 0;
    uint64_t frame_timestamp = ~uint64_t{0};
    uint64_t scene_timestamp = ~uint64_t{0};
    void *waiting_fence = nullptr;
};
struct VKTextureCache {
    Staging staging_buffers[6];
    void trim_staging_buffers(uint64_t frame_timestamp);
};
namespace spdlog {
namespace level { enum level_enum { info, debug, warn, off }; }
level::level_enum actual = level::info;
void set_level(level::level_enum level) { actual = level; }
}
namespace logging {
std::mutex s_level_mutex;
bool s_logging_enabled = true;
spdlog::level::level_enum s_requested_level = spdlog::level::info;
'''
source += function('vita3k/util/src/logging.cpp', 'void set_level(')
source += function('vita3k/util/src/logging.cpp', 'void set_enabled(')
source += function('vita3k/util/src/logging.cpp', 'bool is_enabled(')
source += '\n}\n'
source += function('vita3k/renderer/src/vulkan/texture.cpp', 'void VKTextureCache::trim_staging_buffers(')
source += r'''
int main() {
    // Cold disabled -> config load must remain disabled -> reenable original level.
    logging::set_enabled(false);
    assert(!logging::is_enabled());
    logging::set_level(spdlog::level::debug);
    assert(spdlog::actual == spdlog::level::off);
    logging::set_enabled(true);
    assert(spdlog::actual == spdlog::level::debug);
    assert(logging::is_enabled());
    logging::set_enabled(false);
    logging::set_level(spdlog::level::warn);
    assert(spdlog::actual == spdlog::level::off);
    logging::set_enabled(true);
    assert(spdlog::actual == spdlog::level::warn);

    VKTextureCache cache;
    for (auto &s : cache.staging_buffers) {
        s.buffer.size = 8 * 1024 * 1024;
        s.frame_timestamp = 1;
        s.scene_timestamp = 42;
        s.used_so_far = 1024;
    }
    cache.staging_buffers[1].frame_timestamp = 120; // GPU still using it
    cache.staging_buffers[2].frame_timestamp = 2; // idle119, preserve warm buffer
    cache.staging_buffers[3].frame_timestamp = 200; // future timestamp
    cache.staging_buffers[4].frame_timestamp = ~uint64_t{0}; // unused sentinel
    cache.staging_buffers[5].buffer.size = 1024 * 1024; // retain small allocations
    cache.trim_staging_buffers(121);
    const auto &released = cache.staging_buffers[0];
    assert(released.buffer.destroyed == 1 && released.buffer.size == 0);
    assert(released.used_so_far == 0 && released.waiting_fence == nullptr);
    assert(released.frame_timestamp == ~uint64_t{0});
    assert(released.scene_timestamp == ~uint64_t{0});
    for (int i = 1; i < 6; ++i) assert(cache.staging_buffers[i].buffer.destroyed == 0);
    cache.trim_staging_buffers(121);
    assert(cache.staging_buffers[0].buffer.destroyed == 1); // no double destruction
    // Same buffer can be repopulated and later reclaimed again.
    cache.staging_buffers[0].buffer.size = 4 * 1024 * 1024;
    cache.staging_buffers[0].frame_timestamp = 121;
    cache.trim_staging_buffers(122);
    assert(cache.staging_buffers[0].buffer.destroyed == 1);
    cache.trim_staging_buffers(241);
    assert(cache.staging_buffers[0].buffer.destroyed == 2);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = pathlib.Path(tmp) / 'resource.cpp'
    binary = pathlib.Path(tmp) / 'resource'
    cpp.write_text(source)
    subprocess.run([sys.argv[1], '-std=c++17', '-pthread', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(repo / 'vita3k/renderer/include'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

# Integration guards complement the executed state transitions above.
logging = (repo / 'vita3k/util/src/logging.cpp').read_text()
assert 'logger->set_level(s_logging_enabled ? s_requested_level : spdlog::level::off)' in logging
assert 'log_path.generic_path().native(), s_logging_enabled)' in logging
assert 'set_enabled(enabled);' in function('vita3k/util/src/logging.cpp', 'ExitCode init(')
frontend = function('ios/src/NativeFrontend.mm', 'bool vita3k_ios_logging_enabled()')
assert '@"tsubomi.collectLogs"' in frontend and 'value == nil || [value boolValue]' in frontend
assert 'vita3k_ios_logging_enabled())' in (repo / 'ios/src/UpstreamMain.cpp').read_text()
assert 'logging::set_enabled(vita3k_ios_logging_enabled())' in (repo / 'ios/src/TsubomiBridge.mm').read_text()
assert 'case .collectLogs,' in (repo / 'ios/src/Swift/DefaultsToggle.swift').read_text()
scene = function('vita3k/renderer/src/vulkan/scene.cpp', 'void draw(')
assert scene.index('if (context.current_pipeline == nullptr)') < scene.index('vk::ImageMemoryBarrier barrier')
assert scene.index('if (context.current_pipeline == nullptr)') < scene.index('context.render_cmd.endRenderPass()')
frame = function('vita3k/renderer/src/vulkan/context.cpp', 'void new_frame(')
assert frame.index('device.waitForFences(') < frame.index('texture_cache.trim_staging_buffers(')
print('Production resource transitions and frontend/draw integration checks passed')
