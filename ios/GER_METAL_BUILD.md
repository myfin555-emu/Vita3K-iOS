# God Eater Resurrection isolated iOS build

This configuration is separate from the normal Vita3K iOS build.

Configure on macOS with Xcode:

    cmake -S . -B build-ger -G Xcode \
      -DCMAKE_SYSTEM_NAME=iOS \
      -DVITA3K_BUILD_IOS_UPSTREAM_CORE=ON \
      -DVITA3K_IOS_GER_ONLY=ON \
      -DVITA3K_IOS_GER_METAL_FOUNDATION=ON

Build:

    cmake --build build-ger --config Release --target Vita3KiOS

The bundle is named GodEaterResurrection.app and uses
org.vita3k.ger.a11.

The GE:R-only frontend accepts these known regional title IDs:
PCSG00719, PCSE00801, PCSB00874, PCSH00199.

The normal Vita3KiOS build remains unchanged when VITA3K_IOS_GER_ONLY is OFF.

The Metal foundation currently provides isolated resource, texture, shader,
pipeline and command objects. It is not selected by the live Vita3K renderer
yet; the live emulator remains Vulkan/MoltenVK so image correctness is not
sacrificed by a half-wired backend.
