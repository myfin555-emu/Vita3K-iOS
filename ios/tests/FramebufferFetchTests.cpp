#include <renderer/vulkan/framebuffer_fetch.h>

#include <cstdlib>
#include <iostream>

void require(bool value, const char *message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int main() {
    using namespace renderer::vulkan;
    require(needs_subpass_fetch_workaround(true, "Apple A11 GPU"), "A11 iOS workaround missing");
    require(!needs_subpass_fetch_workaround(false, "Apple A11 GPU"), "workaround must be scoped to iOS");
    for (auto name : { "Apple A12 GPU", "Apple A17 Pro GPU", "Apple M1", "Adreno" })
        require(!needs_subpass_fetch_workaround(true, name), "unaffected GPU forced into workaround");
    for (bool available : { false, true }) {
        for (bool accuracy : { false, true }) {
            for (bool workaround : { false, true }) {
                FeatureState features;
                features.support_shader_interlock = available;
                features.use_texture_viewport = false;
                features.support_unmapped_surface_sync = true;
                select_framebuffer_fetch(features, accuracy, workaround);
                const bool interlock = available && accuracy && !workaround;
                require(features.should_use_shader_interlock() == interlock, "wrong shader path");
                require(features.direct_fragcolor == !interlock, "subpass and storage-image paths must be exclusive");
                require(features.is_programmable_blending_supported(), "framebuffer blending must remain available");
                require(features.is_programmable_blending_need_to_bind_color_attachment() == interlock, "attachment binding does not match shader path");
                require(features.can_surface_sync() && !features.use_texture_viewport, "unrelated accuracy/sync options changed");
            }
        }
    }
}
