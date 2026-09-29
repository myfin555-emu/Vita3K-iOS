# iOS 16–26, JIT and game text input

## Build and deployment

Build the upstream core with **Xcode 26 / iOS 26 SDK**, targeting **iOS 16.0**.
`VITA3K_IOS_DEPLOYMENT_TARGET` is set before CMake initializes its toolchain and
is shared by the application and core. The `ios/triplets/arm64-ios.cmake`
vcpkg overlay builds dependencies for iOS 16.0 as well. Configure with
`-DVCPKG_OVERLAY_TRIPLETS="$PWD/ios/triplets"` and
`-DVITA3K_IOS_DEPLOYMENT_TARGET=16.0`. The GitHub workflows target iOS 16.0.
The SDK version is deliberately newer than the minimum deployment version.

The frontend uses `ObservableObject`/`Published` on all supported systems.
Liquid Glass is availability guarded and falls back to system material/buttons
on iOS 16, 17 and 18. The scroll-target carousel requires iOS 17; iOS 16 uses
the existing grid/list, including controller navigation. Sheet materials fall
back to the system sheet before iOS 16.4.

These are source-level compatibility changes, **not device certification**.
Building and installing on each OS/device remains necessary. OS compatibility
also does not guarantee that a particular Vita game runs.

## MoltenVK 1.4.2

CI and both local project generators install the official **1.4.2** static
XCFramework from a shared SHA-256 pin in `ios/cmake/MoltenVKVersion.cmake`.
Device uses `MoltenVK-ios.tar` (`ios-arm64`); Simulator uses `MoltenVK-all.tar`
(`ios-arm64_x86_64-simulator`). Headers are checked during CMake configuration
so a leftover 1.4.1 package fails early instead of silently building the old driver.

```sh
cmake -P .ci/install-moltenvk.cmake
# For the experimental Simulator target:
cmake -DMVK_PACKAGE=simulator -P .ci/install-moltenvk.cmake
```

The installer verifies the cached/downloaded archive, extracts to staging and
validates the slice and version before replacing the installed package. Local
generators now use the same iOS 16 deployment target as CI. The official 1.4.2
package requires iOS 15 or later, so this does not raise the application's floor.
The macOS desktop dependency is separate and retains its existing version.

### Existing 1.4.1 integration and changes

The previous library was an **unmodified official binary**, not a custom MoltenVK
source build. iOS-specific behavior lives in Vita3K's renderer:

| Area | Behavior with 1.4.2 |
| --- | --- |
| Static Vulkan entry point and Metal surface | Retained; no runtime Vulkan loader needed |
| `FULL_IMAGE_VIEW_SWIZZLE` | Removed from iOS layer settings: the driver marks it obsolete and ignored (already so in 1.4.1); image-view swizzle support remains driver-managed |
| `RESUME_LOST_DEVICE` | Retained for recoverable submission failures; it is not a fix for every GPU fault |
| Debug/verbose driver logging | Remains limited to debug builds |
| CPU/GPU RAM exchange | Keep DoubleBuffer when its required features exist; otherwise use staging-buffer surface readback; do not enable PageTable/ExternalHost |
| Dirty tracking | Preserve copying instead of fault-based buffer/surface tracking during iOS JIT use |
| Pipeline compilation | Preserve asynchronous compilation and existing worker policy |
| Pipeline disk cache | Validate vendor/device/driver UUID and header before loading; rebuild incompatible caches and preserve the empty cache if the driver rejects a payload |
| Texture lifetime / presentation | Preserve guest-page validation and locking during hash/upload, plus capability-derived swapchain extent |

Official 1.4.2 fixes cover swapchain recreation producing 1×1 drawables,
render-target channel corruption during transfers, cached Metal texture
invalidation, argument-buffer alignment/residency and subpass barriers. These
are relevant to this renderer; an FPS improvement or game crash fix requires
measurement on device. New sampler min/max support is limited to supported
Apple10 GPUs with iOS 26; it is not forced on older devices.

Keep the driver's supported defaults for Metal argument buffers, synchronous
queue submission and command-buffer prefill. Prefill modes trade speed, memory,
autorelease handling and synchronization behavior; changing them without a
profile can regress an emulator. Synchronous submission means encoding on the
calling thread, not waiting for the GPU to finish every submission.

