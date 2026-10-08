// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace cpu {
struct AutomaticJitBudget {
    std::size_t slots;
    std::size_t cache_mb;
};

// Reserve execution capacity for UIKit, audio and the renderer. Guest threads
// borrow these workers only while executing, and allocate translations lazily.
// Never multiply caches by the number of threads created by the game.
constexpr AutomaticJitBudget automatic_jit_budget(unsigned host_cores, uint64_t available_bytes) {
    // Keep the original throughput-oriented worker budget. The shared workers
    // already bound executable memory, while fewer workers can make a title
    // repeatedly translate the same hot Vita code on different caches.
    const auto cpu_slots = std::clamp<std::size_t>(host_cores / 2, 2, 4);
    const auto memory_slots = std::clamp<uint64_t>(available_bytes / (256ULL * 1024 * 1024), 2, 4);
    // 16 MiB is the safe baseline for keeping hot game code resident without
    // letting the pool grow with the number of guest threads. Do not trade
    // translation locality for a small RAM saving on performance-oriented iOS.
    return { std::min<std::size_t>(cpu_slots, memory_slots), 16 };
}
} // namespace cpu
