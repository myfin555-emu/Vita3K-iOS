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
    // iOS already has renderer/audio/UI threads competing for host CPU time.
    // Two or three persistent JIT workers are enough for Vita titles while
    // avoiding one translated-code cache per host core group.
    const auto cpu_slots = std::clamp<std::size_t>(host_cores / 3, 2, 3);
    // Keep translated code within a small resident-memory budget. Dynarmic's
    // ARM64 cache has an ~8 MiB minimum, so use 8 MiB under tighter headroom
    // and 12 MiB when memory pressure is lower.
    const auto memory_slots = std::clamp<uint64_t>(available_bytes / (384ULL * 1024 * 1024), 2, 3);
    const std::size_t cache_mb = available_bytes >= 1024ULL * 1024 * 1024 ? 12 : 8;
    return { std::min<std::size_t>(cpu_slots, memory_slots), cache_mb };
}
} // namespace cpu