Sources: [official 1.4.2 release](https://github.com/KhronosGroup/MoltenVK/releases/tag/v1.4.2),
[configuration reference](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/Docs/MoltenVK_Configuration_Parameters.md),
[obsolete swizzle field](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/API/mvk_private_api.h).

Device checks: cold/warm shader-cache launch after upgrading from 1.4.1,
Attack on Titan intro skip, scene transitions, DoubleBuffer on/off, surface
readback colors, rotation/backgrounding and steady-size swapchain behavior.
Compare frame times and peak memory with the same game/scene/settings. Linux
checks package integrity and portable code; Xcode linking and GPU execution
still require macOS/iPhone.

## JIT methods

Open **Settings → JIT**, or tap the JIT banner. Choose one acquisition method:

- **External debugger / JIT tool**: enable JIT for the displayed bundle ID/PID
  with a compatible external tool and return to Tsubomi.
- **TrollStore** (2.0.12+): install this app through TrollStore, select
  **TrollStore**, and tap **Prepare JIT**. This opens the official
  `apple-magnifier://enable-jit?bundle-id=…` URL. TrollStore's privileged helper
  attaches to the running app and detaches; return to Tsubomi for its memory
  probe to confirm readiness. Within this app's iOS 16+ range, TrollStore supports
  16.0–16.6.1, 16.7 RC (20H18), and 17.0, not 16.7 releases or 17.0.1/18/26.
  Without TrollStore, the same URL may open Apple's Magnifier; this is not JIT
  success. Re-enable JIT after each process restart.
- **StikDebug** (iOS 17.4+): **Prepare JIT** opens the documented
  `stikdebug://enable-jit` request with the running PID and bundle ID. On iOS 26
  the request includes the allocator's fixed `universal.js` script. Pairing,
  VPN and Developer Mode must already be configured in StikDebug.

The app checks its `get-task-allow` code-signing flag for external debugger
and StikDebug requests. Sideloading must preserve this entitlement; changing an
unsigned IPA's plist does not grant it. TrollStore uses its own privileged helper
and bypasses this local entitlement check. Opening any helper URL never marks
JIT ready. No additional private entitlements are added to the IPA.

| System / installation | Memory path and applicable acquisition tools |
| --- | --- |
| iOS 16 | Conventional executable mappings: compatible AltJIT, Xcode/LLDB, Jitterbug/JitStreamer, or existing executable-memory permission |
| iOS 17.0–17.3 | Conventional mappings; use an external tool supporting these releases, such as a compatible AltJIT/SideJITServer setup |
| iOS 17.4–18 | Conventional mappings; StikDebug or a compatible external debugger/enabler |
| iOS 26 | Conventional mappings when actually permitted; otherwise StikDebug (or an equivalent universal-protocol debugger) prepares the RX/RW region pool before detaching |
| Compatible TrollStore / jailbreak installation | Automatically use conventional JIT only when the process can allocate executable memory and perform the required protection transitions; no assumption based on installer name |

No single acquisition tool works on every release. Tools above grant the same
process capabilities, so the app does not need a separate allocator for each.
This change integrates external acquisition, TrollStore and StikDebug URL launching; it
**does not embed StikJIT**, import pairing secrets, install a VPN, or add an
app extension. Built-in StikJIT requires a separately signed helper process.
LiveContainer users must enable **Use LiveContainer's Bundle ID** in its settings.

For conventional JIT, readiness requires a successful RWX allocation and
RX → RW → RX transitions. The app permits debugger detachment after permission
is granted. The iOS 26 universal path requires a traced process while all
the configured number of cache regions are prepared (37 by default), then sends
the universal detach request.
The pool is reused across games; exhaustion without an attached script fails
allocation rather than issuing an unhandled breakpoint. Restarting the app
requires acquiring JIT again. The banner only clears after preparation succeeds.

References checked during implementation:

- [StikDebug supported versions and setup](https://github.com/StikDebug/StikDebug)
- [StikJIT universal protocol and URL integration](https://github.com/StikDebug/StikJIT/blob/main/INTEGRATION.md)
- [SideJITServer supported platforms](https://github.com/stossy11/SideJITServer)
- [Jitterbug pairing and debugger launch](https://github.com/osy/Jitterbug)
- [AltJIT instructions](https://faq.altstore.io/altstore-classic/enabling-jit/altjit)
- [TrollStore supported versions and JIT URL](https://github.com/opa334/TrollStore#url-scheme)
- [TrollStore helper attach/detach implementation](https://github.com/opa334/TrollStore/blob/main/RootHelper/jit.m)

## Native game keyboard

Vita3K-Plus at `c44cec34b3b376d3a4698e869625236f0e28a73b` uses
`vita3k/android/jni/ime.cpp`, `android_state.cpp`, `VitaInputConnection.java`
and its native IME overlay. It synchronizes the mobile editor with
`emuenv.ime`, shared by `SceIme` and `SceImeDialog`. Confirm/cancel results are
returned through the guest APIs, not injected as simulated controller presses.

`ios/src/IOSKeyboard.mm` implements that boundary using a native `UITextView`:

- Existing text, caret, keyboard type, return label and editor height come
  from the game's request. An opaque white UIKit editor with black text follows
  the system keyboard layout guide, in portrait and landscape. On iOS the GPU
  IME dialog and its competing controller input handler are suppressed; other
  common dialogs continue using the renderer.
- iOS owns composition, selection, paste, deletion and hardware keyboard input.
  Marked text stays in UIKit until committed, so Japanese/Chinese composition
  is not repeatedly replaced by the game's committed text.
- Text and caret positions use UTF-16; limits do not split surrogate pairs.
- Confirm and the iOS Return/Enter key write `SceImeDialog` results, or queue
  `SCE_IME_EVENT_PRESS_ENTER` in the shared IME state. `sceImeUpdate` delivers
  the final text/caret update before that terminal event without waiting for a
  UIKit frame. Cancel queues `PRESS_CLOSE` and respects dialogMode.
- Return confirms even for multiline requests; pasting multiline text is still
  supported. Selecting a marked-text composition candidate does not confirm.
- Confirmation/cancellation restores the controller and keeps the keyboard
  dismissed for the current session, including while guest callbacks run.
  Only a new game IME session reopens it; an empty event slot or a text/caret
  callback does not imply a request to start editing again.
- The controls disappear while editing. Session generations reject callbacks
  belonging to a previous keyboard request; game exit removes the editor
  before guest state is destroyed.
- `sceImeSetText` and `sceImeSetCaret` synchronize game-initiated changes back
  to the editor. IME callbacks run without holding the editor mutex.

## Validation

Portable boundary tests:

```sh
cmake -S ios/tests -B build-native-tests
cmake --build build-native-tests
ctest --test-dir build-native-tests --output-on-failure
```

The upstream IPA workflow runs these tests before the Xcode build and targets iOS 16.
Linux cannot compile UIKit/SwiftUI or validate device JIT. Before release, run:

1. Install the IPA on iOS 16, 17, 18 and 26; inspect onboarding, library,
   settings, controls and rotation. Confirm iOS 16 grid/controller navigation.
2. On 16/17/18, enable conventional JIT, detach, launch and switch games.
   On 26, exercise universal preparation and detach, then switch games.
3. Try with no JIT, missing get-task-allow, missing StikDebug, and an interrupted
   universal script. Verify the app stays gated and reports the failure.
4. Exercise both `sceImeOpen` and `sceImeDialogInit`: initial text, paste,
   Thai/Japanese/Chinese, emoji at the limit, selection, backspace, hardware
   keyboard, numeric keyboard, multiline Return, Confirm and Cancel. Confirm
   the final typed character reaches the game before Enter. Verify the white
   editor is readable in light/dark mode and both orientations.
5. Test repeated Enter in a game that keeps SceIme open: send exactly one
   confirmation and keep the keyboard closed. Open another field (for example,
   Name then Codename in God Eater) and confirm it gets a fresh working editor.
   Check game-driven text/caret changes during editing, game abort while composing,
   background/foreground, and game exit while the keyboard is shown.
   Noncancelable dialogs must ignore Cancel. The native IME replacement does
   not address unrelated black rectangles or texture artifacts in game graphics.
6. On a supported TrollStore device, install the IPA through TrollStore 2.0.12+,
   request JIT from Settings, return, launch and switch games. Restart the app
   and request JIT again. With TrollStore absent or its request rejected, verify
   opening Magnifier does not clear the JIT banner or enable game launch.

Physical-device and Xcode results are pending; portable tests alone do not
establish that all JIT tools or games work on all four OS families.

## Firmware and advanced settings

Setup, game import and launch require **PSVUPDAT.PUP** (main firmware under
`vs0`) and **PSP2UPDAT.PUP** (fonts under `sa0`). Preinstall content under `pd0`
is optional and has no onboarding page or launch gate.

Settings now expose these core options globally and per game:

- Modules: Automatic, Automatic + manual, Manual, matching Vita3K-Plus's
  `ModulesMode` values. Selections come from installed `vs0/sys/external/*.suprx`.
  Changes take effect on the next launch, with the running game's module policy
  preserved until restart.
- Audio volume (0–100%) and texture upload reuse. Disabling Texture cache
  forces Vulkan texture uploads even when the texture hash is unchanged.

### JIT threads and memory

Global settings accept whole numbers and require a full app restart:

| Setting | Range | Default | Config key |
| --- | --- | --- | --- |
| JIT threads | 1–64 | 37 | `ios-jit-threads` |
| JIT RAM per thread (MB) | 16–128 | 16 | `ios-jit-cache-mb` |
| Emulated RAM budget (MB) | 512–2048 | 640 | `ios-emulator-ram-mb` |

The supplied Attack on Titan log ends with **37 live guest threads** (1 running,
36 waiting). Its 191 cache-allocation events are cumulative, covering 190 guest
IDs; every recorded allocation is 16 MB. These observations determine the
37 × 16 defaults, not a claim of 37 simultaneously executing host cores. The
CPU core selector has been removed; existing optimization preferences persist.

Guest threads now share a bounded pool of reusable Dynarmic engines. Each guest
keeps its own ARM/VFP registers and CP15 state. A slot is released at a syscall
or after a bounded instruction slice, before dispatching any blocking HLE work.
Cache invalidation reaches every slot. Thus **1 × 128 MB** permits one engine
and one 128 MB executable cache, without removing guest threads. It can reduce
parallelism; increasing either field does not guarantee better performance.
Conventional JIT allocates used slots lazily; universal JIT prewarms the selected
number before debugger detach. Increasing the pool/cache can exceed device RAM.

MB means 1,048,576 bytes. The 640 MB guest allocation budget uses the requested
512 + 128 total. It limits committed guest allocations separately from JIT,
returns capacity on free, and rejects requests beyond the limit. Following the
Vita3K-Plus allocator, the emulator still reserves its **4 GB virtual address
space** and commits pages on demand; reserving addresses does not consume 4 GB
of physical RAM. This is not a process memory cap or two physical Vita RAM banks:
GPU textures/staging, JIT metadata, writable aliases and app overhead are extra.
`MAP_JIT` alone does not grant executable-memory permission on iOS; the existing
verified conventional/universal acquisition paths remain necessary.

Portable regression tests live under `ios/tests`. An optional host integration
suite under `ios/tests/jit_pool` builds the actual CPU and memory sources with
Dynarmic and the iOS pool enabled; only logging/disassembly are adapted. It
requires populated Dynarmic submodules and Boost headers. Run CMake configure,
build and CTest for that directory. This does not validate Apple ARM64 memory
permissions, SwiftUI, GPU performance, or game compatibility on an iPhone.

## Movie skip crash investigation

The supplied Attack on Titan (PCSE00812) log was matched to build `497f193` and
symbolicated using its CI IPA. The faulting stack reads:
`hash_texture_data` → `TextureCache::cache_and_bind_texture` →
`vulkan::sync_texture` → renderer command processing, immediately after movie
worker exit/decoder flush. This identifies the faulting code; an on-device
reproduction is still required to confirm the fix.

On iOS, texture hashing and staging upload now share the guest allocation lock
with `free()`, check every source page, and skip invalid/freed texture sources.
Palette and later mip/face reads are checked too. This closes the window where
a queued video texture can be decommitted while the renderer reads it. The
Vulkan surface-size comparison also uses the same capability-derived extent
as swapchain creation, avoiding repeated rebuilds when SDL and Metal report
different pixel sizes. No input throttling or global GPU idle was added.

Device verification: repeatedly skip the intro, switch scenes and games,
background/foreground and rotate; confirm no texture-read fault and no repeated
swapchain rebuild at a stable size. Also verify module override/reset, volume,
texture-cache toggle, cache persistence after restart, and setup without `pd0`.
Portable tests cover invalid/freed page ranges and overflow, but cannot certify
UIKit, Vulkan, or game behavior on device.
