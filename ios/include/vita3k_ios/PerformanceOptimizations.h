#pragma once

#include <cstdint>
#include <stdexcept>
#include <thread>
#include <chrono>

namespace vita3k_ios {

/**
 * @brief Frame rate limiter for stable 60 FPS emulation
 *
 * Critical for games like God Eater Resurrection that are sensitive to frame timing.
 * MoltenVK can have variable frame times; this ensures consistency.
 */
class FrameRateLimiter {
public:
    explicit FrameRateLimiter(int target_fps = 60)
        : target_fps_(target_fps),
          frame_budget_us_(frame_budget(target_fps)),
          last_frame_time_(std::chrono::steady_clock::now()) {
    }

    /**
     * @brief Throttle to maintain target FPS
     * Should be called at end of each emulation frame
     */
    void throttle() {
        auto now = std::chrono::steady_clock::now();
        auto frame_time = std::chrono::duration_cast<std::chrono::microseconds>(
            now - last_frame_time_
        ).count();

        if (frame_time < frame_budget_us_) {
            auto sleep_time = frame_budget_us_ - frame_time;
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_time));
        }

        last_frame_time_ = std::chrono::steady_clock::now();
    }

    /**
     * @brief Get elapsed time since the last throttle/reset (in microseconds)
     */
    int64_t get_last_frame_time_us() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
            now - last_frame_time_
        ).count();
    }

    /**
     * @brief Reset frame timing (call on pause/resume)
     */
    void reset() {
        last_frame_time_ = std::chrono::steady_clock::now();
    }

private:
    static int64_t frame_budget(int target_fps) {
        if (target_fps <= 0 || target_fps > 1000000)
            throw std::invalid_argument("target_fps must be between 1 and 1000000");
        return 1000000 / target_fps;
    }

    int target_fps_;
    int64_t frame_budget_us_;
    std::chrono::steady_clock::time_point last_frame_time_;
};

/**
 * @brief Memory usage monitor for iOS constraints
 *
 * iOS has strict memory limits. This helps detect when emulation
 * is approaching jetsam threshold.
 */
class MemoryMonitor {
public:
    /**
     * @brief Get current memory pressure level
     * @return 0-100, where 100 is critical
     */
    static int get_memory_pressure();

    /**
     * @brief Get total resident set size in MB
     */
    static uint64_t get_rss_mb();

    /**
     * @brief Check if approaching jetsam threshold for current device
     * @return true if memory usage > 85% of available
     */
    static bool is_memory_critical();

    /**
     * @brief Request garbage collection from emulator
     * Called when memory pressure is high
     */
    static void request_gc();
};

/**
 * @brief Thermal monitoring for iOS devices
 *
 * Prevents thermal throttling by scaling emulation quality when device gets hot.
 */
class ThermalThrottleManager {
public:
    enum class ThermalState : int {
        NOMINAL = 0,      // Device is cool, full performance
        MODERATE = 1,     // Device warming up, reduce settings slightly
        CRITICAL = 2,     // Device very hot, reduce settings aggressively
    };

    /**
     * @brief Get current device thermal state
     */
    static ThermalState get_thermal_state();

    /**
     * @brief Apply thermal throttling adjustments to config
     * Automatically reduces resolution multiplier, disables high accuracy, etc.
     *
     * @param current_thermal Previous thermal state
     * @param new_thermal Current thermal state
     */
    static void apply_thermal_scaling(ThermalState current_thermal, ThermalState new_thermal);
};

} // namespace vita3k_ios
