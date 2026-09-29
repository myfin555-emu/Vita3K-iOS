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

#include <vkutil/vulkan.h>

namespace renderer::vulkan {

// Interlock shaders decode/encode sRGB explicitly around imageLoad/imageStore.
// Their typed rgba8 storage image must see the encoded bytes through UNORM;
// sRGB is only a view for fixed-function attachment writes or texture sampling.
inline vk::Format color_surface_image_format(vk::Format attachment_format, bool interlock) {
    return interlock && attachment_format == vk::Format::eR8G8B8A8Srgb
        ? vk::Format::eR8G8B8A8Unorm
        : attachment_format;
}

inline vk::ImageView create_color_surface_view(vk::Device device, vk::Image image,
    vk::Format format, vk::ComponentMapping components, vk::ImageUsageFlags usage,
    bool separate_view_usage) {
    const vk::ImageViewUsageCreateInfo view_usage{ .usage = usage };
    const vk::ImageViewCreateInfo view_info{
        .pNext = separate_view_usage ? &view_usage : nullptr,
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .components = components,
        .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
    };
    // Do not inherit STORAGE on sRGB sampling/attachment views: that format
    // is not a portable storage format and does not match the shader's rgba8.
    return device.createImageView(view_info);
}

} // namespace renderer::vulkan
