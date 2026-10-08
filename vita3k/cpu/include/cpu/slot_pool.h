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
    std::vector<bool> busy;
    struct Waiter {
        std::condition_variable changed;
    };
    std::deque<Waiter *> waiting;

    std::optional<std::size_t> free_slot(std::optional<std::size_t> preferred) const {
        if (preferred && *preferred < busy.size() && !busy[*preferred])
            return preferred;
        const auto it = std::find(busy.begin(), busy.end(), false);
        if (it == busy.end())
            return std::nullopt;
        return static_cast<std::size_t>(it - busy.begin());
    }

    // Notify while holding mutex: waiters live on acquire()'s stack and must
    // not leave the queue before their condition variable has been signalled.
    void notify_next() {
        if (!waiting.empty() && free_slot(std::nullopt))
            waiting.front()->changed.notify_one();
    }

public:
    explicit SlotPool(std::size_t count)
        : busy(count, false) {
        if (!count)
            throw std::invalid_argument("JIT slot count must be positive");
    }

    std::optional<std::size_t> acquire(const std::atomic_bool &cancelled,
        std::optional<std::size_t> preferred = std::nullopt) {
        std::unique_lock lock(mutex);
        if (cancelled.load())
            return std::nullopt;
        // Most syscall returns encounter no contention. Avoid queue allocation
        // and any wakeups in that case, without overtaking queued guests.
        if (waiting.empty()) {
            if (const auto index = free_slot(preferred)) {
                busy[*index] = true;
                return index;
            }
        }
        Waiter waiter;
        waiting.push_back(&waiter);
        waiter.changed.wait(lock, [&] {
            return cancelled.load() || (waiting.front() == &waiter && free_slot(preferred));
        });
        waiting.erase(std::find(waiting.begin(), waiting.end(), &waiter));
        if (cancelled.load()) {
            notify_next();
            return std::nullopt;
        }
        const auto index = free_slot(preferred);
        busy[*index] = true;
        notify_next();
        return index;
    }

    void release(std::size_t index) {
        std::lock_guard lock(mutex);
        busy.at(index) = false;
        notify_next();
    }

    void wake() {
        // Pair with the wait mutex to avoid a lost cancellation notification.
        std::lock_guard lock(mutex);
        for (auto *waiter : waiting)
            waiter->changed.notify_one();
    }
};
} // namespace cpu
