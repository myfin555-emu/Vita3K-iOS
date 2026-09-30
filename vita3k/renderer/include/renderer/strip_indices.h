// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace renderer {

// Metal always treats the largest index as restart in strips/fans. GXM's
// non-restarting draws may use that value as a vertex. Convert only those rare
// draws to triangle lists; keep vertex IDs, winding and the provoking vertex.
template <typename Index>
bool expand_non_restarting_indices(std::span<const Index> indices, bool fan,
    std::vector<uint32_t> &triangles) {
    if (indices.size() < 3 || std::find(indices.begin(), indices.end(), std::numeric_limits<Index>::max()) == indices.end())
        return false;
    triangles.clear();
    for (size_t i = 2; i < indices.size(); ++i) {
        // Vulkan's default provoking vertex is i-2 for strips, i-1 for
        // fans. Rotate the odd strip triangle rather than swapping its first
        // two vertices, so flat attributes keep the same source vertex.
        triangles.push_back(indices[fan ? i - 1 : i - 2]);
        triangles.push_back(indices[fan || i % 2 ? i : i - 1]);
        triangles.push_back(indices[fan ? 0 : (i % 2 ? i - 1 : i)]);
    }
    return true;
}

} // namespace renderer
