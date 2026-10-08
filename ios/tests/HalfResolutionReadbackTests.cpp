// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <renderer/half_resolution_readback.h>

#include <cstdlib>
#include <vector>

static void require(bool condition) {
    if (!condition)
        std::abort();
}

int main() {
    using namespace renderer;
    require(!can_expand_half_resolution(0, 1, 0, 2, 4));
    require(!can_expand_half_resolution(2, 2, 5, 4, 4));
    require(!can_expand_half_resolution(2, 2, 4, 4, 3));
    require(!can_expand_half_resolution(UINT32_MAX, 1, UINT32_MAX - 1, 2, 4));
    for (uint32_t bytes : { 1, 2, 4, 8 }) {
        for (uint32_t width : { 1, 3, 480 }) {
            const uint32_t height = 7;
            require(can_expand_half_resolution(width, height, width * 2, height * 2, bytes));
            const size_t stride = width * 2 * bytes + 17;
            std::vector<uint8_t> src(width * height * bytes);
            for (size_t i = 0; i < src.size(); ++i)
                src[i] = static_cast<uint8_t>(i * 37);
            std::vector<uint8_t> dst(stride * height * 2 + 32, 0xA5);
            expand_half_resolution(dst.data() + 16, stride, src.data(), width, height, bytes);
            for (uint32_t y = 0; y < height * 2; ++y) {
                for (uint32_t x = 0; x < width * 2; ++x)
                    for (uint32_t b = 0; b < bytes; ++b)
                        require(dst[16 + y * stride + x * bytes + b] == src[((y / 2) * width + x / 2) * bytes + b]);
                for (size_t p = width * 2 * bytes; p < stride; ++p)
                    require(dst[16 + y * stride + p] == 0xA5);
            }
            for (size_t i = 0; i < 16; ++i)
                require(dst[i] == 0xA5 && dst[dst.size() - 1 - i] == 0xA5);
        }
    }
}
