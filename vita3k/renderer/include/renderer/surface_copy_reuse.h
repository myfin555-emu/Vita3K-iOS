// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace renderer {

// A snapshot of the active target predates the writes in that scene. Never
// carry it into a later scene, even if the target has not been rebound yet.
inline bool can_reuse_surface_copy(uint64_t copied_generation, uint64_t source_generation,
    uint64_t copied_scene, uint64_t scene, bool copied_from_active_target, bool active_target) {
    return copied_scene != 0 && copied_generation == source_generation
        && (copied_scene == scene || (!copied_from_active_target && !active_target));
}

} // namespace renderer
