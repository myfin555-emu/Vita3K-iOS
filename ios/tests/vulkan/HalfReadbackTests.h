// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <renderer/half_resolution_readback.h>
#include <vector>

// Compare the optimized readback with the former GPU nearest-blit path using
// real Vulkan transfers and synchronization validation, for every eligible format.
void half_readback_roundtrip(vk::Device device, vk::PhysicalDevice gpu, uint32_t family) {
    const auto memory = gpu.getMemoryProperties();
    const auto allocate = [&](vk::MemoryRequirements req, vk::MemoryPropertyFlags flags) {
        uint32_t type = 0;
        while (type < memory.memoryTypeCount && (!(req.memoryTypeBits & (1U << type)) || (memory.memoryTypes[type].propertyFlags & flags) != flags))
            ++type;
        require(type < memory.memoryTypeCount, "no memory for half readback regression");
        return device.allocateMemory({ .allocationSize = req.size, .memoryTypeIndex = type });
    };
    for (const auto format : { vk::Format::eR8Unorm, vk::Format::eR8G8Unorm, vk::Format::eR8G8B8A8Unorm }) {
        const uint32_t bytes = format == vk::Format::eR8Unorm ? 1 : format == vk::Format::eR8G8Unorm ? 2
                                                                                                     : 4;
        constexpr uint32_t width = 128, height = 2;
        const size_t compact_bytes = width * height * bytes;
        const auto buffer = device.createBuffer({ .size = compact_bytes * 6,
            .usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst });
        const auto buffer_memory = allocate(device.getBufferMemoryRequirements(buffer), vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        device.bindBufferMemory(buffer, buffer_memory, 0);
        auto *mapped = static_cast<uint8_t *>(device.mapMemory(buffer_memory, 0, compact_bytes * 6));
        for (size_t i = 0; i < compact_bytes; ++i)
            mapped[i] = static_cast<uint8_t>(i * 37);
        vk::Image images[2];
        vk::DeviceMemory allocations[2];
        for (uint32_t i = 0; i < 2; ++i) {
            images[i] = device.createImage({ .imageType = vk::ImageType::e2D, .format = format, .extent = { width * (i + 1), height * (i + 1), 1 }, .mipLevels = 1, .arrayLayers = 1, .samples = vk::SampleCountFlagBits::e1, .tiling = vk::ImageTiling::eOptimal, .usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst });
            allocations[i] = allocate(device.getImageMemoryRequirements(images[i]), {});
            device.bindImageMemory(images[i], allocations[i], 0);
        }
        const auto pool = device.createCommandPool({ .queueFamilyIndex = family });
        const auto cmd = device.allocateCommandBuffers({ .commandPool = pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1 })[0];
        cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
        const vk::ImageSubresourceRange range{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
        const auto transition = [&](vk::Image image, bool to_source) {
            const vk::ImageMemoryBarrier barrier{
                .srcAccessMask = to_source ? vk::AccessFlagBits::eTransferWrite : vk::AccessFlags{},
                .dstAccessMask = to_source ? vk::AccessFlagBits::eTransferRead : vk::AccessFlagBits::eTransferWrite,
                .oldLayout = to_source ? vk::ImageLayout::eTransferDstOptimal : vk::ImageLayout::eUndefined,
                .newLayout = to_source ? vk::ImageLayout::eTransferSrcOptimal : vk::ImageLayout::eTransferDstOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = range
            };
            cmd.pipelineBarrier(to_source ? vk::PipelineStageFlagBits::eTransfer : vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier);
        };
        transition(images[0], false);
        transition(images[1], false);
        const vk::ImageSubresourceLayers layer{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
        vk::BufferImageCopy copy{ .imageSubresource = layer, .imageExtent = { width, height, 1 } };
        cmd.copyBufferToImage(buffer, images[0], vk::ImageLayout::eTransferDstOptimal, copy);
        transition(images[0], true);
        const vk::ImageBlit blit{ .srcSubresource = layer,
            .srcOffsets = std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ width, height, 1 } },
            .dstSubresource = layer,
            .dstOffsets = std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ width * 2, height * 2, 1 } } };
        cmd.blitImage(images[0], vk::ImageLayout::eTransferSrcOptimal, images[1], vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eNearest);
        transition(images[1], true);
        copy.bufferOffset = compact_bytes;
        cmd.copyImageToBuffer(images[0], vk::ImageLayout::eTransferSrcOptimal, buffer, copy);
        copy.bufferOffset = compact_bytes * 2;
        copy.imageExtent = vk::Extent3D{ width * 2, height * 2, 1 };
        cmd.copyImageToBuffer(images[1], vk::ImageLayout::eTransferSrcOptimal, buffer, copy);
        const vk::MemoryBarrier host{ .srcAccessMask = vk::AccessFlagBits::eTransferWrite, .dstAccessMask = vk::AccessFlagBits::eHostRead };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, host, {}, {});
        cmd.end();
        const auto queue = device.getQueue(family, 0);
        queue.submit(vk::SubmitInfo{ .commandBufferCount = 1, .pCommandBuffers = &cmd });
        queue.waitIdle();
        std::vector<uint8_t> expanded(compact_bytes * 4);
        renderer::expand_half_resolution(expanded.data(), width * 2 * bytes, mapped + compact_bytes, width, height, bytes);
        require(std::memcmp(mapped + compact_bytes * 2, expanded.data(), expanded.size()) == 0, "CPU expansion differs from GPU nearest blit");
        device.unmapMemory(buffer_memory);
        device.destroyCommandPool(pool);
        device.destroyBuffer(buffer);
        device.freeMemory(buffer_memory);
        for (uint32_t i = 0; i < 2; ++i) {
            device.destroyImage(images[i]);
            device.freeMemory(allocations[i]);
        }
    }
    std::cout << "Half readback: R8/RG8/RGBA8 match GPU nearest blits byte-for-byte\n";
}
