"""Compile actual cache view/presentation/descriptor selection with a recording Vulkan boundary."""
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[3]
surface = (repo / 'vita3k/renderer/src/vulkan/surface_cache.cpp').read_text()
context = (repo / 'vita3k/renderer/src/vulkan/context.cpp').read_text()
util = (repo / 'vita3k/vkutil/src/vkutil.cpp').read_text()
header = (repo / 'vita3k/vkutil/include/vkutil/vkutil.h').read_text()

def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

code = r'''
#include <renderer/vulkan/color_surface.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
using namespace renderer::vulkan;
#define LOG_DEBUG(...) ((void)0)
namespace vkutil {
'''
code += header[header.index('static constexpr vk::ComponentMapping default_comp_mapping'):header.index('static constexpr vk::ColorComponentFlags default_color_mask')]
code += function(util, 'vk::ComponentMapping color_to_texture_swizzle(') + '\n}\n'
code += r'''
struct CreatedView { vk::Format format; vk::ComponentMapping components; VkImageUsageFlags usage; };
std::vector<CreatedView> views;
VKAPI_ATTR VkResult VKAPI_CALL create_view(VkDevice, const VkImageViewCreateInfo *info,
    const VkAllocationCallbacks *, VkImageView *view) {
    const auto *usage = static_cast<const VkImageViewUsageCreateInfo *>(info->pNext);
    assert(usage && usage->sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO);
    views.push_back({static_cast<vk::Format>(info->format),
        {static_cast<vk::ComponentSwizzle>(info->components.r), static_cast<vk::ComponentSwizzle>(info->components.g),
         static_cast<vk::ComponentSwizzle>(info->components.b), static_cast<vk::ComponentSwizzle>(info->components.a)}, usage->usage});
    *view = reinterpret_cast<VkImageView>(views.size() + 100);
    return VK_SUCCESS;
}
struct Image { vk::Image image; vk::ImageView view; vk::Format format; };
struct SampledSurfaceView { vk::ImageView view; vk::Format format; vk::ComponentMapping components; };
struct ColorSurfaceCacheInfo {
    Image texture{reinterpret_cast<VkImage>(uintptr_t{1}), reinterpret_cast<VkImageView>(uintptr_t{1}), vk::Format::eR8G8B8A8Unorm};
    vk::ImageView alternate_view{};
    std::vector<SampledSurfaceView> sampled_views;
    vk::ComponentMapping swizzle = vkutil::rgba_mapping;
};
struct State {
    vk::Device device{reinterpret_cast<VkDevice>(uintptr_t{1})};
    struct { bool support_shader_interlock = true; } features;
};
struct VKSurfaceCache {
    State state;
    bool support_image_view_usage = true;
    vk::ImageView retrieve_color_attachment_view(ColorSurfaceCacheInfo &, vk::Format);
    vk::ImageView retrieve_sampled_view(ColorSurfaceCacheInfo &, vk::Format, const vk::ComponentMapping &);
    vk::ImageView present(ColorSurfaceCacheInfo &);
};
'''
code += function(surface, 'vk::ImageView VKSurfaceCache::retrieve_color_attachment_view(')
code += function(surface, 'vk::ImageView VKSurfaceCache::retrieve_sampled_view(')
start = surface.index('            if (info.swizzle == vkutil::rgba_mapping')
end = surface.index('\n        }\n    }\n\n    return nullptr;', start)
code += '\nvk::ImageView VKSurfaceCache::present(ColorSurfaceCacheInfo &info) {\n' + surface[start:end] + '\n}\n'
start = context.index('    vk::DescriptorImageInfo descr_color_info{')
end = context.index('\n    };', start) + len('\n    };')
code += '''
vk::DescriptorImageInfo descriptor(State state, Image *current_color_base_image, vk::ImageView current_color_view) {
''' + context[start:end] + '\nreturn descr_color_info;\n}\n'
code += r'''
int main() {
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateImageView = create_view;
    VKSurfaceCache cache;
    ColorSurfaceCacheInfo info;
    const auto linear = cache.retrieve_color_attachment_view(info, vk::Format::eR8G8B8A8Unorm);
    assert(linear == info.texture.view && views.empty());
    const auto gamma = cache.retrieve_color_attachment_view(info, vk::Format::eR8G8B8A8Srgb);
    assert(gamma != linear && views.size() == 1);
    assert(views[0].format == vk::Format::eR8G8B8A8Srgb && views[0].components == vkutil::default_comp_mapping);
    assert((views[0].usage & VK_IMAGE_USAGE_STORAGE_BIT) == 0);
    assert((views[0].usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0);
    for (int i = 0; i < 10; ++i) {
        assert(cache.retrieve_color_attachment_view(info, vk::Format::eR8G8B8A8Srgb) == gamma);
        assert(cache.retrieve_color_attachment_view(info, vk::Format::eR8G8B8A8Unorm) == linear);
    }
    assert(views.size() == 1);
    assert(descriptor(cache.state, &info.texture, gamma).imageView == linear);
    cache.state.features.support_shader_interlock = false;
    assert(descriptor(cache.state, &info.texture, gamma).imageView == gamma);
    assert(cache.present(info) == linear); // encoded bytes, no second gamma decode
    info.swizzle = {vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eA};
    const auto presented = cache.present(info);
    assert(presented != gamma && presented != linear && views.size() == 2);
    assert(views[1].format == vk::Format::eR8G8B8A8Unorm && views[1].components == info.swizzle);
    assert(views[1].usage == VK_IMAGE_USAGE_SAMPLED_BIT);
    assert(cache.present(info) == presented && views.size() == 2);
    const auto sampled = cache.retrieve_sampled_view(info, vk::Format::eR8G8B8A8Srgb, info.swizzle);
    assert(sampled != presented && sampled != gamma && views.size() == 3);
    assert(views[2].usage == VK_IMAGE_USAGE_SAMPLED_BIT);
    assert(cache.retrieve_color_attachment_view(info, vk::Format::eR8G8B8A8Srgb) == gamma);
    // Also preserve the non-interlock path: sRGB base with a linear alternate.
    info = ColorSurfaceCacheInfo{};
    info.texture.format = vk::Format::eR8G8B8A8Srgb;
    const auto linear_alternate = cache.retrieve_color_attachment_view(info, vk::Format::eR8G8B8A8Unorm);
    const auto encoded_present = cache.present(info);
    assert(encoded_present != linear_alternate && encoded_present != info.texture.view);
    assert(views.back().format == vk::Format::eR8G8B8A8Unorm);
    std::cout << "Production attachment/sample/presentation view isolation, cache reuse and storage descriptor routing passed\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp, binary = pathlib.Path(tmp) / 'views.cpp', pathlib.Path(tmp) / 'views'
    cpp.write_text(code)
    subprocess.run([sys.argv[1], '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(repo / 'vita3k/renderer/include'), '-I' + str(repo / 'vita3k/vkutil/include'),
                    '-I' + sys.argv[2], str(cpp), '-ldl', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
