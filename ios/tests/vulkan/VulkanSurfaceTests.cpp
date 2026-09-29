#include <renderer/vulkan/render_pass_dependencies.h>
#include <renderer/vulkan/texture_descriptor_cache.h>
#include <cstdlib>
#include <iostream>
#include <cstring>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

using namespace renderer::vulkan;
void require(bool ok, const char *message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
static int validation_errors = 0;
VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT *message, void *) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++validation_errors;
        std::cerr << message->pMessage << '\n';
    }
    return VK_FALSE;
}

void descriptor_reuse() {
    TextureDescriptorCache cache;
    TextureDescriptorKey a{};
    a.count = 1;
    a.images[0].imageLayout = vk::ImageLayout::eGeneral;
    TextureDescriptorKey b = a;
    b.images[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    int creates = 0;
    const auto create = [&]() { ++creates; return vk::DescriptorSet{}; };
    // UI sprites alternating between two atlases must not rewrite a set per draw.
    for (int i = 0; i < 10000; ++i)
        cache.get_or_create(i % 2 ? a : b, create);
    require(creates == 2, "alternating descriptors were not reused");
    auto changed = a;
    changed.vertex = true;
    cache.get_or_create(changed, create);
    changed = a;
    changed.count = 2;
    cache.get_or_create(changed, create);
    changed = a;
    changed.images[0].sampler = reinterpret_cast<VkSampler>(uintptr_t{1});
    cache.get_or_create(changed, create);
    changed = a;
    changed.images[0].imageView = reinterpret_cast<VkImageView>(uintptr_t{1});
    cache.get_or_create(changed, create);
    require(creates == 6, "descriptor key omitted layout, count, stage, sampler or view");
    cache.clear();
    cache.get_or_create(a, create);
    require(creates == 7, "frame reset retained stale descriptors");
    std::cout << "10000 alternating bindings: 2 descriptor writes; key changes/reset passed\n";
}

void dependency_coverage() {
    for (bool interlock : {false, true}) {
        for (bool no_color : {false, true}) {
            const auto deps = render_pass_dependencies(interlock, no_color);
            require(bool(deps[0].dstStageMask & vk::PipelineStageFlagBits::eFragmentShader), "rendered color not visible to sampling");
            require(bool(deps[0].dstAccessMask & vk::AccessFlagBits::eShaderRead), "missing sampled read dependency");
            require(bool(deps[0].srcAccessMask & vk::AccessFlagBits::eTransferWrite), "copied targets not visible to next pass");
            if (interlock) {
                require(bool(deps[0].srcAccessMask & vk::AccessFlagBits::eShaderWrite), "fetch writes not visible to next pass");
                if (no_color) {
                    require(bool(deps[0].dstAccessMask & vk::AccessFlagBits::eShaderWrite), "attachment to fetch dependency missing");
                    require(bool(deps[1].dstAccessMask & vk::AccessFlagBits::eShaderWrite), "sample to fetch dependency overwritten");
                }
            }
        }
    }
    require(bool(surface_transfer_barrier().srcAccessMask & vk::AccessFlagBits::eShaderWrite), "readback excludes fetch writes");
}

int main() {
    VULKAN_HPP_DEFAULT_DISPATCHER.init();
    descriptor_reuse();
    dependency_coverage();
    const char *layer = "VK_LAYER_KHRONOS_validation";
    const char *extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkValidationFeatureEnableEXT sync = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT, nullptr, 1, &sync, 0, nullptr};
    vk::ApplicationInfo app{.pApplicationName = "surface-regressions", .apiVersion = VK_API_VERSION_1_1};
    vk::InstanceCreateInfo info{.pNext = &validation, .pApplicationInfo = &app,
        .enabledLayerCount = 1, .ppEnabledLayerNames = &layer,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = &extension};
    const auto instance = vk::createInstance(info);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
    auto create_debug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(instance.getProcAddr("vkCreateDebugUtilsMessengerEXT"));
    auto destroy_debug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(instance.getProcAddr("vkDestroyDebugUtilsMessengerEXT"));
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    debug.pfnUserCallback = debug_message;
    VkDebugUtilsMessengerEXT messenger{};
    require(create_debug(instance, &debug, nullptr, &messenger) == VK_SUCCESS, "validation callback failed");
    const auto devices = instance.enumeratePhysicalDevices();
    require(!devices.empty(), "no Vulkan device");
    const auto gpu = devices.front();
    const auto queues = gpu.getQueueFamilyProperties();
    uint32_t family = 0;
    while (family < queues.size() && !(queues[family].queueFlags & vk::QueueFlagBits::eGraphics)) ++family;
    require(family < queues.size(), "no graphics queue");
    const float priority = 1.0f;
    vk::DeviceQueueCreateInfo queue_info{.queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    vk::DeviceCreateInfo device_info{.queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info};
    const auto device = gpu.createDevice(device_info);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
    // Validate every production render-pass mode and its stage/access masks.
    for (bool interlock : {false, true}) {
        for (bool no_color : {false, true}) {
            const auto deps = render_pass_dependencies(interlock, no_color);
            vk::AttachmentDescription attachments[] = {
                {.format = vk::Format::eR8G8B8A8Unorm, .samples = vk::SampleCountFlagBits::e1,
                    .loadOp = vk::AttachmentLoadOp::eLoad, .storeOp = vk::AttachmentStoreOp::eStore,
                    .initialLayout = vk::ImageLayout::eGeneral, .finalLayout = vk::ImageLayout::eGeneral},
                {.format = vk::Format::eD32Sfloat, .samples = vk::SampleCountFlagBits::e1,
                    .loadOp = vk::AttachmentLoadOp::eClear, .storeOp = vk::AttachmentStoreOp::eStore,
                    .initialLayout = vk::ImageLayout::eUndefined, .finalLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal}
            };
            vk::AttachmentReference color{.attachment = 0, .layout = vk::ImageLayout::eGeneral};
            vk::AttachmentReference depth{.attachment = no_color ? 0U : 1U, .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal};
            vk::SubpassDescription subpass{.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
                .inputAttachmentCount = no_color ? 0U : 1U, .pInputAttachments = &color,
                .colorAttachmentCount = no_color ? 0U : 1U, .pColorAttachments = &color, .pDepthStencilAttachment = &depth};
            vk::RenderPassCreateInfo pass_info{.attachmentCount = no_color ? 1U : 2U,
                .pAttachments = no_color ? &attachments[1] : attachments, .subpassCount = 1, .pSubpasses = &subpass,
                .dependencyCount = no_color ? 2U : 4U, .pDependencies = deps.data()};
            const auto pass = device.createRenderPass(pass_info);
            device.destroyRenderPass(pass);
        }
    }
    std::cout << "Validated render passes on " << gpu.getProperties().deviceName << '\n';
    device.destroy();
    destroy_debug(instance, messenger, nullptr);
    instance.destroy();
    require(validation_errors == 0, "Vulkan validation reported errors");
}
