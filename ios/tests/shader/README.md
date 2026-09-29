# Shader regressions for the iOS Plus port

This optional host suite compiles the **actual** GXM and shader libraries with
repository-pinned glslang, SPIRV-Cross, fmt, spdlog and Boost. It does not boot the
emulator or require game files. The only replacement is the optional shader CLI
file reader (`FileReader.cpp`, std::ifstream instead of SDL). Production logging
and Boost path code are compiled.

Requirements: CMake 3.22+, C++20 compiler, Python 3, SPIRV-Tools (`spirv-val` and
`spirv-dis`). On Amazon Linux, `sudo dnf install spirv-tools` supplies the latter.

From the repository root:

```sh
git submodule update --init --depth 1 external/glslang external/SPIRV-Cross external/fmt external/spdlog external/boost
cmake -S ios/tests/shader -B build-shader-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build-shader-tests -j 4
ctest --test-dir build-shader-tests --output-on-failure
```

Tests also compare raw F16/F32 attribute shaders with scaled/RGB capabilities
enabled and disabled: their register bits must be identical, while typed U16
attributes still receive numeric conversion. All four cases are translated to
iOS MSL and validated as SPIR-V.

Tests cover synthetic GXP uniform layouts, USSE predicates/branches, base-2
complex arithmetic, conditional vector moves, integer instruction repetition,
initialized registers, signed GPU address addition and mapped byte/half/word
loads/stores. Generated Vulkan 1.0 SPIR-V is validated with SPIRV-Tools. The GXP
fixtures are also translated to iOS Metal source by pinned SPIRV-Cross. The
shader target enables the iOS translation paths. Predicated and unconditional
interlock discard fixtures verify that native discard remains available outside
that path and that interlock shaders do not emit helper-thread queries.
`ValidateSpirv.py` evaluates the small straight-line address fixture against
independent native 64-bit sums; it is not a general shader or GPU emulator.

This does **not** run Apple's Metal compiler, execute shaders on an iPhone,
validate Vulkan synchronization on a GPU, or establish game compatibility/FPS.
The ordinary dependency-free suite remains `cmake -S ios/tests -B build-tests`.

### Unmapped uniform data

Enable `-DVITA3K_EXECUTE_SHADER_TESTS=ON` when Vulkan headers, a loader and a
compute driver are installed. `uniform_execution_tests` executes a module built
by the production uniform helpers, checks all byte alignments of a 32-byte
input, and checks packed 8/16-bit loads of 1–16 components. The reference uses
CPU byte copies and verifies untouched register lanes, including buffer tails.
It can run on a software Vulkan driver; it does not establish iPhone FPS.
