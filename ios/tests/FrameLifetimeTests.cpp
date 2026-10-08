// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include <renderer/vulkan/frame_lifetime.h>

int main() {
    using renderer::vulkan::is_frame_timestamp_in_flight;
    using renderer::vulkan::should_release_staging_buffer;
    // Exhaust startup and steady-state boundaries, including unused/future slots.
    for (uint64_t current = 0; current != 20; ++current) {
        for (uint64_t timestamp = 0; timestamp != 25; ++timestamp) {
            const bool retired = timestamp <= current && current - timestamp >= 3;
            if (should_release_staging_buffer(512 * 1024, timestamp, current, true) != retired)
                return 1;
            if (should_release_staging_buffer(0, timestamp, current, true))
                return 1;
            const bool expected = timestamp <= current && timestamp + 3 > current;
            if (is_frame_timestamp_in_flight(timestamp, current) != expected) {
                std::cerr << "incorrect staging lifetime " << timestamp << ' ' << current << '\n';
                return 1;
            }
        }
        if (should_release_staging_buffer(8 * 1024 * 1024, ~uint64_t{ 0 }, current, true))
            return 1;
        if (is_frame_timestamp_in_flight(~uint64_t{ 0 }, current))
            return 1;
    }
    if (!is_frame_timestamp_in_flight(~uint64_t{ 0 } - 1, ~uint64_t{ 0 } - 1))
        return 1;
}
