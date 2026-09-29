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
