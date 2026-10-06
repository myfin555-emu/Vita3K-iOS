# Tsubomi performance & stability guide

## Stability pack (`perf/stability-pack`)

1. **Thermal / memory monitoring** during play (watchdog samples every ~2s).
2. **Frame pacing** with monotonic deadlines (less micro-stutter).
3. **Surface-sync readback coalescing** — intermediate GPU→CPU post-syncs are
   dropped while one is in flight. This is the main fix for **God Eater
   Resurrection** menu FPS (settings / quest UI). Rage Burst 2 was already fine.

## God Eater Resurrection menu FPS

**Symptom:** Lobby / combat smooth after first shader compile, but opening the
in-game settings or quest-accept UI tanks FPS until the menu closes.

**Cause:** Resurrection redraws **linear color surfaces** every menu frame.
Surface sync queued a blocking wait on the previous readback → serialized the
wait-queue. Not the Tsubomi virtual-pad overlay.

**Fix (in this branch):** `VKSurfaceCache::queue_post_surface_sync` skips
in-flight intermediates (`SurfaceReadback::ready()`).

**Recommended per-game settings** (Library → long-press → Settings):

| Knob | Value |
|------|--------|
| Resolution | 1.0× |
| High accuracy | On |
| Async pipelines | On |
| Anisotropic | 2 |
| Surface sync | leave enabled (renderer fix handles menu thrash) |

Optional XML profiles in-tree (copy to device `config/config_<TitleID>.xml`):
- `vita3k/config/config_PCSA00026.xml` (US)
- `vita3k/config/config_PCSB00874.xml` (EU)

Validation layer must stay **off** in those profiles.

## Other titles

| Title | Notes |
|-------|--------|
| God Eater 2 Rage Burst | Usually smooth in menus; no special surface-sync thrash. |
| Persona 4 Golden | Keep double-buffer memory mapping **off**. |
| VA-11 HALL-A | Light; aniso 2 is enough. |

## Global tips

- JIT required (StikDebug etc.) before boot.
- Keep the device cool; thermal *critical* forces 0.5× resolution via the watchdog.
- Attach `tsubomi.log` when reporting issues (`iOS runtime pressure:` lines help).
