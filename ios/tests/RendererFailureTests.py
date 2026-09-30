"""Exercise production render-failure publication, frontend handoff and session reset."""
import pathlib
import subprocess
import sys
import tempfile
repo = pathlib.Path(__file__).resolve().parents[2]
batch = (repo/'vita3k/renderer/src/batch.cpp').read_text()
frontend = (repo/'ios/src/UpstreamMain.cpp').read_text()
start = batch.index('} catch (const std::exception &error)')
end = batch.index('\nvoid start_render_thread', start)
catch = batch[start:end]
start = batch.index('void start_render_thread')
end = batch.index('\nvoid stop_render_thread', start)
launch = batch[start:end]
start = frontend.index('        if (emuenv->renderer->render_failed.load')
end = frontend.index('        vita3k_ios_update_keyboard', start)
consume = frontend[start:end]
code = r'''
#include <atomic>
#include <cassert>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#define LOG_ERROR(...) ((void)0)
struct DisplayState {}; struct GxmState {}; struct MemState {}; struct Config {};
struct State {
    std::atomic<bool> render_abort{false}, render_failed{false};
    std::string render_error;
    std::unique_ptr<std::thread> render_thread;
    struct { std::atomic<bool> aborted{false}; void abort() { aborted = true; } } command_buffer_queue;
    int done = 0, presented = 0;
    std::shared_future<void> release;
    void done_current() { ++done; }
};
void render_loop(State &state, DisplayState &, GxmState &, MemState &, Config &) try {
    state.release.wait();
    throw std::runtime_error("synthetic required shader failure");
    ++state.presented;
'''
code += catch + launch
code += r'''
int main() {
    State state;
    DisplayState display; GxmState gxm; MemState mem; Config config;
    for (int session = 0; session < 2; ++session) {
        std::promise<void> release;
        state.release = release.get_future().share();
        start_render_thread(state, display, gxm, mem, config);
        assert(!state.render_failed && state.render_error.empty());
        release.set_value();
        state.render_thread->join();
        assert(state.render_failed.load(std::memory_order_acquire));
        assert(state.render_abort && state.command_buffer_queue.aborted);
        assert(state.presented == 0 && state.done == session + 1);
        struct { State *renderer; } env{&state};
        auto *emuenv = &env;
        std::string rendering_error;
        while (true) {
'''
code += consume
code += r'''
            assert(false);
        }
        assert(rendering_error == "synthetic required shader failure");
    }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path/'test.cpp').write_text(code)
    subprocess.run([sys.argv[1], '-std=c++20', '-pthread', '-UNDEBUG', str(path/'test.cpp'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True, timeout=5)
print('Production render error handoff and next-session reset checks passed')
