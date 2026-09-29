"""Run production cache matching/readback with synthetic rows, without the emulator."""
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[3]
surface = (repo / 'vita3k/renderer/src/vulkan/surface_cache.cpp').read_text()

def function(signature):
    start = surface.index(signature)
    brace = surface.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (surface[end] == '{') - (surface[end] == '}')
        end += 1
    return surface[start:end]

start = surface.index('if ((casted_vec[i].cropped_height')
condition = surface[start + 4:surface.index(' {', start) - 1]
start = surface.index('            const size_t host_stride =')
allocation = surface[start:surface.index('            copy_buffer.init_buffer', start)]
source = r'''
#include <renderer/vulkan/texture_descriptor_cache.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_format_traits.hpp>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <iostream>
enum SceGxmColorBaseFormat { SCE_GXM_COLOR_BASE_FORMAT_U8U8U8, RGBA,
    SCE_GXM_COLOR_BASE_FORMAT_U2F10F10F10, SCE_GXM_COLOR_BASE_FORMAT_U2U10U10U10,
    SCE_GXM_COLOR_BASE_FORMAT_U4U4U4U4, SCE_GXM_COLOR_BASE_FORMAT_U1U5U5U5,
    SCE_GXM_COLOR_BASE_FORMAT_SE5M9M9M9, SCE_GXM_COLOR_BASE_FORMAT_F11F11F10,
    SCE_GXM_COLOR_BASE_FORMAT_U5U6U5 };
struct MemState {};
struct Pointer {
    uint8_t *p;
    template <typename T> Pointer cast() const { return *this; }
    uint8_t *get(const MemState &) const { return p; }
};
struct Buffer { void *mapped_data; size_t size; };
struct ColorSurfaceCacheInfo {
    uint32_t stride_bytes, original_width, original_height;
    SceGxmColorBaseFormat format;
    struct { vk::Format format; } texture;
    vk::ComponentMapping swizzle;
    Pointer data;
    std::unique_ptr<Buffer> copy_buffer;
    void *sws_context = nullptr;
};
namespace gxm { uint32_t bits_per_pixel(SceGxmColorBaseFormat f) { return f == SCE_GXM_COLOR_BASE_FORMAT_U8U8U8 ? 24 : 32; } }
enum AVPixelFormat { AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24, AV_PIX_FMT_RGB0 };
// RGB24 conversion is not exercised here; its allocation size is tested separately.
void *sws_getContext(...) { std::abort(); }
void sws_scale(...) { std::abort(); }
struct VKSurfaceCache {
    struct { struct { bool enable_memory_mapping = false; } features; } state;
    void perform_post_surface_sync(const MemState &, ColorSurfaceCacheInfo *);
};
'''
source += function('static bool format_need_additional_memory(')
source += function('static bool format_support_swizzle(')
start = surface.index('template <typename T>\nstatic void swizzle_text_T_2')
end = surface.index('void VKSurfaceCache::perform_post_surface_sync', start)
source += surface[start:end]
source += function('void VKSurfaceCache::perform_post_surface_sync(')
scene = (repo / 'vita3k/renderer/src/vulkan/scene.cpp').read_text()
start = scene.index('static vk::DescriptorSet retrieve_texture_descriptor(')
end = scene.index('static void draw_bind_descriptors(', start)
source += r'''
using namespace renderer::vulkan;
struct MockDevice {
    int updates = 0;
    void updateDescriptorSets(uint32_t count, const vk::WriteDescriptorSet *writes, uint32_t, void *) {
        ++updates;
        for (uint32_t i = 0; i < count; ++i) {
            assert(writes[i].descriptorCount == 1 && writes[i].dstBinding == i);
            assert(writes[i].pImageInfo && writes[i].pImageInfo->sampler);
        }
    }
};
struct VKContext {
    struct State {
        struct Frame { TextureDescriptorCache texture_descriptors; } frame_data;
        struct Image { vk::Sampler sampler; vk::ImageView view; } default_image;
        MockDevice device;
        Frame &frame() { return frame_data; }
    } state;
    vk::DescriptorImageInfo vertex_textures[16]{}, fragment_textures[16]{};
    vk::DescriptorSet empty_set{};
    uintptr_t allocations = 0;
};
vk::DescriptorSet retrieve_descriptor(VKContext &context, bool, uint16_t) {
    return vk::DescriptorSet{reinterpret_cast<VkDescriptorSet>(++context.allocations)};
}
'''
source += scene[start:end]
source += r'''
void binding_integration() {
    VKContext context;
    context.state.default_image.sampler = reinterpret_cast<VkSampler>(uintptr_t{1});
    context.state.default_image.view = reinterpret_cast<VkImageView>(uintptr_t{1});
    assert(retrieve_texture_descriptor(context, false, 0) == context.empty_set);
    assert(context.allocations == 0);
    for (int i = 0; i < 1000; ++i) {
        context.fragment_textures[0].sampler = reinterpret_cast<VkSampler>(uintptr_t{2});
        context.fragment_textures[0].imageView = reinterpret_cast<VkImageView>(static_cast<uintptr_t>(2 + i % 2));
        retrieve_texture_descriptor(context, false, 2); // also exercises fallback slot
    }
    assert(context.allocations == 2 && context.state.device.updates == 2);
    context.state.frame().texture_descriptors.clear();
    retrieve_texture_descriptor(context, false, 2);
    assert(context.allocations == 3 && context.state.device.updates == 3);
}
'''
source += r'''
struct CastedTexture {
    uint32_t cropped_height = 8, cropped_width = 8, cropped_y = 0, cropped_x = 0;
    int format = 1;
    struct { vk::Format format = vk::Format::eR8G8B8A8Unorm; } texture;
    vk::ComponentMapping components{};
};
bool matches(const CastedTexture &candidate, vk::Format vk_format, vk::ComponentMapping resulting_swizzle) {
    std::vector<CastedTexture> casted_vec{candidate};
    size_t i = 0;
    uint32_t height = 8, width = 8, start_sourced_line = 0, start_x = 0;
    int base_format = 1;
    return ''' + condition + r''';
}
int main() {
    binding_integration();
    CastedTexture candidate;
    assert(matches(candidate, vk::Format::eR8G8B8A8Unorm, {}));
    assert(!matches(candidate, vk::Format::eR8G8B8A8Srgb, {}));
    vk::ComponentMapping bgra{vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eA};
    assert(!matches(candidate, vk::Format::eR8G8B8A8Unorm, bgra));
    candidate.components = bgra;
    assert(matches(candidate, vk::Format::eR8G8B8A8Unorm, bgra));
    candidate.cropped_x = 1;
    assert(!matches(candidate, vk::Format::eR8G8B8A8Unorm, bgra));

    std::vector<uint8_t> guest(24, 0xA5), gpu(24);
    for (size_t i = 0; i < gpu.size(); ++i) gpu[i] = static_cast<uint8_t>(i);
    ColorSurfaceCacheInfo info{};
    info.stride_bytes = 12;
    info.original_width = 2;
    info.original_height = 2;
    info.format = RGBA;
    info.texture.format = vk::Format::eR8G8B8A8Unorm;
    info.swizzle = {vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA};
    info.data = {guest.data()};
    info.copy_buffer = std::make_unique<Buffer>(Buffer{gpu.data(), gpu.size()});
    VKSurfaceCache cache;
    cache.perform_post_surface_sync({}, &info);
    for (size_t row = 0; row < 2; ++row) {
        for (size_t i = 0; i < 8; ++i) assert(guest[row * 12 + i] == gpu[row * 12 + i]);
        for (size_t i = 8; i < 12; ++i) assert(guest[row * 12 + i] == 0xA5);
    }
    info.swizzle = bgra;
    cache.perform_post_surface_sync({}, &info);
    for (size_t row = 0; row < 2; ++row) {
        for (size_t pixel = 0; pixel < 2; ++pixel) {
            const size_t offset = row * 12 + pixel * 4;
            assert(guest[offset] == gpu[offset + 2]);
            assert(guest[offset + 2] == gpu[offset]);
        }
        for (size_t i = 8; i < 12; ++i) assert(guest[row * 12 + i] == 0xA5);
    }
    info.format = SCE_GXM_COLOR_BASE_FORMAT_U8U8U8;
    info.stride_bytes = 9;
    auto *last_written_surface = &info;
    Buffer copy_buffer{};
''' + allocation + r'''
    assert(copy_buffer.size == 24); // 3 host RGBA pixels per row, 2 rows.
    std::cout << "Production gamma/swizzle cache isolation and padded readback passed\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = pathlib.Path(tmp) / 'surface.cpp'
    binary = pathlib.Path(tmp) / 'surface'
    cpp.write_text(source)
    # Existing swizzle helper intentionally ignores identity/constant components.
    subprocess.run([sys.argv[1], '-std=c++20', '-Wall', '-Wextra', '-Werror', '-Wno-switch',
                    '-I' + str(repo / 'vita3k/renderer/include'),
                    '-I' + str(repo / 'vita3k/vkutil/include'),
                    '-I' + sys.argv[2],
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

context = (repo / 'vita3k/renderer/src/vulkan/context.cpp').read_text()
frame = context[context.index('void new_frame('):]
assert frame.index('waitForFences(') < frame.index('frame.texture_descriptors.clear()') < frame.index('frame.destroy_queue.destroy_objects()')
texture = (repo / 'vita3k/renderer/src/vulkan/texture.cpp').read_text()
assert 'state.frame().destroy_queue.add(sampler)' in texture
assert 'image_info.imageLayout != layout' in texture
scene = (repo / 'vita3k/renderer/src/vulkan/scene.cpp').read_text()
assert 'texture_descriptors.get_or_create(key' in scene
assert 'retrieve_texture_descriptor(context, true' in scene
assert 'retrieve_texture_descriptor(context, false' in scene
assert 'casted->texture.transition_to(cmd_buffer, vkutil::ImageLayout::SampledImage)' in surface
