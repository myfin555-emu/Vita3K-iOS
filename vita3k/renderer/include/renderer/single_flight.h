// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <condition_variable>
#include <mutex>
#include <set>

namespace renderer {

// Let unrelated shaders compile concurrently, but sleep while another worker
// produces the same shader. A failed producer also releases its waiters.
template <typename Key>
class SingleFlight {
    std::mutex mutex;
    std::condition_variable ready;
    std::set<Key> active;

public:
    class Lease {
        SingleFlight &owner;
        Key key;

    public:
        Lease(SingleFlight &owner, const Key &key)
            : owner(owner)
            , key(key) {
            std::unique_lock lock(owner.mutex);
            owner.ready.wait(lock, [&] { return owner.active.count(key) == 0; });
            owner.active.insert(key);
        }
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        ~Lease() {
            {
                std::lock_guard lock(owner.mutex);
                owner.active.erase(key);
            }
            owner.ready.notify_all();
        }
    };

    Lease acquire(const Key &key) { return Lease(*this, key); }
};

} // namespace renderer
