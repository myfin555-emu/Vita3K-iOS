# God Eater Resurrection native GXM/Metal iOS build

This configuration builds a separate GE:R appliance product. It selects the
real `renderer::Backend::Metal` path only for the GE:R build; the normal
Vita3K iOS build continues to use its existing backend.

Configure on macOS with Xcode:

    cmake -S . -B build-ger -G Xcode \
      -DCMAKE_SYSTEM_NAME=iOS \
      -DVITA3K_BUILD_IOS_UPSTREAM_CORE=ON \
      -DVITA3K_IOS_GER_ONLY=ON

Build:

    cmake --build build-ger --config Release --target Vita3KiOS

The bundle is named `GodEaterResurrection.app` and uses
`org.vita3k.ger.a11`.

The GE:R-only frontend accepts these known regional title IDs:
PCSG00719, PCSE00801, PCSB00874, PCSH00199.

The native renderer path is:

    GE:R guest
      -> SceGxm command stream
      -> renderer::Backend::Metal
      -> Metal GXM context/resources
      -> MTLRenderPipelineState / MTLCommandBuffer
      -> CAMetalLayer

GXP shaders are translated through the existing SPIR-V recompiler and
SPIRV-Cross MSL backend. Texture/sampler state, uniform buffers, vertex
streams, render targets, depth/stencil, blending, surface synchronization,
and presentation are owned by the Metal backend.

The A11 GE:R correctness profile remains active: high accuracy stays enabled
and surface synchronization is not disabled.

Physical Xcode/A11 execution is still required to validate end-to-end gameplay;
this document does not claim a device runtime result.
