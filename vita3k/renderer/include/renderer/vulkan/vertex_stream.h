// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

#include <algorithm>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>
#include <vkutil/vulkan.h>
#include <vulkan/vulkan_format_traits.hpp>

namespace renderer::vulkan {

// Metal cannot fetch an attribute wider than its binding's stride. Preserve
// Vulkan's overlapping loads by expanding each host record, including all
// attribute offsets, instead of letting MoltenVK truncate the input format.
inline uint32_t metal_vertex_stride(uint32_t guest_stride, uint32_t binding,
    std::span<const vk::VertexInputAttributeDescription> attributes) {
    if (guest_stride == 0)
        return 0; // constant binding; Metal handles this separately
    uint64_t extent = guest_stride;
    for (const auto &attribute : attributes) {
        if (attribute.binding == binding)
            extent = std::max(extent, uint64_t(attribute.offset) + vk::blockSize(attribute.format));
    }
    extent = (extent + 3) & ~uint64_t{ 3 };
    if (extent > std::numeric_limits<uint32_t>::max())
        throw std::length_error("Metal vertex stride overflow");
    return static_cast<uint32_t>(extent);
}

inline std::vector<uint8_t> repack_vertex_stream(const uint8_t *source, uint32_t size,
    uint32_t guest_stride, uint32_t host_stride) {
    if (!size || !guest_stride)
        return {};
    const uint64_t count = (uint64_t(size) + guest_stride - 1) / guest_stride;
    const uint64_t output_size = count * host_stride;
    if (output_size > std::numeric_limits<uint32_t>::max())
        throw std::length_error("Metal vertex stream overflow");
    std::vector<uint8_t> result(static_cast<size_t>(output_size), 0);
    for (uint64_t i = 0; i < count; ++i) {
        const uint64_t offset = i * guest_stride;
        // The last uploaded record can end mid-stride. Copy overlapping bytes
        // only when present and zero the remainder; never read past the upload.
        const size_t bytes = std::min<uint64_t>(host_stride, size - offset);
        std::memcpy(result.data() + i * host_stride, source + offset, bytes);
    }
    return result;
}

} // namespace renderer::vulkan
