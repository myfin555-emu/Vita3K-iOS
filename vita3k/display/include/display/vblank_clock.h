// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <chrono>

namespace display {

// Use a monotonic 60 Hz timeline. A late wakeup skips expired deadlines rather
// than producing a burst of vblank callbacks or shifting every later frame.
inline std::chrono::steady_clock::time_point next_vblank_deadline(
    std::chrono::steady_clock::time_point previous,
    std::chrono::steady_clock::time_point now) {
    constexpr auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<int, std::ratio<1, 60>>(1));
    auto next = previous + period;
    if (next <= now)
        next += period * ((now - next) / period + 1);
    return next;
}

} // namespace display
