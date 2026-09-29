#include <atomic>
#include <cstdlib>
#include <ctrl/virtual_pad.h>
#include <thread>

static void require(bool ok) {
    if (!ok)
        std::abort();
}
int main() {
    ctrl::VirtualPad pad;
    pad.axis(1, 123);
    require(pad.snapshot().id == 0 && pad.snapshot().axis(1) == 0);
    pad.attach(42);
    require(pad.snapshot().axis(4) == INT16_MIN);
    // Direction reversals and release must be observable without any SDL pump.
    pad.axis(1, INT16_MIN);
    require(pad.snapshot().axis(1) == INT16_MIN);
    pad.axis(1, INT16_MAX);
    pad.button(0, true);
    pad.button(1, true);
    require(pad.snapshot().axis(1) == INT16_MAX);
    require(pad.snapshot().button(0) && pad.snapshot().button(1));
    pad.button(0, false);
    require(!pad.snapshot().button(0) && pad.snapshot().button(1));
    pad.release_all();
    auto neutral = pad.snapshot();
    require(neutral.id == 42 && !neutral.buttons && neutral.axis(1) == 0 && neutral.axis(5) == INT16_MIN);
    std::atomic_bool done{ false };
    std::thread writer([&] {
        for (int n = 0; n < 10000; ++n) {
            pad.attach(42);
            pad.axis(1, INT16_MAX);
            pad.attach(0);
        }
        done.store(true);
    });
    while (!done.load()) {
        const auto sample = pad.snapshot();
        require(sample.id == 0 || sample.id == 42);
        if (!sample.id)
            require(sample.axis(1) == 0 && sample.buttons == 0);
    }
    writer.join();
    pad.button(-1, true);
    pad.button(32, true);
    pad.axis(6, 123);
    require(!pad.snapshot().buttons);
}
