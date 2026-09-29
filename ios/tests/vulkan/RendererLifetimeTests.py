"""Compile production surface retirement and wait-worker code with controlled GPU fences."""
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[3]
baseline = '--baseline' in sys.argv

def source(path):
    if baseline:
        return subprocess.check_output(['git', 'show', 'HEAD:' + path], cwd=repo, text=True)
    return (repo / path).read_text()

def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

surface = source('vita3k/renderer/src/vulkan/surface_cache.cpp')
context = source('vita3k/renderer/src/vulkan/context.cpp')
code = r'''
#include <vkutil/vulkan.h>
#include <renderer/vulkan/surface_readback.h>
#include <renderer/vulkan/frame_descriptor.h>
#include <renderer/vulkan/texture_descriptor_cache.h>
#include <renderer/vulkan/frame_lifetime.h>
using namespace renderer::vulkan;
using renderer::vulkan::SurfaceReadback;
#include <util/overloaded.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <variant>
#include <vector>
#define LOG_ERROR(...) ((void)0)
#define log_hex(x) (x)
using Address = uint32_t;
struct MemState {};
struct Buffer { bool live = true; void *mapped_data = nullptr; };
struct Image { bool live = true; int view = 1; };
namespace vkutil {
using Buffer = ::Buffer;
struct DestroyQueue {
    int images = 0, buffers = 0, views = 0;
    void destroy_objects() {}
    void add_image(Image &i) { assert(i.live); i.live = false; ++images; }
    void add_buffer(Buffer &b) { assert(b.live); b.live = false; ++buffers; }
    template <typename T> void add(T &v) { v = {}; ++views; }
};
}
struct ColorSurfaceCacheInfo {
    struct Cast { Buffer transition_buffer; Image texture; };
    struct View { int view = 1; };
    std::vector<Cast> casted_textures;
    std::vector<View> sampled_views;
    int alternate_view = 2;
    Image texture;
    std::unique_ptr<Image> blit_image = std::make_unique<Image>();
    std::unique_ptr<Buffer> copy_buffer = std::make_unique<Buffer>();
    void *sws_context = reinterpret_cast<void *>(1);
    bool need_post_surface_sync = true, need_buffer_sync = true;
    std::shared_ptr<SurfaceReadback> pending_readback;
    int pixels = 0;
};
int freed_converters = 0;
void sws_freeContext(void *p) { if (p) ++freed_converters; }
struct Pointer {
    uint32_t *ptr = nullptr;
    Pointer() = default;
    explicit Pointer(Address) {}
    operator bool() const { return ptr; }
    uint32_t *get(const MemState &) { return ptr; }
};
template <typename T> using Ptr = Pointer;
struct Notification { Pointer address; uint32_t value; };
struct FenceWaitRequest { vk::Fence fence; };
struct NotificationRequest { Notification notifications[2]; };
struct FrameDoneRequest { uint64_t frame_timestamp; };
struct BufferSyncRequest { Address location, size; };
struct PostSurfaceSyncRequest { ColorSurfaceCacheInfo *cache_info; std::shared_ptr<SurfaceReadback> completion; };
struct SyncSignalRequest { int *sync; uint32_t timestamp; };
using CallbackRequestFunction = std::function<void()>;
struct CallbackRequest { CallbackRequestFunction *callback; };
using Request = std::variant<FenceWaitRequest, NotificationRequest, FrameDoneRequest,
    BufferSyncRequest, PostSurfaceSyncRequest, SyncSignalRequest, CallbackRequest>;
struct Queue {
    std::mutex mutex;
    std::condition_variable changed;
    std::queue<Request> requests;
    bool aborted = false;
    void push(Request r) { std::lock_guard lock(mutex); requests.push(std::move(r)); changed.notify_all(); }
    std::unique_ptr<Request> pop() {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return aborted || !requests.empty(); });
        if (requests.empty()) return nullptr;
        auto r = std::make_unique<Request>(std::move(requests.front())); requests.pop(); return r;
    }
    bool is_aborted() { std::lock_guard lock(mutex); return aborted; }
    void abort() { std::lock_guard lock(mutex); aborted = true; changed.notify_all(); }
};
struct State;
struct VKSurfaceCache {
    State &state;
    void destroy_surface(ColorSurfaceCacheInfo &info);
    void clear_surfaces_changed() {}
    std::vector<int> retired_framebuffer_views{};
    void destroy_framebuffers(int view) { retired_framebuffer_views.push_back(view); }
    void queue_post_surface_sync(ColorSurfaceCacheInfo *surface);
    void perform_post_surface_sync(const MemState &, ColorSurfaceCacheInfo *surface) {
        assert(surface->copy_buffer && surface->copy_buffer->live);
        surface->pixels = 42;
    }
};
struct FrameObject {
    vkutil::DestroyQueue destroy_queue;
    std::vector<vk::Fence> rendered_fences;
    vk::CommandPool prerender_pool, render_pool;
    FrameDescriptor vert_descriptors[16], frag_descriptors[16], color_descriptor;
    TextureDescriptorCache texture_descriptors;
    uint64_t frame_timestamp = 1;
};
struct State {
    struct Features { bool enable_memory_mapping = false, support_unmapped_surface_sync = true; } features;
    struct Device {
        std::shared_future<void> gate;
        std::atomic<bool> entered = false;
        std::atomic<int> resets = 0;
        void resetFences(const std::vector<vk::Fence> &) { ++resets; }
        void resetCommandPool(vk::CommandPool) {}
        void destroy(vk::DescriptorPool) {}
        vk::Result waitForFences(const std::vector<vk::Fence> &, int, uint64_t) {
            entered = true; gate.wait(); return vk::Result::eSuccess;
        }
    } device;
    Queue request_queue;
    std::mutex notification_mutex;
    std::condition_variable notification_ready;
    struct Mapping { size_t size; std::variant<vkutil::Buffer> buffer_impl; };
    std::map<Address, Mapping> mapped_memories;
    std::atomic<int> current_frame_idx = 1;
    FrameObject frames[3];
    FrameObject &frame() { return frames[current_frame_idx]; }
    VKSurfaceCache surface_cache{*this};
};
namespace renderer { void subject_done(int *p, uint32_t value) { *p = value; } }
struct VKContext {
    State &state;
    explicit VKContext(State &state) : state(state) {}
    std::mutex new_frame_mutex;
    std::condition_variable new_frame_condv;
    uint64_t last_frame_waited = 0;
    uint64_t frame_timestamp = 3;
    uint32_t last_vert_texture_count = 0, last_frag_texture_count = 0;
    void wait_thread_function(const MemState &mem);
};
'''
code = code.replace('#include <memory>', '#include <memory>\n#include <map>')
code += function(surface, 'void VKSurfaceCache::destroy_surface(ColorSurfaceCacheInfo &info)')
if not baseline:
    code += function(surface, 'void VKSurfaceCache::queue_post_surface_sync(')
    code += function(context, 'void VKContext::wait_thread_function(')
    # Substitute only the driver boundary; execute the production frame lifecycle.
    code += function(context, 'void new_frame(').replace(
        'vk::Device device = context.state.device;', 'auto &device = context.state.device;')
