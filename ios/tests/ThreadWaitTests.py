"""Exercise production module/sync wait bodies without booting a guest CPU."""
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]


def function(path, signature):
    source = (repo / path).read_text()
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


source = r'''
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <iostream>
// Enforce POSIX's concurrent-mutex binding rule on Linux as well as Darwin.
struct CheckedCondition {
    std::condition_variable cv;
    std::mutex guard;
    std::mutex *bound = nullptr;
    int active = 0;
    void enter(std::unique_lock<std::mutex> &lock) {
        std::lock_guard g(guard);
        if (active && bound != lock.mutex()) {
            std::cerr << "condition variable used concurrently with different mutexes\n";
            std::exit(2);
        }
        bound = lock.mutex(); ++active;
    }
    void leave() { std::lock_guard g(guard); --active; }
    int waiters() { std::lock_guard g(guard); return active; }
    template<class P> void wait(std::unique_lock<std::mutex> &lock, P pred) {
        enter(lock); cv.wait(lock, pred); leave();
    }
    template<class D, class P> bool wait_for(std::unique_lock<std::mutex> &lock, D time, P pred) {
        enter(lock); bool ready = cv.wait_for(lock, time, pred); leave(); return ready;
    }
    void notify_all() { cv.notify_all(); }
};
using Address = uint32_t;
using SceSize = uint32_t;
using SceUInt = uint32_t;
template<class T> struct Ptr {};
enum class ThreadStatus { run, wait, dormant };
struct KernelState {};
struct WaitingThreadData {};
template<class T> using ThreadDataQueueInterator = int;
struct Queue { int erased = 0; void erase(int) { ++erased; } };
using WaitingThreadQueuePtr = std::shared_ptr<Queue>;
constexpr int SCE_KERNEL_OK = 0;
constexpr int SCE_KERNEL_ERROR_WAIT_TIMEOUT = -1;
#define RET_ERROR(x) (x)
struct ThreadState {
    std::mutex mutex;
    CheckedCondition status_cond, primitive_cond;
    ThreadStatus status = ThreadStatus::dormant;
    Address entry_point = 7;
    uint32_t returned_value = 42;
    bool delete_requested = false;
    void raise_waiting_threads() {}
    int start(SceSize, Ptr<void>) {
        std::lock_guard lock(mutex);
        update_status(ThreadStatus::run);
        return 0;
    }
    void update_status(ThreadStatus, std::optional<ThreadStatus> = std::nullopt);
    uint32_t run_guest_function(Address, SceSize, Ptr<void>);
};
using ThreadStatePtr = std::shared_ptr<ThreadState>;
'''
source += function('vita3k/kernel/src/thread.cpp', 'void ThreadState::update_status(')
source += function('vita3k/kernel/src/thread.cpp', 'uint32_t ThreadState::run_guest_function(')
source += function('vita3k/kernel/src/sync_primitives.cpp', 'inline static int handle_timeout(')
source += r'''
void await_waiter(CheckedCondition &cond) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!cond.waiters()) {
        if (std::chrono::steady_clock::now() > deadline) std::exit(3);
        std::this_thread::yield();
    }
}
int main() {
    KernelState kernel;
    for (int n = 0; n < 30; ++n) {
        auto thread = std::make_shared<ThreadState>();
        auto queue = std::make_shared<Queue>();
        std::mutex primitive;
        auto caller = std::async(std::launch::async, [&] { return thread->run_guest_function(99, 0, {}); });
        await_waiter(thread->status_cond);
        auto guest = std::async(std::launch::async, [&] {
            std::unique_lock primitive_lock(primitive);
            std::unique_lock thread_lock(thread->mutex);
            thread->update_status(ThreadStatus::wait);
            thread_lock.unlock();
            SceUInt timeout = 1000000;
            auto result = handle_timeout(kernel, thread, thread_lock, primitive_lock, queue, 0, "test", n % 2 ? &timeout : nullptr);
            primitive_lock.unlock();
            thread_lock.lock();
            thread->update_status(ThreadStatus::dormant);
            return result;
        });
        await_waiter(thread->primitive_cond);
        {
            std::lock_guard primitive_lock(primitive);
            std::lock_guard thread_lock(thread->mutex);
            thread->update_status(ThreadStatus::run);
        }
        assert(guest.get() == 0);
        assert(caller.get() == 42);
        assert(thread->entry_point == 7 && queue->erased == 0);
    }
    for (SceUInt duration : {0U, 1000U}) {
        auto thread = std::make_shared<ThreadState>();
        auto queue = std::make_shared<Queue>();
        std::mutex primitive;
        std::unique_lock primitive_lock(primitive);
        std::unique_lock thread_lock(thread->mutex);
        thread->update_status(ThreadStatus::wait);
        thread_lock.unlock();
        assert(handle_timeout(kernel, thread, thread_lock, primitive_lock, queue, 0, "test", &duration) == SCE_KERNEL_ERROR_WAIT_TIMEOUT);
        assert(duration == 0 && queue->erased == 1 && thread->status == ThreadStatus::run);
    }
}
'''
with tempfile.TemporaryDirectory(prefix='vita-thread-wait-') as directory:
    path = pathlib.Path(directory)
    (path / 'test.cpp').write_text(source)
    subprocess.run([sys.argv[1], '-std=c++17', '-pthread', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=15)
