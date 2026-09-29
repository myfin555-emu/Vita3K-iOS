#include <renderer/pvrt-dec.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void require(bool value, const char *message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void check_solid(uint32_t width, uint32_t height, bool two_bpp, bool version_two,
    uint32_t endpoints, std::array<uint8_t, 4> expected) {
    // Identical opaque A/B endpoints and zero modulation yield a solid image,
    // independently of block order. PVRTC still stores a minimum 2x2 words
    // for tiny mip levels; the decoder must crop these to the requested size.
    const uint32_t stored_width = std::max(width, two_bpp ? 16U : 8U);
    const uint32_t stored_height = std::max(height, 8U);
    const uint32_t compressed_bytes = stored_width * stored_height / (two_bpp ? 4 : 2);
    std::vector<uint32_t> compressed(compressed_bytes / 4, 0);
    for (size_t i = 1; i < compressed.size(); i += 2)
        compressed[i] = endpoints;
    const size_t rgba_bytes = width * height * 4;
    std::vector<uint8_t> rgba(rgba_bytes + 32, 0xa5);
    const auto consumed = pvr::PVRTDecompressPVRTC(compressed.data(), two_bpp,
        width, height, version_two, rgba.data() + 16);
    require(consumed == compressed_bytes, "PVRTC source footprint is wrong");
    for (size_t i = 0; i < rgba_bytes; ++i)
        require(rgba[16 + i] == expected[i % 4], "decoded RGBA pixel differs from known endpoint color");
    for (size_t i = 0; i < 16; ++i)
        require(rgba[i] == 0xa5 && rgba[16 + rgba_bytes + i] == 0xa5, "decoder wrote outside the RGBA image");
}
} // namespace

int main() {
    for (bool two_bpp : { false, true }) {
        for (bool version_two : { false, true }) {
            for (const auto size : { std::array<uint32_t, 2>{ 32, 16 }, { 16, 8 }, { 4, 4 }, { 1, 1 } }) {
                check_solid(size[0], size[1], two_bpp, version_two, 0xfc00fc00, { 255, 0, 0, 255 });
                check_solid(size[0], size[1], two_bpp, version_two, 0x83e083e0, { 0, 255, 0, 255 });
                check_solid(size[0], size[1], two_bpp, version_two, 0x801f801e, { 0, 0, 255, 255 });
                check_solid(size[0], size[1], two_bpp, version_two, 0xfffffffe, { 255, 255, 255, 255 });
            }
        }
    }
}
