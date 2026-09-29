// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>

static void check(VkResult result) {
    if (result != VK_SUCCESS)
        throw std::runtime_error("Vulkan result " + std::to_string(result));
}

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("pass generated SPIR-V module");
        std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
        const auto bytes = file.tellg();
        if (bytes <= 0 || bytes % 4)
            throw std::runtime_error("invalid SPIR-V file");
        std::vector<uint32_t> code(static_cast<size_t>(bytes) / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char *>(code.data()), bytes);
        if (!file)
            throw std::runtime_error("cannot read SPIR-V file");
        VkInstance instance;
        VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo instance_info{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        instance_info.pApplicationInfo = &app;
        check(vkCreateInstance(&instance_info, nullptr, &instance));
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
        if (!count)
            throw std::runtime_error("no Vulkan device");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
        auto physical = devices[0];
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
        uint32_t family = 0;
        while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT))
            ++family;
        if (family == count)
            throw std::runtime_error("no compute queue");
        float priority = 1;
        VkDeviceQueueCreateInfo queue_info{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queue_info.queueFamilyIndex = family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        VkDeviceCreateInfo device_info{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        VkDevice device;
        check(vkCreateDevice(physical, &device_info, nullptr, &device));
        VkQueue queue;
        vkGetDeviceQueue(device, family, 0, &queue);
        VkPhysicalDeviceMemoryProperties properties;
        vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        std::array<VkBuffer, 2> buffers;
        std::array<VkDeviceMemory, 2> memory;
        std::array<void *, 2> mapped;
        const VkDeviceSize sizes[] = { 32, 405 * sizeof(uint32_t) };
        for (int i = 0; i < 2; ++i) {
            VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            info.size = sizes[i];
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            check(vkCreateBuffer(device, &info, nullptr, &buffers[i]));
            VkMemoryRequirements requirements;
            vkGetBufferMemoryRequirements(device, buffers[i], &requirements);
            uint32_t type = 0;
            constexpr auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            while (type < properties.memoryTypeCount && (!(requirements.memoryTypeBits & (1u << type)) || (properties.memoryTypes[type].propertyFlags & flags) != flags))
                ++type;
            if (type == properties.memoryTypeCount)
                throw std::runtime_error("no coherent host memory");
            VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = type;
            check(vkAllocateMemory(device, &allocation, nullptr, &memory[i]));
            check(vkBindBufferMemory(device, buffers[i], memory[i], 0));
            check(vkMapMemory(device, memory[i], 0, sizes[i], 0, &mapped[i]));
            std::memset(mapped[i], 0, sizes[i]);
        }
        auto input = static_cast<uint8_t *>(mapped[0]);
        for (int i = 0; i < 32; ++i)
            input[i] = static_cast<uint8_t>(0x80 + i);
        VkDescriptorSetLayoutBinding bindings[2]{};
        for (uint32_t i = 0; i < 2; ++i)
            bindings[i] = { i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo layout_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout_info.bindingCount = 2;
        layout_info.pBindings = bindings;
        VkDescriptorSetLayout set_layout;
        check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &set_layout));
        VkPipelineLayoutCreateInfo pipeline_layout_info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &set_layout;
        VkPipelineLayout layout;
        check(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &layout));
        VkDescriptorPoolSize pool_size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 };
        VkDescriptorPoolCreateInfo pool_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        VkDescriptorPool pool;
        check(vkCreateDescriptorPool(device, &pool_info, nullptr, &pool));
        VkDescriptorSetAllocateInfo set_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        set_info.descriptorPool = pool;
        set_info.descriptorSetCount = 1;
        set_info.pSetLayouts = &set_layout;
        VkDescriptorSet set;
        check(vkAllocateDescriptorSets(device, &set_info, &set));
        VkDescriptorBufferInfo buffer_infos[2]{};
        VkWriteDescriptorSet writes[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            buffer_infos[i] = { buffers[i], 0, sizes[i] };
            writes[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &buffer_infos[i];
        }
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        VkShaderModuleCreateInfo shader_info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        shader_info.codeSize = code.size() * 4;
        shader_info.pCode = code.data();
        VkShaderModule shader;
        check(vkCreateShaderModule(device, &shader_info, nullptr, &shader));
        VkComputePipelineCreateInfo pipeline_info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        pipeline_info.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_info.stage.module = shader;
        pipeline_info.stage.pName = "main";
        pipeline_info.layout = layout;
        VkPipeline pipeline;
        check(vkCreateComputePipelines(device, nullptr, 1, &pipeline_info, nullptr, &pipeline));
        VkCommandPoolCreateInfo command_pool_info{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        command_pool_info.queueFamilyIndex = family;
        VkCommandPool command_pool;
        check(vkCreateCommandPool(device, &command_pool_info, nullptr, &command_pool));
        VkCommandBufferAllocateInfo command_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        command_info.commandPool = command_pool;
        command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_info.commandBufferCount = 1;
        VkCommandBuffer command;
        check(vkAllocateCommandBuffers(device, &command_info, &command));
        VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        check(vkBeginCommandBuffer(command, &begin));
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
        vkCmdDispatch(command, 1, 1, 1);
        VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        check(vkEndCommandBuffer(command));
        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        check(vkQueueSubmit(queue, 1, &submit, nullptr));
        check(vkQueueWaitIdle(queue));
        auto output = static_cast<uint32_t *>(mapped[1]);
        bool passed = true;
        for (int offset = 0; offset <= 28; ++offset) {
            uint32_t expected = 0;
            for (int byte = 0; byte < 4; ++byte)
                expected |= uint32_t(input[offset + byte]) << (8 * byte);
            if (output[offset] != expected) {
                std::cerr << "uniform byte offset " << offset << ": expected " << std::hex << expected << ", got " << output[offset] << std::dec << '\n';
                passed = false;
            }
        }
        unsigned result_index = 29;
        for (unsigned mode : { 0u, 1u, 2u }) {
            const unsigned size = mode == 0 ? 1 : 2;
            const unsigned end = mode == 2 ? 31 : 32;
            for (unsigned count = 1; count <= (mode == 2 ? 15u : 16u); ++count) {
                uint8_t expected_bytes[32];
                std::memset(expected_bytes, 0xa5, sizeof(expected_bytes));
                std::memcpy(expected_bytes, input + end - size * count, size * count);
                for (unsigned word = 0; word < 8; ++word) {
                    uint32_t expected = 0;
                    for (unsigned byte = 0; byte < 4; ++byte)
                        expected |= uint32_t(expected_bytes[word * 4 + byte]) << (byte * 8);
                    if (output[result_index++] != expected) {
                        std::cerr << "subword size=" << size << " count=" << count << " register=" << word << " expected=" << std::hex << expected << " actual=" << output[result_index - 1] << std::dec << " mismatch\n";
                        passed = false;
                    }
                }
            }
        }
        vkDestroyCommandPool(device, command_pool, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyShaderModule(device, shader, nullptr);
        vkDestroyDescriptorPool(device, pool, nullptr);
        vkDestroyPipelineLayout(device, layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        for (int i = 0; i < 2; ++i) {
            vkUnmapMemory(device, memory[i]);
            vkDestroyBuffer(device, buffers[i], nullptr);
            vkFreeMemory(device, memory[i], nullptr);
        }
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return passed ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
