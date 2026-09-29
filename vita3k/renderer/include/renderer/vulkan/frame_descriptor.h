// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include <vkutil/vulkan.h>

namespace renderer::vulkan {

inline constexpr uint32_t TEXTURE_DESCRIPTOR_PACK_SIZE = 64;

struct FrameDescriptor {
    std::vector<vk::DescriptorSet> sets;
    uint32_t descriptors_idx = 0;
    struct Pool {
        vk::DescriptorPool handle;
        uint64_t last_used;
    };
    // Texture pools belong to just this frame slot. Color pools are managed
    // by VKState and leave this vector empty.
    std::vector<Pool> pools;

    // Caller has waited for this slot's GPU fences and CPU readbacks.
    template <typename Destroy>
    void reset(uint64_t timestamp, Destroy destroy) {
        const size_t used = (descriptors_idx + TEXTURE_DESCRIPTOR_PACK_SIZE - 1) / TEXTURE_DESCRIPTOR_PACK_SIZE;
        for (size_t i = 0; i < std::min(used, pools.size()); ++i)
            pools[i].last_used = timestamp;
        // Keep one warm pack; release a high-water mark only after 120 idle
        // frames, avoiding allocation churn when a menu briefly closes.
        while (pools.size() > 1 && pools.size() > used) {
            const auto &pool = pools.back();
            if (timestamp < pool.last_used || timestamp - pool.last_used < 120)
                break;
            destroy(pool.handle);
            pools.pop_back();
            sets.resize(pools.size() * TEXTURE_DESCRIPTOR_PACK_SIZE);
        }
        descriptors_idx = 0;
    }
};

} // namespace renderer::vulkan
