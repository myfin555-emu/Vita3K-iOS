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
#include <cstdint>
#include <unordered_map>
#include <vulkan/vulkan_hash.hpp>

namespace renderer::vulkan {

struct TextureDescriptorKey {
    std::array<vk::DescriptorImageInfo, 16> images{};
    uint16_t count = 0;
    bool vertex = false;
    bool operator==(const TextureDescriptorKey &) const = default;
};

struct TextureDescriptorHash {
    size_t operator()(const TextureDescriptorKey &key) const {
        size_t hash = (key.count << 1) | key.vertex;
        for (uint16_t i = 0; i < key.count; ++i) {
            const auto value = std::hash<vk::DescriptorImageInfo>{}(key.images[i]);
            hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

// One cache per frame slot. Sets are immutable until that slot's fences have
// completed. This only reuses bindings: texture hashing/uploads still run on
// every bind so guest CPU writes remain visible.
class TextureDescriptorCache {
    std::unordered_map<TextureDescriptorKey, vk::DescriptorSet, TextureDescriptorHash> sets;

public:
    template <typename Create>
    vk::DescriptorSet get_or_create(const TextureDescriptorKey &key, Create create) {
        const auto it = sets.find(key);
        if (it != sets.end())
            return it->second;
        const auto set = create();
        // Bound CPU metadata in scenes with many unique combinations.
        if (sets.size() < 1024)
            sets.emplace(key, set);
        return set;
    }

    void clear() { sets.clear(); }
};

} // namespace renderer::vulkan
