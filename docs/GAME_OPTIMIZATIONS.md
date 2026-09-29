# Game optimization experiments

The `feature/god-eater-resurrection-optimization` branch contributed the
following experimental files. They are preserved in the combined branch; no
on-device performance measurements were supplied with the merge.

## Configuration example

`vita3k/config/config_PCSA00026.xml` is the profile supplied by the source branch.
Its title ID and settings have not been verified against an installed game.
Check the title ID shown in your game library before using it.

The loader reads `<config_path>/config/config_<app_path>.xml` (see
`vita3k/config/src/settings.cpp`). A file in the source tree is not automatically
installed or applied. Back up an existing custom configuration before manually
copying a profile, and use the game's actual app path for its filename.

The profile requests Vulkan, high accuracy, surface synchronization, asynchronous
pipeline compilation, and 4x anisotropic filtering. Compatibility, visual quality,
and frame rate require device testing. The loader only reads supported attributes;
extra XML fields do not add runtime features.

## Helper code

- `renderer/surface_cache.h` provides a standalone `SurfaceChangeTracker` with
  hash-set membership and per-frame clearing. The current Vulkan surface cache
  still uses its existing vector; this helper is not connected to rendering.
- `vita3k_ios/PerformanceOptimizations.h` provides a standalone frame limiter.
  It is not called by the emulator's frame loop.
- `MemoryMonitor` and `ThermalThrottleManager` are declarations without
  implementations. Memory-pressure handling, garbage collection, and automatic
  thermal scaling are not provided by these declarations.

These files do not establish any FPS improvement or device compatibility claim.
Integrating them needs separate renderer, lifecycle, and physical-device testing.

## Profiling

Tracy is a build option (`VITA3K_ENABLE_TRACY`), disabled by default. The external
CMake configuration enables instrumentation for Debug/RelWithDebInfo when opted
in. There is no runtime settings switch introduced by this branch. Use measured
traces and reproducible scenes before changing defaults or reporting speedups.
