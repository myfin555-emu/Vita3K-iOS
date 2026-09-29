// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <mutex>

namespace ctrl {
// Current UIKit state, independent of SDL's deferred virtual-joystick update.
// One short lock gives a consistent snapshot without holding an SDL lock.
class VirtualPad {
public:
    struct Snapshot {
        uint32_t id = 0;
        uint32_t buttons = 0;
        std::array<int16_t, 6> axes{ 0, 0, 0, 0, INT16_MIN, INT16_MIN };
        bool button(int index) const {
            return index >= 0 && index < 32 && (buttons & (uint32_t{ 1 } << index));
        }
        int16_t axis(int index) const {
            return index >= 0 && index < static_cast<int>(axes.size()) ? axes[index] : 0;
        }
    };

    void attach(uint32_t id) {
        const std::lock_guard lock(mutex);
        state = {};
        state.id = id;
    }
    void button(int index, bool pressed) {
        const std::lock_guard lock(mutex);
        if (!state.id || index < 0 || index >= 32)
            return;
        const auto bit = uint32_t{ 1 } << index;
        state.buttons = pressed ? state.buttons | bit : state.buttons & ~bit;
    }
    void axis(int index, int16_t value) {
        const std::lock_guard lock(mutex);
        if (state.id && index >= 0 && index < static_cast<int>(state.axes.size()))
            state.axes[index] = value;
    }
    void release_all() {
        const std::lock_guard lock(mutex);
        const auto id = state.id;
        state = {};
        state.id = id;
    }
    Snapshot snapshot() {
        const std::lock_guard lock(mutex);
        return state;
    }

private:
    std::mutex mutex;
    Snapshot state;
};

inline VirtualPad virtual_pad;
} // namespace ctrl
