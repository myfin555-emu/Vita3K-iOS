// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace renderer::vulkan {

constexpr int MAX_FRAMES_RENDERING = 3;

// Subtract only after checking ordering: startup frames are smaller than the window.
constexpr bool is_frame_timestamp_in_flight(uint64_t timestamp, uint64_t current) {
    return timestamp != ~uint64_t{ 0 } && timestamp <= current
        && current - timestamp < MAX_FRAMES_RENDERING;
}

// Avoid churn for normal uploads. At 30 FPS, 120 idle frames is four seconds.
constexpr bool should_release_staging_buffer(uint64_t bytes, uint64_t last_used, uint64_t current) {
    return bytes > 1024 * 1024 && last_used != ~uint64_t{ 0 }
    && last_used <= current && current - last_used >= 120;
}

} // namespace renderer::vulkan
