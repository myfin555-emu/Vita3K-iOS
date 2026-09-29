#pragma once

#include <features/state.h>

#include <string_view>

namespace renderer::vulkan {

// The A11 Metal compiler rejects the helper-invocation operation emitted for
// storage-image interlock shaders. Keep framebuffer fetch on the existing
// subpass-input path on that device, including when high accuracy is selected.
inline bool needs_subpass_fetch_workaround(bool ios, std::string_view device_name) {
    return ios && device_name == "Apple A11 GPU";
}

inline void select_framebuffer_fetch(FeatureState &features, bool high_accuracy, bool force_subpass) {
    features.support_shader_interlock = features.support_shader_interlock && high_accuracy && !force_subpass;
    features.direct_fragcolor = !features.support_shader_interlock;
}

} // namespace renderer::vulkan
