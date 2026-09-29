# Interlock discard regression

This host test uses the production discard helper and the repository-pinned
glslang builder. It validates real SPIR-V and checks primary/secondary phase
termination, suppression of later writes, and reaching the interlock end.
A bounded interpreter checks only these fixture instructions, not GPU execution.

It also reproduces the old Metal source containing `simd_is_helper_thread` and
checks the replacement with and without Metal argument buffers. Use SPIRV-Cross
commit `6c09849fe88c48eaed08413aa022aaa136a3a057`, pinned by MoltenVK v1.4.2
(the iOS build's driver), for this comparison. Build its CLI with the default
HLSL/CPP/reflect backends enabled; disabling HLSL while enabling the CLI fails.

Requirements: CMake 3.22+, C++17 compiler, Python 3, SPIRV-Tools and the above
SPIRV-Cross CLI. Initialize `external/glslang`, then from the repository root:

```sh
cmake -S ios/tests/interlock -B build-interlock-tests \
  -DSPIRV_CROSS=/absolute/path/to/spirv-cross
cmake --build build-interlock-tests -j2
ctest --test-dir build-interlock-tests --output-on-failure
```

The complementary `ios/tests/shader` suite tests translation of synthetic guest
GXP programs through the actual compiler. Neither suite runs Apple's Metal
compiler or proves visual correctness, GPU timing, or sustained game FPS.
