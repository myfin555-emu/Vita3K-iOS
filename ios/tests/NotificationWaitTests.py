"""Run the production notification wait with accelerated host timeout slices."""
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
source = (repo / 'vita3k/modules/SceGxm/SceGxm.cpp').read_text()
signature = 'EXPORT(int, sceGxmNotificationWait, const SceGxmNotification *notification)'
start = source.index(signature)
brace = source.index('{', start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
body = source[start:end].replace(signature, 'int notification_wait(const SceGxmNotification *notification)')
code = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <thread>
#define TRACY_FUNC(...) ((void)0)
#define RET_ERROR(error) (error)
#define LOG_WARN(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO(...) ((void)0)
constexpr int SCE_GXM_ERROR_INVALID_POINTER = -1;
using namespace std::chrono_literals;
struct Address {
    uint32_t *pointer = nullptr;
    explicit operator bool() const { return pointer != nullptr; }
    uint32_t *get(int) const { return pointer; }
};
struct SceGxmNotification { Address address; uint32_t value; };
struct Condition {
    std::condition_variable ready;
    std::atomic<int> slices{0};
    template <class Predicate>
    bool wait_for(std::unique_lock<std::mutex> &lock, std::chrono::seconds, Predicate predicate) {
        ++slices;
        return ready.wait_for(lock, 1ms, predicate);
    }
};
struct Renderer { std::mutex notification_mutex; Condition notification_ready; } renderer;
struct Env {
    int mem = 0;
    Renderer *renderer = &::renderer;
    struct { std::atomic<bool> abort{false}; } display;
} emuenv;
'''
code += body
code += r'''
int main() {
    assert(notification_wait(nullptr) == SCE_GXM_ERROR_INVALID_POINTER);
    SceGxmNotification invalid{};
    assert(notification_wait(&invalid) == SCE_GXM_ERROR_INVALID_POINTER);
    uint32_t completion = 0;
    SceGxmNotification notification{{&completion}, 1};
    auto waiting = std::async(std::launch::async, [&] { return notification_wait(&notification); });
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (renderer.notification_ready.slices < 12 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    assert(renderer.notification_ready.slices >= 12);
    // Ten expired slices must not release unsignalled guest resources.
    assert(waiting.wait_for(5ms) == std::future_status::timeout);
    {
        std::lock_guard lock(renderer.notification_mutex);
        completion = notification.value;
    }
    renderer.notification_ready.ready.notify_all();
    assert(waiting.wait_for(2s) == std::future_status::ready && waiting.get() == 0);
    assert(notification_wait(&notification) == 0); // Already completed.
    completion = 0;
    renderer.notification_ready.slices = 0;
    auto stopping = std::async(std::launch::async, [&] { return notification_wait(&notification); });
    const auto abort_deadline = std::chrono::steady_clock::now() + 2s;
    while (renderer.notification_ready.slices == 0 && std::chrono::steady_clock::now() < abort_deadline)
        std::this_thread::yield();
    assert(renderer.notification_ready.slices > 0);
    emuenv.display.abort = true;
    renderer.notification_ready.ready.notify_all();
    assert(stopping.wait_for(2s) == std::future_status::ready && stopping.get() == 0);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.cpp').write_text(code)
    for defines in ([], ['-DVITA3K_PLATFORM_IOS']):
        subprocess.run([sys.argv[1], '-std=c++17', '-pthread', '-UNDEBUG', *defines,
                        str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True, timeout=5)
print('Production notification completion and shutdown checks passed')
