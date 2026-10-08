#include <atomic>
#include <cpu/automatic_jit.h>
#include <cpu/slot_pool.h>
#include <cstdlib>
#include <thread>
#include <vector>

static void require(bool ok) {
    if (!ok)
        std::abort();
}

int main() {
    constexpr uint64_t mib = 1024 * 1024;
    // Missing OS estimates, constrained phones, A11 and larger devices.
    for (unsigned cores : { 0, 1, 2, 4, 6, 8, 16, 128 }) {
        for (uint64_t headroom : { 0ULL, 64ULL, 512ULL, 768ULL, 2048ULL, 65536ULL }) {
            const auto budget = cpu::automatic_jit_budget(cores, headroom * mib);
            require(budget.slots >= 2 && budget.slots <= 4);
            require(budget.slots * budget.cache_mb <= 64);
        }
    }
    require(cpu::automatic_jit_budget(6, 2048 * mib).slots == 2);
    require(cpu::automatic_jit_budget(8, 512 * mib).slots == 2);
    require(cpu::automatic_jit_budget(12, 2048 * mib).slots == 3);
    require(cpu::automatic_jit_budget(8, 2048 * mib).cache_mb == 12);
    require(cpu::automatic_jit_budget(8, 512 * mib).cache_mb == 8);
    // More guest threads than physical slots must progress without exceeding
    // the automatic cap. Leases end before a guest could wait on a syscall.
    auto budget = cpu::automatic_jit_budget(6, 2048 * mib);
    cpu::SlotPool pool(budget.slots);
    std::atomic_bool cancelled{ false };
    std::atomic_int active{ 0 }, completed{ 0 };
    std::vector<std::thread> threads;
    for (int guest = 0; guest < 37; ++guest) {
        threads.emplace_back([&] {
            for (int n = 0; n < 100; ++n) {
                auto slot = pool.acquire(cancelled);
                require(slot.has_value());
                require(++active <= static_cast<int>(budget.slots));
                std::this_thread::yield();
                --active;
                pool.release(*slot);
                ++completed;
            }
        });
    }
    for (auto &thread : threads)
        thread.join();
    require(completed == 3700 && active == 0);
}
