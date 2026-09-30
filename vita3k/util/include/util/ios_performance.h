// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#ifdef VITA3K_PLATFORM_IOS
#include <util/log.h>

#include <pthread.h>
#include <pthread/qos.h>

#include <algorithm>
#include <mutex>
#include <vector>

namespace util {

// A public iOS scheduling request, not Adreno's GPU power-control switch.
// Register only dedicated emulation workers. The registry lock serializes
// toggles with thread exit so no override can target a recycled pthread.
class IOSPerformanceThread;
struct IOSPerformanceRegistry {
    std::mutex mutex;
    bool enabled = false;
    std::vector<IOSPerformanceThread *> threads;
};
inline IOSPerformanceRegistry ios_performance_registry;

class IOSPerformanceThread {
    const qos_class_t requested_qos;
    pthread_t thread = pthread_self();
    pthread_override_t override = nullptr;

    void apply(bool enabled) {
        if (enabled && !override) {
            override = pthread_override_qos_class_start_np(thread, requested_qos, 0);
            if (!override)
                LOG_WARN("iOS Turbo: worker scheduling request was rejected");
        } else if (!enabled && override) {
            const int result = pthread_override_qos_class_end_np(override);
            if (result != 0)
                LOG_WARN("iOS Turbo: could not end worker scheduling override: {}", result);
            else
                override = nullptr;
        }
    }

    friend void set_ios_performance_mode(bool enabled);

public:
    explicit IOSPerformanceThread(qos_class_t qos = QOS_CLASS_USER_INTERACTIVE)
        : requested_qos(qos) {
        auto &registry = ios_performance_registry;
        std::lock_guard lock(registry.mutex);
        registry.threads.push_back(this);
        apply(registry.enabled);
    }
    IOSPerformanceThread(const IOSPerformanceThread &) = delete;
    IOSPerformanceThread &operator=(const IOSPerformanceThread &) = delete;
    ~IOSPerformanceThread() {
        auto &registry = ios_performance_registry;
        std::lock_guard lock(registry.mutex);
        apply(false);
        auto &threads = registry.threads;
        threads.erase(std::find(threads.begin(), threads.end(), this));
    }
};

inline void set_ios_performance_mode(bool enabled) {
    auto &registry = ios_performance_registry;
    std::lock_guard lock(registry.mutex);
    registry.enabled = enabled;
    for (auto *thread : registry.threads)
        thread->apply(enabled);
}

} // namespace util
#endif
