"""Exercise production pipeline scheduling/publication with a controlled compiler."""
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

pipeline = source('vita3k/renderer/src/vulkan/pipeline_cache.cpp')
scene = source('vita3k/renderer/src/vulkan/scene.cpp')
code = r'''
#include <vkutil/vulkan.h>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <thread>
using namespace std::chrono_literals;
struct MemState {};
enum SceGxmPrimitiveType : uint32_t { Triangles };
struct SceGxmProgram { bool is_frag_color_used() const { return false; } };
struct VKFragmentProgram { uint64_t blending_hash = 0; };
template <typename T> struct Ptr { T *p; T *get() const {return p;} T *get(MemState &) const { return p; } };
struct SceGxmVertexProgram { uint64_t key_hash = 0; int attributes = 0; std::atomic<int> compile_threads_on{0}; };
struct SceGxmFragmentProgram {
    Ptr<SceGxmProgram> program;
    Ptr<VKFragmentProgram> renderer_data;
    std::atomic<int> compile_threads_on{0};
};
namespace shader { struct Hints { int color_format; const int *attributes; }; }
struct GxmRecordState {
    uint64_t serial = 0;
    Ptr<SceGxmFragmentProgram> fragment_program;
    Ptr<SceGxmVertexProgram> vertex_program;
    struct { int colorFormat = 0; } color_surface;
};
constexpr size_t record_pipeline_len = sizeof(GxmRecordState);
uint64_t XXH3_64bits(const void *record, size_t) { return static_cast<const GxmRecordState *>(record)->serial; }
struct State {
    struct { bool support_shader_interlock = false; } features;
    std::atomic<int> shaders_count_compiled{0};
};
struct VKContext {
    GxmRecordState record;
    vk::RenderPass current_render_pass, current_shader_interlock_pass;
    shader::Hints shader_hints;
};
'''
code += function(pipeline, 'struct CompileRequest') + ';\n'
code += r'''
struct Queue {
    std::mutex mutex;
    std::condition_variable ready;
    std::queue<CompileRequest *> requests;
    void enqueue(int, CompileRequest *request) {
        std::lock_guard lock(mutex); requests.push(request); ready.notify_all();
    }
    void wait_dequeue(int, CompileRequest *&request) {
        std::unique_lock lock(mutex); ready.wait(lock, [&] {return !requests.empty();});
        request = requests.front(); requests.pop();
    }
};
namespace moodycamel { struct ConsumerToken { explicit ConsumerToken(Queue &) {} operator int() const {return 0;} }; }
struct PipelineCache {
    State state;
    bool use_async_compilation = true, can_use_deferred_compilation = false;
    std::mutex failed_pipelines_mutex;
    std::condition_variable pipeline_ready;
    std::set<uint64_t> failed_pipelines;
    std::map<uint64_t, vk::Pipeline> pipelines;
    Queue pipeline_compile_queue;
    int pipeline_compile_queue_token = 0;
    static constexpr int pipeline_cache_save_delay = 15;
    std::atomic<uint64_t> next_pipeline_cache_save{0};
    std::atomic<int> compiled{0};
    std::shared_future<void> gate;
    bool fail = false;
    vk::Pipeline compile_pipeline(SceGxmPrimitiveType, vk::RenderPass, SceGxmVertexProgram &, SceGxmFragmentProgram &, const GxmRecordState &record, shader::Hints, MemState &) {
        ++compiled;
        if (gate.valid()) gate.wait();
        return fail ? vk::Pipeline{} : std::bit_cast<vk::Pipeline>(record.serial);
    }
    void compiler_thread(MemState &);
    vk::Pipeline retrieve_pipeline(VKContext &, SceGxmPrimitiveType &, bool, MemState &);
};
'''
code += function(pipeline, 'void PipelineCache::compiler_thread(')
code += function(pipeline, 'vk::Pipeline PipelineCache::retrieve_pipeline(')
code += r'''
struct DrawContext {
    bool refresh_pipeline = false;
    SceGxmPrimitiveType last_primitive = Triangles;
    vk::Pipeline current_pipeline;
    struct { struct {
        int attempts = 0;
        vk::Pipeline retrieve_pipeline(DrawContext &, SceGxmPrimitiveType &, bool, MemState &) {
            ++attempts; return std::bit_cast<vk::Pipeline>(uint64_t{99});
        }
    } pipeline_cache; } state;
    struct { void bindPipeline(vk::PipelineBindPoint, vk::Pipeline) {} } render_cmd;
};
void retry_draw(DrawContext &context, SceGxmPrimitiveType type, MemState &mem) {
    int instance_count = 1, count = 6;
'''
start = scene.index('    // Resolve first')
end = scene.index('    // can happen with asynchronous', start)
code += scene[start:end] + '\n}\n'
code += r'''
int main() {
    MemState mem;
    SceGxmProgram program;
    VKFragmentProgram fragment;
    SceGxmVertexProgram vertex;
    SceGxmFragmentProgram gxm_fragment{{&program}, {&fragment}};
    VKContext context{{1, {&gxm_fragment}, {&vertex}}};
    SceGxmPrimitiveType type = Triangles;
    PipelineCache cache;
    // A first-use render target must never lose its initial draw.
    assert(cache.retrieve_pipeline(context, type, true, mem));
    assert(cache.compiled == 1 && cache.pipeline_compile_queue.requests.empty());
    cache.can_use_deferred_compilation = true;
    context.record.serial = 2;
    std::promise<void> release;
    cache.gate = release.get_future().share();
    assert(!cache.retrieve_pipeline(context, type, true, mem));
    std::thread worker([&] { cache.compiler_thread(mem); });
    assert(!cache.retrieve_pipeline(context, type, true, mem));
    // A pending job now required by a full-screen quad must be awaited.
    auto required = std::async(std::launch::async, [&] { return cache.retrieve_pipeline(context, type, false, mem); });
    assert(required.wait_for(30ms) == std::future_status::timeout);
    release.set_value();
    assert(required.wait_for(2s) == std::future_status::ready && required.get());
    cache.pipeline_compile_queue.enqueue(0, nullptr);
    worker.join();
    assert(cache.compiled == 2);
    assert(vertex.compile_threads_on == 0 && gxm_fragment.compile_threads_on == 0);
    // Failed publication must also release a waiter and must not recompile.
    context.record.serial = 3;
    cache.fail = true;
    std::promise<void> failure_release;
    cache.gate = failure_release.get_future().share();
    assert(!cache.retrieve_pipeline(context, type, true, mem));
    std::thread failed_worker([&] { cache.compiler_thread(mem); });
    cache.can_use_deferred_compilation = false;
    auto failed = std::async(std::launch::async, [&] { return cache.retrieve_pipeline(context, type, true, mem); });
    assert(failed.wait_for(30ms) == std::future_status::timeout);
    failure_release.set_value();
    assert(failed.wait_for(2s) == std::future_status::ready && !failed.get());
    cache.pipeline_compile_queue.enqueue(0, nullptr);
    failed_worker.join();
    assert(!cache.retrieve_pipeline(context, type, true, mem) && cache.compiled == 3);
    DrawContext draw;
    retry_draw(draw, type, mem);
    assert(draw.current_pipeline && draw.state.pipeline_cache.attempts == 1);
    retry_draw(draw, type, mem);
    assert(draw.state.pipeline_cache.attempts == 1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.cpp').write_text(code)
    subprocess.run([sys.argv[1], '-std=c++20', '-pthread', '-UNDEBUG', str(path / 'test.cpp'),
                    '-I' + str(repo / 'vita3k/vkutil/include'), '-I' + sys.argv[2], '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
print('Production pipeline readiness checks passed')
