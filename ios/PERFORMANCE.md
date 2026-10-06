# Tsubomi performance & stability guide

This document describes the **stability pack** (`perf/stability-pack`) and the
settings that keep gameplay smooth on non-jailbroken iPhones/iPads.

## What the stability pack does

1. **Thermal monitoring** – every 2 seconds during a game session the guest
   watchdog reads `NSProcessInfo.thermalState`. When the device moves to
   *serious* or *critical*, Tsubomi automatically:
   - lowers the resolution multiplier (down to 0.5× at critical)
   - turns off high-accuracy / surface-sync GPU paths
   - clamps anisotropic filtering to 1×
   - keeps async pipeline compilation on (reduces shader-compile stutter)

2. **Memory pressure** – the same sample tracks RSS and
   `os_proc_available_memory()`. Above ~85% pressure a GC hint is logged so
   jetsam kills are easier to diagnose; headroom is written to `tsubomi.log`.

3. **Frame pacing** – `FrameRateLimiter` uses a monotonic deadline schedule
   (sleep + short spin) so a single late frame does not cascade into multi-
   frame sleep debt.

4. **Recommended first-run defaults**
   - `async_pipeline_compilation = true`
   - `cpu_opt = true`
   - `shader_cache = true` / `texture_cache = true`
   - `high_accuracy = true` (until thermal scales it down)
   - `anisotropic_filtering = 4`
   - `resolution_multiplier = 1.0`
   - `v_sync = true`, FPS limit 60

## Recommended per-game settings

Apply these from **Library → long-press game → Settings** (per-title overrides).

| Title | Title ID (examples) | Resolution | High accuracy | Async pipelines | Aniso | Notes |
|-------|---------------------|------------|---------------|-----------------|-------|-------|
| Persona 4 Golden | PCSG00563 / PCSE00120 | 1.0× | On | On | 4 | Keep **double-buffer memory mapping OFF** (garbles models). |
| God Eater Resurrection | PCSA00026 | 1.0× | On | On | 4 | Benefits from surface-sync; thermal pack already optimises combat stutter. |
| VA-11 HALL-A | PCSB01012 / others | 1.0× | Off if hot | On | 2 | Light 2D title; lower aniso if device warms up. |
| Gravity Rush | PCSF00024 | 0.75–1.0× | On | On | 2 | GPU heavy; let thermal scaler drop res when *serious*. |
| Uncharted Golden Abyss | PCSF00001 | 0.75× | On | On | 2 | Prefer slightly lower res over stutter. |

### Global tips

- Enable **JIT** (StikDebug or equivalent) before launch — without JIT games refuse to boot and any partial run is extremely slow.
- Keep the phone cool (remove thick cases, avoid direct sun). Thermal *critical* will force 0.5× resolution.
- After a long session, force-quit and relaunch to drop shader/pipeline caches if memory pressure stayed high.
- Attach `tsubomi.log` when reporting freezes; the watchdog lines `iOS thermal=` and `iOS memory headroom=` show the state right before a problem.

## Developer notes

- Headers: `ios/include/vita3k_ios/PerformanceOptimizations.h`
- Implementation: `ios/src/PerformanceOptimizations.mm`
- Wired from the guest watchdog thread in `ios/src/UpstreamMain.cpp`
- Built by the upstream-core target in `ios/CMakeLists.txt`
