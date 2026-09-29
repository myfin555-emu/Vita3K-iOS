#include <vita3k_ios/PerformanceOptimizations.h>
#include <renderer/surface_cache.h>

#include <cstdlib>

int main() {
    renderer::SurfaceChangeTracker tracker;
    if (tracker.has_changes() || !tracker.mark_dirty(0x1000)
        || tracker.mark_dirty(0x1000) || !tracker.is_dirty(0x1000))
        return EXIT_FAILURE;
    tracker.mark_dirty_batch({0x1000, 0x2000, 0x2000});
    if (tracker.count_dirty() != 2 || tracker.is_dirty(0x3000))
        return EXIT_FAILURE;
    tracker.clear_frame();
    if (tracker.has_changes() || tracker.is_dirty(0x1000))
        return EXIT_FAILURE;

    for (const int fps : {0, -1, 1000001}) {
        try {
            vita3k_ios::FrameRateLimiter invalid(fps);
            return EXIT_FAILURE;
        } catch (const std::invalid_argument &) {
        }
    }
    vita3k_ios::FrameRateLimiter limiter(1000000);
    limiter.throttle();
    limiter.reset();
    return limiter.get_last_frame_time_us() >= 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
