// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "SrgbStorageRoundTrip.h"
#include <array>
#include <cmath>
#include <renderer/vulkan/color_surface.h>

// Exercise the production image-format/view policy on a real Vulkan driver.
// A linear storage view must preserve the bytes encoded by an sRGB attachment;
// reading/writing through it must not decode or encode those bytes again.
void srgb_storage_roundtrip(vk::Device device, vk::PhysicalDevice gpu, uint32_t family, bool legacy) {
    using namespace renderer::vulkan;
    const auto memory = gpu.getMemoryProperties();
    const auto allocate = [&](vk::MemoryRequirements req, vk::MemoryPropertyFlags flags) {
        uint32_t type = 0;
        while (type < memory.memoryTypeCount && (!(req.memoryTypeBits & (1U << type)) || (memory.memoryTypes[type].propertyFlags & flags) != flags))
            ++type;
        require(type < memory.memoryTypeCount, "no suitable memory type");
        return device.allocateMemory({ .allocationSize = req.size, .memoryTypeIndex = type });
    };
    const auto base_format = legacy ? vk::Format::eR8G8B8A8Srgb
                                    : color_surface_image_format(vk::Format::eR8G8B8A8Srgb, true);
    require(color_surface_image_format(vk::Format::eR8Unorm, true) == vk::Format::eR8Unorm,
        "single-channel surface format changed");
    require(color_surface_image_format(vk::Format::eR8G8B8A8Srgb, false) == vk::Format::eR8G8B8A8Srgb,
        "non-interlock surface format changed");
    const auto image = device.createImage({ .flags = vk::ImageCreateFlagBits::eMutableFormat,
        .imageType = vk::ImageType::e2D,
        .format = base_format,
        .extent = { 1, 1, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment
            | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage
            | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst });
    const auto image_memory = allocate(device.getImageMemoryRequirements(image), {});
    device.bindImageMemory(image, image_memory, 0);
    // Matches the default Image::init_image view: base format, inherited usage.
    const auto storage = create_color_surface_view(device, image, base_format, {}, {}, false);
    const auto attachment = create_color_surface_view(device, image, vk::Format::eR8G8B8A8Srgb, {},
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment, !legacy);
    const auto sampled = create_color_surface_view(device, image, vk::Format::eR8G8B8A8Srgb, {},
        vk::ImageUsageFlagBits::eSampled, !legacy);
    const auto presented = create_color_surface_view(device, image, vk::Format::eR8G8B8A8Unorm,
        { vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eA },
        vk::ImageUsageFlagBits::eSampled, !legacy);
    const vk::DescriptorSetLayoutBinding binding{ .binding = 0, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute };
    const auto set_layout = device.createDescriptorSetLayout({ .bindingCount = 1, .pBindings = &binding });
    const vk::DescriptorPoolSize pool_size{ vk::DescriptorType::eStorageImage, 1 };
    const auto descriptors = device.createDescriptorPool({ .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size });
    const auto set = device.allocateDescriptorSets({ .descriptorPool = descriptors, .descriptorSetCount = 1, .pSetLayouts = &set_layout }).front();
    const vk::DescriptorImageInfo descriptor{ .imageView = storage, .imageLayout = vk::ImageLayout::eGeneral };
    device.updateDescriptorSets(vk::WriteDescriptorSet{ .dstSet = set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &descriptor }, {});

    if (!legacy) {
        const auto shader = device.createShaderModule({ .codeSize = sizeof(srgb_roundtrip_code), .pCode = srgb_roundtrip_code });
        const auto layout = device.createPipelineLayout({ .setLayoutCount = 1, .pSetLayouts = &set_layout });
        const auto pipeline = device.createComputePipeline(nullptr, { .stage = { .stage = vk::ShaderStageFlagBits::eCompute, .module = shader, .pName = "main" }, .layout = layout }).value;
        const auto buffer = device.createBuffer({ .size = 4, .usage = vk::BufferUsageFlagBits::eTransferDst });
        const auto buffer_memory = allocate(device.getBufferMemoryRequirements(buffer),
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        device.bindBufferMemory(buffer, buffer_memory, 0);
        const auto pool = device.createCommandPool({ .queueFamilyIndex = family });
        const auto cmd = device.allocateCommandBuffers({ .commandPool = pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1 }).front();
        const auto queue = device.getQueue(family, 0);
        const vk::ImageSubresourceRange range{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
        const auto barrier = [&](vk::PipelineStageFlags from, vk::AccessFlags src,
                                 vk::PipelineStageFlags to, vk::AccessFlags dst,
                                 vk::ImageLayout old_layout, vk::ImageLayout new_layout) {
            const vk::ImageMemoryBarrier dependency{ .srcAccessMask = src, .dstAccessMask = dst, .oldLayout = old_layout, .newLayout = new_layout, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image, .subresourceRange = range };
            cmd.pipelineBarrier(from, to, {}, {}, {}, dependency);
        };
        // Repeat sRGB -> linear -> sRGB on one allocation, as games reuse targets.
        for (bool gamma : { true, false, true }) {
            const vk::AttachmentDescription color{ .format = gamma ? vk::Format::eR8G8B8A8Srgb : base_format,
                .samples = vk::SampleCountFlagBits::e1,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .initialLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .finalLayout = vk::ImageLayout::eColorAttachmentOptimal };
            const vk::AttachmentReference ref{ .attachment = 0, .layout = vk::ImageLayout::eColorAttachmentOptimal };
            const vk::SubpassDescription subpass{ .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
                .colorAttachmentCount = 1,
                .pColorAttachments = &ref };
            const auto pass = device.createRenderPass({ .attachmentCount = 1, .pAttachments = &color, .subpassCount = 1, .pSubpasses = &subpass });
            const auto view = gamma ? attachment : storage;
            const auto framebuffer = device.createFramebuffer({ .renderPass = pass, .attachmentCount = 1, .pAttachments = &view, .width = 1, .height = 1, .layers = 1 });
            cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
            barrier(vk::PipelineStageFlagBits::eTopOfPipe, {}, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::AccessFlagBits::eColorAttachmentWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
            const vk::ClearValue clear{ .color = vk::ClearColorValue{ std::array<float, 4>{ 0.25f, 0.5f, 0.75f, 0.5f } } };
            cmd.beginRenderPass({ .renderPass = pass, .framebuffer = framebuffer, .renderArea = { { 0, 0 }, { 1, 1 } }, .clearValueCount = 1, .pClearValues = &clear }, vk::SubpassContents::eInline);
            cmd.endRenderPass();
            barrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite,
                vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral);
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, layout, 0, set, {});
            cmd.dispatch(1, 1, 1);
            barrier(vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite,
                vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead,
                vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal);
            const vk::BufferImageCopy copy{ .imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 }, .imageExtent = { 1, 1, 1 } };
            cmd.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, buffer, copy);
            const vk::MemoryBarrier host{ .srcAccessMask = vk::AccessFlagBits::eTransferWrite, .dstAccessMask = vk::AccessFlagBits::eHostRead };
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, host, {}, {});
            cmd.end();
            queue.submit(vk::SubmitInfo{ .commandBufferCount = 1, .pCommandBuffers = &cmd });
            queue.waitIdle();
            const auto *pixels = static_cast<const uint8_t *>(device.mapMemory(buffer_memory, 0, 4));
            const std::array<int, 4> expected = gamma ? std::array<int, 4>{ 225, 188, 137, 128 } : std::array<int, 4>{ 191, 128, 64, 128 };
            for (size_t c = 0; c < 4; ++c) {
                if (std::abs(int(pixels[c]) - expected[c]) > 1)
                    std::cerr << "gamma=" << gamma << " channel=" << c << " expected=" << expected[c] << " got=" << int(pixels[c]) << '\n';
                require(std::abs(int(pixels[c]) - expected[c]) <= 1, "storage read/write changed sRGB bytes or alpha");
            }
            device.unmapMemory(buffer_memory);
            device.resetCommandPool(pool);
            device.destroyFramebuffer(framebuffer);
            device.destroyRenderPass(pass);
        }
        device.destroyCommandPool(pool);
        device.destroyBuffer(buffer);
        device.freeMemory(buffer_memory);
        device.destroyPipeline(pipeline);
        device.destroyPipelineLayout(layout);
        device.destroyShaderModule(shader);
        std::cout << "sRGB/linear/sRGB attachment -> rgba8 storage round trips preserve RGB bytes and linear alpha\n";
    }
    device.destroyDescriptorPool(descriptors);
    device.destroyDescriptorSetLayout(set_layout);
    for (auto view : { storage, attachment, sampled, presented })
        device.destroyImageView(view);
    device.destroyImage(image);
    device.freeMemory(image_memory);
}
