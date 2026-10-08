// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <display/vblank_clock.h>

#include <cstdlib>
#include <initializer_list>

static void require(bool value) {
    if (!value)
        std::abort();
}

int main() {
    using namespace std::chrono;
    const steady_clock::time_point start{};
    const auto first = display::next_vblank_deadline(start, start);
    const auto period = first - start;
    require(period > milliseconds(16) && period < milliseconds(17));
    auto deadline = start;
    for (int frame = 1; frame <= 600; ++frame) {
        // Simulate callback work plus variable scheduler wakeup latency.
        deadline = display::next_vblank_deadline(deadline, deadline + microseconds(frame % 3000));
        require(deadline == start + period * frame);
    }
    require(duration_cast<microseconds>(deadline - start).count() >= 9999999);
    // Stalls/resume never request a past deadline or a catch-up spin loop.
    for (const auto delay : {period, period * 2, period * 10000}) {
        const auto now = first + delay;
        const auto next = display::next_vblank_deadline(first, now);
        require(next > now && next - now <= period);
        require((next - start) % period == steady_clock::duration::zero());
    }
}
