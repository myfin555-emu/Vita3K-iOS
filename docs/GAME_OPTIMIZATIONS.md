# Game-Specific Optimization Guide

## God Eater Resurrection (PCSA00026)

### Overview
God Eater Resurrection is a heavy action RPG with complex shader workloads, high-poly character models, and intense particle effects. On iOS/MoltenVK, it requires careful tuning to maintain 60 FPS.

### Configuration Profile
Located at: `vita3k/config/config_PCSA00026.xml`

**Key Settings:**
- **High Accuracy Mode**: `true` - Bypasses fast-path rendering shortcuts that cause visual glitches in boss fights
- **Async Pipeline Compilation**: `true` - Prevents frame stuttering during shader compilation
- **Anisotropic Filtering**: `4` - God Eater has heavily textured environments; level 4 is optimal balance
- **Texture Cache**: `true` - Absolutely critical; texture misses cause severe performance drops
- **Shader Cache**: `true` - Rebuild cache if experiencing graphical artifacts

### LLE Module Configuration
The following low-level modules provide optimal compatibility:

```
sceJobQueue     - Handles multi-threaded job system (God Eater heavily uses this)
sceAudio        - Audio playback/mixing
sceAvplayer     - Video codec for intro cinematics
sceSsl          - Network/online multiplayer support
```

### Performance Tuning

#### iPhone/iPad Pro Models
| Device | Resolution | Notes |
|--------|-----------|-------|
| iPhone 13/14 | 1.0x | Maintains 60 FPS in most areas |
| iPhone 14 Pro | 1.25x | Test 1.25x if device has 6GB+ VRAM |
| iPad Air 5 | 1.5x | Can handle higher multiplier |
| iPad Pro M1 | 1.75x | Maximum recommended multiplier |

#### Critical Performance Hotspots
1. **Monster AI Update Phase** (~frames 2-8 of engagement)
   - CPU-intensive pathfinding and behavior tree evaluation
   - May cause 5-10ms spikes even on high-end device
   
2. **Particle Effect Storms** (boss super attacks)
   - GPU-limited; shader bottleneck
   - Async compilation prevents stalls
   
3. **Character Model Rendering** (4-player co-op)
   - Vertex buffer updates and skinning
   - Surface sync overhead is ~10% of frame time

### Troubleshooting

#### Issue: Garbled/Missing Character Models
**Cause:** Memory mapping corruption  
**Fix:** Edit `config_PCSA00026.xml`:
```xml
<gpu ... memory-mapping="single-buffer" .../>
```
If still broken, try disabling surface sync:
```xml
<gpu ... disable-surface-sync="true" .../>
```

#### Issue: Flickering Textures or Missing Shadows
**Cause:** Shader cache is stale or textures aren't syncing  
**Fix:** Clear shader/texture cache:
1. Delete `vita3k/cache/shader_cache/`
2. Restart emulation
3. Let first run rebuild cache (15-30 sec)

#### Issue: Frame Drops During Multi-Monster Battles
**Cause:** JIT cache insufficient or surface sync stalls  
**Fix:** Enable async pipeline compilation (already enabled in config) and verify:
```xml
<gpu async-pipeline-compilation="true" />
```
If still dropping frames, try disabling high accuracy:
```xml
<gpu high-accuracy="false" />
```
Note: May lose visual fidelity in some cutscenes.

#### Issue: Audio Crackling/Stuttering
**Cause:** Audio backend mismatch  
**Fix:** Ensure audio module is LLE:
```xml
<lle-modules>
    <module>sceAudio</module>
</lle-modules>
```

### Surface Sync Optimization (iOS-Specific)

God Eater Resurrection is sensitive to MoltenVK surface synchronization. The game frequently reads back render targets to CPU (for UI rendering and gameplay logic).

**Recommended Settings:**
- `disable-surface-sync="false"` - Enable sync to prevent garbled surfaces
- `double_buffer="true"` (in iOS struct) - Use GPU memory mapping when available

**Why:** MoltenVK doesn't have true hardware surface mapping like Vulkan on PC. Disabling sync saves bandwidth but causes visual artifacts. The staging-buffer fallback is slower but more reliable.

### Performance Monitoring

Use Tracy profiler to identify remaining bottlenecks:
1. In emulator settings, enable Tracy
2. Launch God Eater Resurrection
3. Connect Tracy server: `localhost:8086`
4. Profile 1 full encounter (60 frames)

**Expected Frame Budget (60 FPS):**
- CPU (emulation): 8-12ms
- GPU (rendering): 12-14ms
- Surface sync: 1-3ms
- Audio: <1ms

If any category exceeds budget, report on GitHub with Tracy trace file.

### Known Limitations

❌ **Not Fixable:**
- Online multiplayer may have network latency issues (platform limitation)
- 4-player co-op will not sustain 60 FPS on iPhone 13 mini

✅ **Can Be Improved:**
- Rendering artifacts in certain weather effects (pending MoltenVK updates)
- Particle count scaling (can reduce in future config)

---

## Adding Configurations for Other Games

### Template
```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- Game Name (TITLEID) - Description -->
<config>
    <core modules-mode="1">
        <lle-modules>
            <!-- Add LLE modules as needed -->
        </lle-modules>
    </core>
    
    <cpu cpu-opt="true"/>
    <gpu backend-renderer="Vulkan" 
         high-accuracy="false" 
         async-pipeline-compilation="true"
         ... />
    <audio audio-backend="SDL" audio-volume="100" enable-ngs="true"/>
    <system ... />
    <emulator ... />
    <debug ... />
    <network ... />
</config>
```

### Steps to Add New Game Config
1. Find game's Title ID (TITLEID) from game info screen
2. Create `vita3k/config/config_TITLEID.xml`
3. Start with defaults from `vita3k/config/include/config/state.h`
4. Test and adjust settings
5. Document findings in this guide
6. Submit PR with config + documentation

---

*Last Updated: 2026-09-28*  
*Maintainer: Vita3K iOS Team*