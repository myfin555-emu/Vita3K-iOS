// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace renderer {
inline std::string ios_shader_cache_namespace(uint32_t features_mask) {
    return "ios-vk-features-" + std::to_string(features_mask);
}
} // namespace renderer
