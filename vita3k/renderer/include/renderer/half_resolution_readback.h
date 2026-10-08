// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace renderer {

constexpr bool can_expand_half_resolution(uint32_t width, uint32_t height,
    uint32_t original_width, uint32_t original_height, uint32_t pixel_bytes) {
    return width && height && uint64_t{ width } * 2 == original_width
        && uint64_t{ height } * 2 == original_height
        && (pixel_bytes == 1 || pixel_bytes == 2 || pixel_bytes == 4 || pixel_bytes == 8);
}

// Equivalent to a nearest-filter 2x blit for bit-preserving, linear formats.
// Source rows are tightly packed. Preserve guest row padding on both rows.
template <size_t PixelBytes>
inline void expand_half_resolution_pixels(uint8_t *destination, size_t stride,
    const uint8_t *source, uint32_t width, uint32_t height) {
    const size_t source_stride = size_t{ width } * PixelBytes;
    for (uint32_t y = 0; y < height; ++y) {
        auto *row = destination + size_t{ y } * 2 * stride;
        const auto *input = source + size_t{ y } * source_stride;
        for (uint32_t x = 0; x < width; ++x) {
            std::memcpy(row + size_t{ x } * 2 * PixelBytes, input + size_t{ x } * PixelBytes, PixelBytes);
            std::memcpy(row + (size_t{ x } * 2 + 1) * PixelBytes, input + size_t{ x } * PixelBytes, PixelBytes);
        }
        std::memcpy(row + stride, row, source_stride * 2);
    }
}

inline void expand_half_resolution(uint8_t *destination, size_t stride,
    const uint8_t *source, uint32_t width, uint32_t height, uint32_t pixel_bytes) {
    switch (pixel_bytes) {
    case 1: expand_half_resolution_pixels<1>(destination, stride, source, width, height); break;
    case 2: expand_half_resolution_pixels<2>(destination, stride, source, width, height); break;
    case 4: expand_half_resolution_pixels<4>(destination, stride, source, width, height); break;
    case 8: expand_half_resolution_pixels<8>(destination, stride, source, width, height); break;
    }
}

} // namespace renderer
