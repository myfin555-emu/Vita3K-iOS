#include <renderer/vulkan/vertex_stream.h>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace renderer::vulkan;
static void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    const std::array attributes{
        vk::VertexInputAttributeDescription{.location = 0, .binding = 0, .format = vk::Format::eR32G32B32A32Uint, .offset = 0},
        vk::VertexInputAttributeDescription{.location = 1, .binding = 1, .format = vk::Format::eR16G16B16A16Uint, .offset = 6},
    };
    require(metal_vertex_stride(12, 0, attributes) == 16, "Metal would truncate UInt4 to UInt3");
    require(metal_vertex_stride(10, 1, attributes) == 16, "attribute end exceeds expanded stride");
    require(metal_vertex_stride(7, 2, attributes) == 8, "unaligned Metal stride");
    require(metal_vertex_stride(0, 0, attributes) == 0, "constant binding changed");
    require(metal_vertex_stride(32, 0, attributes) == 32, "compatible stride changed");

    // The last record is short. ASan must see no read beyond these exact bytes.
    const auto source = std::make_unique<uint8_t[]>(37);
    for (uint32_t i = 0; i < 37; ++i) source[i] = static_cast<uint8_t>(i + 1);
    for (const auto &[stride, binding] : {std::pair{12u, 0u}, {10u, 1u}, {7u, 2u}}) {
        const auto host_stride = metal_vertex_stride(stride, binding, attributes);
        const auto packed = repack_vertex_stream(source.get(), 37, stride, host_stride);
        const uint32_t count = (37 + stride - 1) / stride;
        require(packed.size() == count * host_stride, "repacked size mismatch");
        for (uint32_t vertex = 0; vertex < count; ++vertex) {
            for (uint32_t byte = 0; byte < host_stride; ++byte) {
                const uint32_t offset = vertex * stride + byte;
                const uint8_t expected = offset < 37 ? source[offset] : 0;
                require(packed[vertex * host_stride + byte] == expected, "overlapping vertex payload or tail padding corrupted");
            }
        }
    }
    require(repack_vertex_stream(nullptr, 0, 12, 16).empty(), "empty upload changed");
    require(repack_vertex_stream(source.get(), 37, 0, 0).empty(), "constant upload repacked");
    std::cout << "Metal vertex stride, overlapping loads and short upload tails passed\n";
}
