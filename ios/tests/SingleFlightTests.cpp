// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <renderer/single_flight.h>

#include <atomic>
#include <cassert>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

int main() {
    renderer::SingleFlight<int> flights;
    // Same-key producers share one published result, even under contention.
    int result = 0;
    std::atomic<int> producers{ 0 };
    std::vector<std::thread> workers;
    for (int i = 0; i < 24; ++i) {
        workers.emplace_back([&] {
            auto lease = flights.acquire(7);
            if (!result) {
                ++producers;
                std::this_thread::yield();
                result = 42;
            }
            assert(result == 42);
        });
    }
    for (auto &worker : workers)
        worker.join();
    assert(producers == 1);

    // Holding one key must not serialize an unrelated shader.
    {
        auto lease = flights.acquire(7);
        auto unrelated = std::async(std::launch::async, [&] {
            auto other = flights.acquire(8);
            return true;
        });
        assert(unrelated.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        assert(unrelated.get());
    }
    // An exception in generation must let a later caller retry.
    try {
        auto lease = flights.acquire(7);
        throw std::runtime_error("compile failed");
    } catch (const std::runtime_error &) {
    }
    auto retry = std::async(std::launch::async, [&] {
        auto lease = flights.acquire(7);
        return true;
    });
    assert(retry.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(retry.get());
}
