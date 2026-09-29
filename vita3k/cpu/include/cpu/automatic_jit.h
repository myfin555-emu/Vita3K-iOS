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
    const auto cpu_slots = std::clamp<std::size_t>(host_cores / 2, 2, 4);
    // Spend at most 1/16 of launch headroom on translated code (32-64 MiB).
    // Zero means the OS could not provide an estimate; use the minimum budget.
    const auto memory_slots = std::clamp<uint64_t>(available_bytes / (256ULL * 1024 * 1024), 2, 4);
    return { std::min<std::size_t>(cpu_slots, memory_slots), 16 };
}
} // namespace cpu
