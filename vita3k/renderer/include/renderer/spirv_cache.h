// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace renderer {

// Check framing before handing cached bytes to a driver. Semantic validation
// remains the compiler/driver's job; this rejects truncated headers/instructions.
inline bool valid_spirv_cache(std::span<const uint32_t> words) {
    if (words.size() < 5 || words[0] != 0x07230203 || words[3] == 0 || words[4] != 0)
        return false;
    std::size_t offset = 5;
    while (offset < words.size()) {
        const uint32_t count = words[offset] >> 16;
        if (count == 0 || count > words.size() - offset)
            return false;
        offset += count;
    }
    return offset > 5;
}

} // namespace renderer
