# Vulkan surface and UI binding regressions

This optional host suite needs CMake 3.22+, a C++20 compiler, Python 3,
Vulkan headers/loader, a graphics-capable Vulkan device (Mesa lavapipe works),
and `VK_LAYER_KHRONOS_validation`.

On Amazon Linux the dependencies are available with:

```sh
sudo dnf install cmake vulkan-headers vulkan-loader-devel vulkan-validation-layers mesa-vulkan-drivers
```

From the repository root:

```sh
git submodule update --init --recursive external/VulkanMemoryAllocator-Hpp external/boost external/fmt
cmake -S ios/tests/vulkan -B build-vulkan-tests -DCMAKE_BUILD_TYPE=Debug \
  -DVulkan_INCLUDE_DIR="$PWD/external/VulkanMemoryAllocator-Hpp/Vulkan-Headers/include"
cmake --build build-vulkan-tests -j 2
ctest --test-dir build-vulkan-tests --output-on-failure
```

CTest prefers Mesa lavapipe when installed, discovering either `lvp_icd.json`
or `lvp_icd.<architecture>.json`. It sets the selected manifest for the Vulkan
test process so a stale driver path inherited from CI cannot hide the driver.
To choose another installed driver, configure with
`-DVITA3K_VULKAN_TEST_ICD=/absolute/path/to/driver.json`. Without lavapipe or an
explicit override, the suite uses the Vulkan loader's normal selection.

The build compiles each shared Vulkan helper as the first include, followed by
the production Vulkan/VMA wrapper. Aggregate and designated-initializer checks
catch Vulkan-Hpp configuration differences without injecting test-only macros.
The suite uses the pinned VMA-Hpp Vulkan headers. To check the iOS MoltenVK
headers on the host as well:

```sh
cmake -P .ci/install-moltenvk.cmake
cmake -S ios/tests/vulkan -B build-moltenvk-header-tests -DCMAKE_BUILD_TYPE=Debug \
  -DVulkan_INCLUDE_DIR="$PWD/build-deps/moltenvk/MoltenVK/MoltenVK/include"
cmake --build build-moltenvk-header-tests -j 2
ctest --test-dir build-moltenvk-header-tests --output-on-failure
```

Run both configurations before the IPA build. Enabling this CI gate requires
installing the companion `vulkan-checks.yml` workflow and adding its reusable
job to `unsigned-ipa.needs` in `ios-upstream.yml`. Workflow installation requires
GitHub workflow write permission; the source/test fix can be pushed separately.

The vertex-stream test checks Metal stride expansion for the logged UInt4/12-byte
case, interleaved attribute offsets, overlapping guest records, constant bindings
and zero-padded short final records.

The C++ test uses the production dependency and descriptor-cache helpers. It
creates each render-pass mode on the Vulkan device with validation enabled,
checks attachment/storage/sampling dependency coverage, and checks immutable
binding reuse and cache invalidation. The 10,000-binding alternating fixture
requires two descriptor creations, rather than one per binding change. This is
an operation-count regression, not an FPS benchmark.

The Python test compiles production surface matching, row readback/swizzle and
descriptor-binding code with small host fixtures. It checks sRGB/linear and
swizzle separation, guest row-padding preservation, RGB24 staging allocation,
fallback texture slots, and binding reuse. Only emulator state, allocation and
driver-update boundaries are replaced; texture payloads remain synthetic.

These checks do not execute game shaders, validate Apple's Metal compiler,
exercise the SwiftUI HUD, or prove rendering/FPS on iPhone. Run Gravity Rush
from the opening through gameplay and compare God Eater's 2D menu open/closed
on the same device/settings. Capture fresh logs and screenshots for each title.
The ordinary portable suite remains `cmake -S ios/tests -B build-native-tests`.

## Readback lifetime and upload memory regressions

`renderer_lifetime_tests` compiles the production surface retirement, readback
queue, wait worker and frame reuse functions with controlled GPU fences. It
checks that extent/format changes discard old staging/blit/conversion resources,
CPU readback precedes notifications and frame fence reset, finish callbacks
wait for GPU completion, and queue cancellation releases waits without allowing
a delayed CPU copy to access a recycled surface. Only the driver/state boundary
is replaced; the lifecycle functions run unchanged (the local device variable
uses the fake driver type). The baseline surface retirement reproducer is:

```sh
python3 ios/tests/vulkan/RendererLifetimeTests.py /usr/bin/g++ \
  external/VulkanMemoryAllocator-Hpp/Vulkan-Headers/include --baseline
```

`--baseline` reads HEAD, so use it before committing the fix or against the old
revision. The vertex test writes into sentinel-guarded destination spans and
checks exact overlapping payload/tail bytes. `frame_descriptor_tests` verifies
that recently used packs remain live, idle excess packs retire after 120 frames,
and one warm pack remains. Texture pools are now local to a frame slot; its
immutable binding cache is cleared only after GPU/CPU completion. To additionally compile the real `vkutil/src/objects.cpp` mapped upload
implementation, initialize `external/spdlog` and configure with
`-DVITA3K_COMPILE_UPLOAD_OBJECTS=ON`. This opt-in check runs locally for both
header versions; the lightweight CI workflow fetches only its existing header
dependencies. The native IPA build compiles the implementation normally.

These tests establish correctness and operation-count changes, not device FPS
or process RAM measurements. In particular, shader compression was already
enabled in the iOS MoltenVK configuration and is not newly added here.

## sRGB storage and presentation

`vulkan_surface_tests` now also uses the production color-surface view helper
with a Vulkan 1.0 instance and explicitly enabled `VK_KHR_maintenance2`, matching
the renderer's extension setup. It renders a clear through an sRGB attachment,
executes a typed rgba8 storage shader that swaps red/blue, and checks the encoded
readback bytes and linear alpha. The same allocation is reused as sRGB, linear,
and sRGB again; sampling/presentation views are created with restricted usage.
No game shader or Metal execution is implied. `--legacy-srgb-storage` reproduces
the old image/view creation on lavapipe and intentionally fails validation.

The small compute shader is provided as SPIR-V assembly and embedded words so
the suite needs no shader compiler package. To verify/rebuild its payload:

```sh
spirv-as --target-env vulkan1.0 ios/tests/vulkan/SrgbStorageRoundTrip.spvasm -o /tmp/srgb-roundtrip.spv
spirv-val --target-env vulkan1.0 /tmp/srgb-roundtrip.spv
```

`SrgbStorageRoundTrip.h` contains the resulting little-endian uint32 words.
`surface_view_routing_tests` compiles the actual attachment/sample view cache,
presentation selection, descriptor image selection and swizzle function with a
recording Vulkan creation boundary. It checks that sRGB, raw storage and
presentation swizzle cannot alias the wrong view and repeated use is cached.
The retirement test additionally checks both attachment view framebuffer keys.
