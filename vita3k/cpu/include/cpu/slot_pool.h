// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

namespace cpu {
// Fair admission to bounded execution slots. Waiting guest threads hold no JIT
// cache. The lease must end before dispatching a guest syscall that can block.
class SlotPool {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<bool> busy;
    std::deque<uint64_t> waiting;
    uint64_t next_ticket = 0;

public:
    explicit SlotPool(std::size_t count)
        : busy(count, false) {
        if (!count)
            throw std::invalid_argument("JIT slot count must be positive");
    }

    std::optional<std::size_t> acquire(const std::atomic_bool &cancelled, std::optional<std::size_t> preferred = std::nullopt) {
        std::unique_lock lock(mutex);
        const auto ticket = next_ticket++;
        waiting.push_back(ticket);
        changed.wait(lock, [&] {
            return cancelled.load() || (waiting.front() == ticket && std::find(busy.begin(), busy.end(), false) != busy.end());
        });
        waiting.erase(std::find(waiting.begin(), waiting.end(), ticket));
        if (cancelled.load()) {
            changed.notify_all();
            return std::nullopt;
        }

        // Keep a guest thread on the same worker whenever possible. Dynarmic's
        // translated blocks live in the worker's code cache, so stable affinity
        // avoids recompiling the same Vita hot path on another worker after a
        // scheduler wake-up. If the preferred slot is busy, fall back to any
        // free slot so parallel guest threads still make progress.
        std::size_t index = busy.size();
        if (preferred && *preferred < busy.size() && !busy[*preferred])
            index = *preferred;
        else
            index = std::find(busy.begin(), busy.end(), false) - busy.begin();
        busy[index] = true;
        changed.notify_all();
        return index;
    }

    void release(std::size_t index) {
        std::lock_guard lock(mutex);
        busy.at(index) = false;
        changed.notify_all();
    }

    void wake() {
        // Pair with the wait mutex to avoid a lost cancellation notification.
        std::lock_guard lock(mutex);
        changed.notify_all();
    }
};
} // namespace cpu
