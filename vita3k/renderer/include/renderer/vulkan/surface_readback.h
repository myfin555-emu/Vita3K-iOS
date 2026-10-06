// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <chrono>
#include <future>
#include <mutex>

namespace renderer::vulkan {

// The renderer owns the surface; the wait worker owns a queued CPU copy.
// Cancellation must exclude an already-running copy before surface recycling.
class SurfaceReadback {
    std::mutex mutex;
    bool cancelled = false;
    std::promise<void> completion;
    std::future<void> finished = completion.get_future();

public:
    /** Non-blocking: true once complete() has run (or was cancelled). */
    bool ready() const {
        return finished.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    template <typename IsAborted>
    void wait(IsAborted is_aborted) {
        while (finished.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready) {
            if (is_aborted()) {
                std::lock_guard lock(mutex);
                cancelled = true;
                return;
            }
        }
    }

    template <typename Copy>
    void complete(Copy copy) {
        std::lock_guard lock(mutex);
        if (!cancelled)
            copy();
        completion.set_value();
    }
};

} // namespace renderer::vulkan
