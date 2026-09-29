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

#include <array>
#include <vulkan/vulkan.hpp>

namespace renderer::vulkan {

// Render targets can alternate between attachment writes, storage-image
// framebuffer fetch and sampling. Submission order alone does not make those
// writes visible to the next pass.
inline std::array<vk::SubpassDependency, 4> render_pass_dependencies(bool interlock, bool no_color) {
    const auto shaders = vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;
    const auto tests = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
    std::array<vk::SubpassDependency, 4> dependencies{};
    dependencies[0] = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | tests | vk::PipelineStageFlagBits::eTransfer,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | tests | shaders,
        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite
            | vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eShaderRead
    };
    if (interlock) {
        dependencies[0].srcStageMask |= vk::PipelineStageFlagBits::eFragmentShader;
        dependencies[0].srcAccessMask |= vk::AccessFlagBits::eShaderWrite;
        if (no_color)
            dependencies[0].dstAccessMask |= vk::AccessFlagBits::eShaderWrite;
    }

    // Also finish sampling before overwriting the image in a later pass.
    dependencies[1] = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = shaders | vk::PipelineStageFlagBits::eTransfer,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | tests,
        .srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead,
        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite
    };
    if (interlock && no_color) {
        dependencies[1].dstStageMask |= vk::PipelineStageFlagBits::eFragmentShader;
        dependencies[1].dstAccessMask |= vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
    }

    // In-pass programmable blending and mid-scene vertex-buffer writes.
    dependencies[2] = {
        .srcSubpass = 0,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .dstStageMask = vk::PipelineStageFlagBits::eFragmentShader,
        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
        .dstAccessMask = vk::AccessFlagBits::eInputAttachmentRead,
        .dependencyFlags = vk::DependencyFlagBits::eByRegion
    };
    dependencies[3] = {
        .srcSubpass = 0,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eVertexShader,
        .dstStageMask = vk::PipelineStageFlagBits::eVertexInput,
        .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
        .dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead
    };
    return dependencies;
}

// Used outside render passes, before copying a rendered surface.
inline vk::MemoryBarrier surface_transfer_barrier() {
    return { .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eTransferRead };
}
inline constexpr auto surface_write_stages = vk::PipelineStageFlagBits::eColorAttachmentOutput
    | vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eTransfer;

} // namespace renderer::vulkan