code += r'''
int main() {
    State state;
    ColorSurfaceCacheInfo old;
    state.surface_cache.destroy_surface(old);
    if (old.blit_image || old.copy_buffer || old.sws_context || old.need_post_surface_sync || old.need_buffer_sync) {
        std::cerr << "Recycled render target retained old readback extent/format resources\n";
        return 1;
    }
    assert(state.frame().destroy_queue.buffers == 1 && state.frame().destroy_queue.images == 2);
    assert(freed_converters == 1);
'''
if not baseline:
    code += r'''
    assert((state.surface_cache.retired_framebuffer_views == std::vector<int>{2, 1}));
    MemState mem;
    VKContext context{state};
    std::promise<void> gpu;
    state.device.gate = gpu.get_future().share();
    std::thread worker([&] { context.wait_thread_function(mem); });
    ColorSurfaceCacheInfo pending;
    uint32_t notification = 0;
    state.request_queue.push(FenceWaitRequest{});
    state.surface_cache.queue_post_surface_sync(&pending);
    state.request_queue.push(NotificationRequest{{{Pointer{}, 0}, {Pointer{}, 0}}});
    NotificationRequest notify{};
    notify.notifications[0].address.ptr = &notification;
    notify.notifications[0].value = 9;
    state.request_queue.push(notify);
    state.frame().rendered_fences.push_back(vk::Fence{});
    auto next_frame = std::async(std::launch::async, [&] { new_frame(context); });
    std::promise<void> retiring;
    auto retired = std::async(std::launch::async, [&] {
        retiring.set_value();
        state.surface_cache.destroy_surface(pending);
        assert(pending.pixels == 42);
    });
    retiring.get_future().wait();
    assert(retired.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
    assert(next_frame.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
    assert(state.device.resets == 0);
    gpu.set_value();
    retired.get();
    next_frame.get();
    assert(state.device.resets == 1);
    assert(notification == 9 && pending.pixels == 42);
    // A finish callback must also drain fences when no readback was queued.
    std::promise<void> second_gpu;
    state.device.gate = second_gpu.get_future().share();
    std::promise<void> finish;
    auto finished = finish.get_future();
    state.request_queue.push(FenceWaitRequest{});
    state.request_queue.push(CallbackRequest{new CallbackRequestFunction([&] { finish.set_value(); })});
    assert(finished.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
    second_gpu.set_value();
    finished.get();
    state.request_queue.abort(); worker.join();
    // An aborted queue may never dispatch this copy. Retirement must return,
    // and a worker that already popped it must not touch the recycled surface.
    ColorSurfaceCacheInfo abandoned;
    abandoned.pending_readback = std::make_shared<SurfaceReadback>();
    const auto delayed = abandoned.pending_readback;
    state.surface_cache.destroy_surface(abandoned);
    bool copied_after_cancel = false;
    delayed->complete([&] { copied_after_cancel = true; });
    assert(!copied_after_cancel);
    // The same shutdown must wake a renderer waiting to reuse a frame slot.
    context.last_frame_waited = 0;
    state.frames[2].rendered_fences.push_back(vk::Fence{});
    new_frame(context);
    assert(state.device.resets == 1);
    std::cout << "Surface retirement, CPU readback, notification/frame completion and finish ordering passed\n";
'''
code += '}\n'
with tempfile.TemporaryDirectory() as tmp:
    cpp = pathlib.Path(tmp) / 'lifetime.cpp'
    exe = pathlib.Path(tmp) / 'lifetime'
    cpp.write_text(code)
    subprocess.run([sys.argv[1], '-std=c++20', '-pthread', '-Wall', '-Wextra',
                    '-I' + str(repo / 'vita3k/vkutil/include'),
                    '-I' + str(repo / 'vita3k/renderer/include'),
                    '-I' + str(repo / 'vita3k/util/include'), '-I' + sys.argv[2],
                    str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
