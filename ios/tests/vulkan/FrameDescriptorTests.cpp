#include <renderer/vulkan/frame_descriptor.h>
#include <cassert>
#include <iostream>
using namespace renderer::vulkan;

int main() {
    FrameDescriptor frame;
    for (uintptr_t i = 1; i <= 4; ++i)
        frame.pools.push_back({vk::DescriptorPool(reinterpret_cast<VkDescriptorPool>(i)), 1});
    frame.sets.resize(4 * TEXTURE_DESCRIPTOR_PACK_SIZE);
    size_t destroyed = 0;
    const auto destroy = [&](vk::DescriptorPool pool) { assert(pool); ++destroyed; };
    frame.descriptors_idx = 256;
    frame.reset(10, destroy); // all four packs actually used
    assert(destroyed == 0 && frame.descriptors_idx == 0);
    frame.descriptors_idx = 1;
    frame.reset(129, destroy);
    assert(destroyed == 0); // 119 idle frames
    frame.descriptors_idx = 65;
    frame.reset(130, destroy);
    assert(destroyed == 2 && frame.sets.size() == 128 && frame.pools.size() == 2);
    frame.descriptors_idx = 1;
    frame.reset(249, destroy);
    assert(destroyed == 2); // second pack recently used
    frame.reset(250, destroy);
    assert(destroyed == 3 && frame.sets.size() == 64);
    frame.reset(1000, destroy);
    assert(destroyed == 3); // keep warm pack
    frame.pools.push_back({vk::DescriptorPool(reinterpret_cast<VkDescriptorPool>(5)), 2000});
    frame.sets.resize(128);
    frame.reset(1001, destroy);
    assert(destroyed == 3); // future timestamp cannot underflow
    FrameDescriptor color;
    color.sets.resize(16); // pools owned by state, not this object
    color.descriptors_idx = 1;
    color.reset(5000, destroy);
    assert(color.sets.size() == 16 && destroyed == 3);
    std::cout << "Descriptor high-water retention and retirement passed\n";
}
