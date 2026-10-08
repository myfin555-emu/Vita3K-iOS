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
constexpr bool should_release_staging_buffer(uint64_t bytes, uint64_t last_used, uint64_t current, bool memory_pressure = false) {
    // Pressure can release warm scratch, but never one of the in-flight frames.
    const uint64_t minimum_age = memory_pressure ? MAX_FRAMES_RENDERING : 120;
    const uint64_t minimum_size = memory_pressure ? 0 : 1024 * 1024;
    return bytes > minimum_size && last_used != ~uint64_t{ 0 }
    && last_used <= current && current - last_used >= minimum_age;
}

} // namespace renderer::vulkan
