# Spike S1: GX graphics backend (P1-12)

**Owner:** runtime-gc-wii
**Date:** 2026-10-08
**Time-box:** 2 weeks (P1-12). This report is the desk study plus the probes that could run without fetching new dependencies. The render check on synthetic scenes is moved to a gate at the start of P1-14 (§8), still inside the time-box.
**Inputs:** ADR 0007 (§4–6 and the S1 brief), ADR 0008, ADR 0009, `docs/titles/mp4.md` §8–10, Phase 1 plan (P1-12, P1-14, E6, E7, R6).
**Output:** a recommendation for ADR 0007 §6. The proposed text is `docs/adr/0007-s6-proposed-amendment.md`, for architect to review and fold in.

## 1. Recommendation

**Adopt aurora's GX front end and its Dawn/WebGPU renderer, plus its MTX sources, without `aurora_core` (option a).** Its OS, VI, PAD, DVD, CARD, SI, window, input and ImGui parts are not used. Three things come with it:

- **A host shim of our own** in `runtimes/gc_wii/gx/` that replaces `aurora_core`. It covers about 20 symbols: the window queries, device init without a surface, VI framebuffer size, config paths and logging. It also adds **EFB→`cpu_rgba8` readback** and the **null backend**.
- **A small patch set** against the pin (§6), mainly for determinism isolation.
- **A gate at the start of P1-14** (§8). Dawn must build pinned on both OSes, and synthetic scenes must render headless with readback. If that fails, the fallback is **option (c)**: keep aurora's backend-independent front end (GX API → FIFO → command processor) and write our own renderer on SDL_GPU behind it. A full rewrite (b) is not recommended in any case.

**Pin:** the Party Board pin, `514339438178ef2bed1b14e5149d90ece0c6e0cc`. Party Board's game code is compiled against this exact header set. A later bump is worth planning (§7), because upstream has since fixed two of our issues and one of Party Board's bugs.

## 2. How the evidence was gathered

- aurora and Party Board were cloned at their ADR 0009 pins into the session scratchpad, **outside the repo**. Nothing from either is committed, and the user's dump was not used.
- **GX surface:** a throwaway script listed every `GX*`/`GD*` call in Party Board's game sources (`src/game`, `src/REL`, `src/libhu`, `src/msm`) and classified each against aurora's definitions at the pin (§3).
- **Code reading** of aurora's GX front end, command processor, `gfx/` renderer, `webgpu/` device code and MTX, looking for host time, threads, window coupling and CPU-visible results.
- **SDL_GPU probe:** a throwaway C program linked against the SDL3 that our `vcpkg.json` builds (3.4.12, `x64-windows-static-md`). It listed the compiled-in GPU drivers, then created a device with no window, cleared an offscreen texture and downloaded it to CPU memory.
- **Upstream activity:** git history and Party Board's issue tracker (read-only).
- **Not done:** building aurora and rendering synthetic scenes through it. That needs Dawn plus about eight libraries fetched from the network (§5), and a Clang toolchain, which this machine does not have (Visual Studio is installed without its LLVM component). Fetching those was outside what this spike was cleared to download, so it is gate G-a/G-d in §8.

## 3. Coverage of MP4's GX surface

At the pin, Party Board's game sources call **131 distinct GX entry points**. P1-01 counted about 127 on the decomp. The difference is aurora's resource-lifetime extensions (`GXDestroyTexObj`/`GXDestroyTlutObj`, 52 call sites added by Party Board) and two header macros. **MP4 calls no `GD*` function.**

| aurora status at the pin | # | Notes |
|---|---|---|
| Implemented | 104 | Includes TEV with swap tables, konst colours and S10 colours; indirect textures (`GXSetIndTexMtx`, `GXSetTevIndWarp`, `GXSetTevIndTile`, `GXSetIndTexCoordScale`); fog; lights (spot, distance attenuation, specular); CI textures with TLUTs; runtime display lists; `GXCopyTex`; scissor; viewport jitter; line width (expanded to quads, since WebGPU has no wide lines); indexed `GXPosition1x8`/`1x16`; `GXProject` and the viewport/projection getters. |
| Header macro or inline | 2 | `GXSETARRAY`, `GXSetTexCoordGen`. |
| Stub | 9 | Seven are display-copy/XFB calls (`GXCopyDisp`, `GXSetDispCopy*`, `GXSetCopyFilter`), which are correct as no-ops on PC. `GXInvalidateTexAll` is also fine as a no-op, because aurora versions texture data. **`GXSetZTexture` is a real gap**: it is used by m419 (Tree Stomp). |
| `// TODO` (absent) | 11 | Ten perf-metric calls. ADR 0007 §5 already says these return 0. Plus `GXGetYScaleFactor`, which is used only on the PAL path. |
| Missing | 5 | `GXSetDrawSync`, `GXSetDrawSyncCallback`, `GXWaitDrawDone`, `GXResetWriteGatherPipe` and the `GXNtsc480Prog` mode object. Party Board supplies these in its port layer, which we don't use. ADR 0007 §5 already defines them (sync callbacks fire synchronously; the rest are no-ops). We add them to our SDK, about 30 lines. |

**Known rendering gaps** (presentation only; R6 already says no readback feeds logic):
- **Z textures** (m419): we need to write this, about a day in the shader generator plus the depth write.
- **Partial `GXCopyTex` clears** (m427, Right Oar Left): Party Board works around it with `TARGET_PC` (skips the clear). Fixed upstream after the pin (`9c0bf66`).
- **Party Board's open graphical issues:** #29 m427 flashlight, #30 m402 particles, #31 m406 vertex explosions. None has a diagnosis yet. They cannot be reproduced without the dump lane.

**Option (b) would start from zero coverage.** The aurora code it would replace is about 6.4k lines of front end (GX API, FIFO, command processor), about 14.4k lines of renderer (`gx/` backend, `gfx/`, `webgpu/`) and 2.5k lines of headers, written over several years for Metaforce, Dusklight and Party Board.

**Headers are not optional.** Party Board's game structs embed aurora's `GXTexObj`/`GXTlutObj`, whose sizes differ on 64-bit. Its game code also calls aurora's extensions. Even option (b) would have to ship aurora-ABI-compatible Dolphin headers, which in practice means aurora's headers.

## 4. Fitting behind the shell's presenter, readback and the null backend

**Window ownership.** At the pin, `aurora_initialize` creates the SDL window, and `webgpu::initialize` creates a surface and passes it to the adapter request as `compatibleSurface`. The GX code itself reaches outside `lib/gx`, `lib/gfx` and `lib/dolphin/gx` only through a narrow set of symbols:
- `window::get_window_size`, `get_sdl_window`, `SurfaceLock`, `is_presentable`, `push_custom_event` and `set_frame_buffer_aspect_fit`;
- `webgpu::g_device`, `g_queue`, `g_instance`, `g_frameBuffer`, `g_frameBufferResolved`, `g_depthBuffer`, `g_graphicsConfig`, `g_backendType`, the texture-format capability flags and the cache functions;
- `vi::configured_fb_size`;
- `aurora::g_config` (`cachePath`, `resourcesPath`, `allowTextureDumps`);
- `log_internal`, `OSPanic` and `OSCachedToPhysical`.

So we **do not link `aurora_core`**. Our host shim provides those symbols: an offscreen framebuffer at `internal_scale` × 640×480, no window, and no ImGui, RmlUi or input. `webgpu/gpu.cpp` is compiled from the pin with a patch that makes the surface optional. Dawn can request an adapter without a compatible surface.

**Readback.** aurora has no colour readback, so this is new code. In the `gfx::end_frame` encoder callback, copy the EFB texture into a `MapRead` buffer, map it, and write it out as `cpu_rgba8` with stride = width and R,G,B,A byte order (ADR 0002 rule 7). The default is synchronous. A one-frame-delayed readback is an allowed E7 optimisation: it delays only the picture, never game state. Upstream is already moving toward embedding (`db7a3d6` "Support external render targets", `b979ff6` public `aurora::gfx` API, both after the pin).

**Null backend.** aurora's own `gx_fifo_tests` builds the whole GX API, FIFO and command processor **without the WebGPU runtime**, by linking a stub file for the renderer symbols. That is exactly the structure of our null backend: the same front end, with the renderer replaced by no-ops, and no Dawn at link time. Every `GXGet*` getter reads only `__gx`, the API-side CPU shadow state, and none reads renderer state, so null and real cannot differ in anything the game can see (checked for every getter in `GXGet.cpp`).

**Device sharing later.** This is the main cost of (a). The renderer has its own Dawn device, while the shell presents through SDL. Phase 1 uses `cpu_rgba8` (ADR 0002 rule 8), so this does not matter until the GPU hand-off ADR, which ADR 0007's Consequences already gate on E7. The options at that point are for the presenter to adopt WebGPU for the GC/Wii path, or to use native interop (D3D12 shared handles, Vulkan external memory). With (b) or (c), the device would be SDL_GPU's and sharing would be natural.

## 5. Determinism isolation

Rendering must never feed game logic, and aurora's OS time must not be used. Findings at the pin:

| # | Finding | Severity | Handling |
|---|---|---|---|
| D1 | **`GXAdjustForOverscan` returns the window's framebuffer size.** Party Board's `init.c` calls it, and the result becomes `RenderMode`, which feeds the game's viewport/scissor, `defCamera` (`hsfman.c`) and so `GXGetViewportv`/`GXProject`, which are game-visible in m427/m428 (mp4.md §8). It also feeds the **framebuffer heap allocation size** (`init.c`), which changes the arena layout. Window size or `internal_scale` would change game state, breaking E6 and ADR 0002 rule 2. | **High** | Backport upstream `e2fc845` (2026-09-21, "Restore original GXAdjustForOverscan logic"), which computes it from the input mode like the SDK. Then the logical size is always 640×448 for NTSC. Our shim keeps `vi::configured_fb_size` logical too. |
| D2 | **MTX calls host libm trig** (`sinf`, `cosf`, `tanf`, `acosf`) in `MTXRotRad`/`RotAxisRad`, `MTXPerspective`/`LightPerspective` and the quaternion functions. Party Board uses `MTXRotRad` 136 times, `MTXRotDeg` 39, `MTXRotAxisRad` 19 and `MTXLightPerspective` 30. This breaks ADR 0008 §5. | **High** | Compile aurora's MTX sources inside the game module with ADR 0008 flags, and redirect those four functions to the pinned MSL math through a forced-include header, so no source patch is needed. Golden tests (P1-21). |
| D3 | `PSVECNormalize`/`PSVECMag`/`PSVECDistance` use a **bit-exact table emulation of PPC `frsqrte`** plus an explicit `fmaf`. Both are exactly specified, so the results are platform-independent, and closer to console behaviour than the C versions. Upstream later fixed signed-overflow UB in that emulation (`4143fc1`) and a rounding issue in `PSVECSquareMag` (`f8573d3`). | Low | Keep. Backport both fixes. Architect should confirm that an explicit `fmaf` call is acceptable under "no FMA contraction". It is not contraction, because it is written in the source and correctly rounded by C99. Otherwise, replace those three functions with our C versions. |
| D4 | **Host threads in the renderer:** a render worker, a pipeline-compile thread and an SQLite pipeline-cache writer, plus sleep/poll loops in `begin_frame`. | None for state | Presentation only. Nothing flows back to the CPU-side GX state: the getters read only `__gx` (§4). |
| D5 | **Asynchronous pipeline compilation skips draws** whose pipeline is not ready yet (`bind_pipeline` returns false). Images are therefore not reproducible from run to run, though the state is. | Med (golden images) | Patch: a synchronous-pipeline mode with no per-frame build cap, used by golden-image tests and optionally by the dump lane. |
| D6 | `GXPeekZ` (`gfx/depth_peek.cpp`) returns depth from a snapshot that is refreshed on `std::chrono::steady_clock`. That is a wall-clock read with a GPU→CPU result. | None for MP4 (no `GXPeek*` calls) | Patch: trap it with a fatal error in our build, so a later title cannot use it without a decision. |
| D7 | **No aurora OS time** is referenced from `lib/gx`, `lib/gfx`, `lib/dolphin/gx|gd|mtx`. Their only OS/VI dependencies are `OSPanic`, `OSCachedToPhysical` and `vi::configured_fb_size`, which our SDK provides. | None | aurora's OS/VI/time are not linked at all. |
| D8 | **`GXCopyTex`** creates a GPU render target keyed by destination address and leaves the CPU bytes untouched. That is ADR 0007 §5 exactly. **New finding:** m415 (Stamp Out!) `memcpy`s the shadow-map copy destination (`Hu3DShadowData.buf`) into another texture's CPU buffer. That is a CPU read of a copy destination, though **presentation only**: the bytes are used only as a texture, never by game logic. Party Board aliases the pointer under `TARGET_PC`. | Low | Keep Party Board's `TARGET_PC` branch for m415. Allow-list it in the P1-14 debug trap. `mp4.md` §8 is updated. |
| D9 | **Renderer-side caches are host state:** the global texture-object ID counter, the copy-texture map and the pipeline/texture caches. After `loadState` they can refer to stale textures. | Low (presentation) | Clear the copy-texture and texture caches on `loadState`. Put the ID counter in the `SaveState` registered globals. Never hash it (ADR 0008 §8). |
| D10 | **Display lists recorded into game memory contain host pointers** (aurora's `ARRAYBASE` and texture-object commands use 64-bit addresses) and aurora-specific opcodes. | Low | The hash never covers raw arena bytes (ADR 0008 §8). Same-platform save states are fine. This joins the cross-platform `SaveState` work (ADR 0008 §10, Phase 2). |

**GPU output itself is not bit-identical across GPUs or drivers.** That is true for every backend option. It affects golden-image comparisons (they need tolerances or a per-machine baseline), never the state hash.

## 6. Patch set against the pin

All patches are against MIT code and allowed by ADR 0009 §5:
1. Backport `e2fc845` (overscan, D1).
2. Backport `4143fc1` and `f8573d3` (MTX fixes, D3).
3. `webgpu/gpu.cpp`: make the surface optional (headless device).
4. Synchronous-pipeline mode (D5).
5. `GXPeekZ` trap (D6).
6. Cache reset hook for `loadState` (D9).
7. `GXSetZTexture` (m419), and if needed a backport of `9c0bf66` (partial copy clears, m427).

Everything else is our own code in `runtimes/gc_wii/gx/`:
- the host shim;
- readback;
- the null backend's stub table;
- the trig redirect header;
- the five missing SDK entry points;
- the copy-destination debug trap.

## 7. Platforms, SDL3 and build weight

**SDL_GPU with our vcpkg features (measured on Windows).** The SDL3 3.4.12 that `vcpkg.json` builds contains **one GPU driver, `direct3d12`**. The Vulkan driver is not compiled in, because the port's `vulkan` feature is off. The probe created a device **without a window** on D3D12, cleared an offscreen texture and read back the expected pixel (255, 128, 0, 255). Linux has no D3D12, so with the same feature set **SDL_GPU on Linux would have no driver at all**. That follows from the port's feature mapping (`vulkan` → `SDL_VULKAN`). It is inferred, because no Linux machine was available.
- For **(a)** this does not matter. Dawn brings its own Vulkan (Linux) and D3D12 (Windows) backends, and in Phase 1 we render offscreen with no surface. If a surface is ever needed, aurora takes the X11/Wayland/HWND handles from SDL window properties, which our `x11`/`wayland` features already provide.
- For **(b)/(c)**: `vcpkg.json` must add the `sdl3` `vulkan` feature. That is a root build file, so the request goes to architect. SDL bundles the Vulkan headers, and the feature adds no new port dependency.

**Runtime shader compilation decides the build weight.** GX pipelines are generated at runtime from TEV state. aurora writes WGSL and Dawn's Tint compiles it in-process. SDL_GPU accepts only precompiled bytecode per backend (SPIR-V, DXIL, MSL), so (b)/(c) would also need a runtime compiler, such as SDL_shadercross with DXC and SPIRV-Cross. The gap in build weight between Dawn and SDL_GPU is much smaller than "Dawn vs. nothing".

**Dawn and pinnability.** aurora's default `package` provider downloads a prebuilt Dawn tarball from `encounter/dawn-build` by **release tag, with no hash**. That fails ADR 0009's pinning rules (full commit hashes, verified sources). It is also an unverified third-party binary. We use the **`vendor` provider**: Dawn source at aurora's recorded commit (`AURORA_DAWN_REF` = `13abc3bc8ea2d3c2050f9e77a12d012108ceee24`), whose own dependencies are pinned by Dawn's `DEPS` through `fetch_dawn_dependencies.py`. Cost:
- a large one-time source fetch and a long first build (tens of minutes), cached in `external/` or asset-import's cache like the other pins;
- Dawn is linked as one shared library outside the game module's fixed base (ADR 0008 §11).

Since Phase 1 is developer-only (D1), this is acceptable.

**Other aurora dependencies:** abseil, xxhash, fmt, zlib, libpng, sqlite3, zstd and Tracy (magic_enum is bundled). aurora's CMake uses existing targets first and falls back to FetchContent, several of them hash-pinned. We should provide them through our vcpkg manifest, which architect owns, so that no FetchContent runs.

**Windows and Linux.** Party Board ships aurora on both, with Clang presets on both. Note for P1-14/P1-15: this development machine has no `clang-cl`, and ADR 0008 §4 requires Clang. The Visual Studio "C++ Clang tools" component, or LLVM, must be installed first.

## 8. Gate at the start of P1-14

These checks run inside the remaining S1 time-box. Synthetic data only, no dump.

| Gate | Check | Fallback trigger |
|---|---|---|
| G-a | Dawn (vendor, pinned commit) and aurora's GX build with Clang on Windows and Linux from a clean cache, with no un-hashed download. | Not achieved within 5 working days, or Dawn cannot be pinned → option (c). |
| G-b | A headless device (no window) and EFB → `cpu_rgba8` readback on both OSes. Must also work on Linux under a CI-like headless session (Vulkan, or Dawn's null adapter for the null CI lane). | Fails on either OS → option (c). |
| G-c | The null backend links **without Dawn**. For each synthetic scene, the CPU-side GX state (`__gx` plus the FIFO byte stream) is identical with null and real. | Any difference is a bug in our shim, to be fixed before P1-14 continues. |
| G-d | Synthetic scenes render, compared visually and then as golden images with a tolerance:<br>– TEV swap tables, konst and S10 colours;<br>– indirect warp/tile;<br>– fog;<br>– spot and specular lights;<br>– CI8 + TLUT;<br>– a runtime display list;<br>– `GXCopyTex` re-sampled as a texture, including a partial-rectangle copy;<br>– a Z texture (**expected to fail until patch 7**). | A feature that aurora renders wrongly is a patch, not a fallback trigger. |
| G-e | Two runs on one machine give identical images in synchronous-pipeline mode. | Patch 4 is not enough → investigate; images only. |

**Option (c), if triggered.** Keep aurora's front end (GX API, FIFO, command processor, `GXState`, headers and MTX, all backend-independent and MIT). Write our own renderer on SDL_GPU, porting aurora's TEV→WGSL generator to HLSL or SPIR-V with runtime compilation, and add the `sdl3` `vulkan` feature. Estimate: 6–10 weeks to reach aurora's level on MP4. That would move P1-14 off the critical path's current dates.

## 9. Criteria summary

| Criterion | (a) aurora GX/MTX on Dawn | (b) own GX on SDL_GPU | (c) aurora front end + own SDL_GPU renderer |
|---|---|---|---|
| Coverage of MP4's GX surface | 104/131 implemented. The rest is no-ops, ADR-defined stubs, and one real gap (Z textures). | Starts at 0. Must still match aurora's ABI and extensions. | Front end as (a). Renderer starts at 0. |
| Usable without aurora's OS/VI/PAD/DVD/CARD and without owning the window | Yes, with our shim (about 20 symbols) and patch 3. | Yes | Yes |
| Readback and null/headless | Readback is new code (small). Null = front end plus stubs, proven by aurora's own tests. | All new | Readback via SDL_GPU (probe proved it on D3D12). Null as (a). |
| Determinism isolation | Getters are CPU-only. D1 and D2 must be patched. No OS time. | Ours by construction | As (a) for the front end |
| MTX strict FP | Plain C, no SIMD. Trig redirected. PS rsqrte exact. | Our C versions | As (a) |
| Windows/Linux | Both (Dawn D3D12/Vulkan). Independent of SDL features. | Linux needs the `sdl3` `vulkan` feature | Same as (b) |
| Build weight | Dawn source build (heavy, one-time, cached) plus 8 libraries | SDL_GPU plus a runtime shader compiler | Same as (b) |
| Pinnability | aurora by commit. Dawn by commit (vendor). Not the prebuilt package. | — | aurora by commit |
| License | MIT (aurora), BSD-3 (Dawn), Apache-2.0 (Tint and some Dawn dependencies). No GPL found in GX/gfx sources. | Ours plus zlib (SDL) plus the shader compiler's licences | Mixed, all permissive |
| Device sharing with the presenter later | Separate Dawn device. Interop or a WebGPU presenter needed (later ADR). | Natural | Natural |
| Maintenance | Very active upstream: 373 commits in the year to the pin, 169 of them in GX areas, and 132 commits since the pin. Fixes flow to us, but each bump must keep Party Board compiling and re-record golden trails. | All on us, for 5 titles | Renderer on us, front end shared |
| Time to MP4 parity | Shortest | Months | 6–10 weeks |

## 10. Risks

| Risk | Likelihood / impact | Mitigation |
|---|---|---|
| Dawn's vendor build is too heavy or fragile for local developer builds on both OSes. | Med / Med | Gate G-a. Cache it like the other pins. Fallback (c). |
| Upstream churn makes pin bumps expensive, and Party Board lags aurora. | High / Low | Bump deliberately through the dump lane (ADR 0009). Keep our patch set small and upstreamable. Bump to a commit that includes `e2fc845`, `9c0bf66` and `db7a3d6` once Party Board builds against it, or carry those as patches. |
| Unknown aurora rendering bugs in MP4 minigames beyond the three known ones. | Med / Low | These are presentation-only (R6). E1 is playable with cosmetic bugs. Each fix is a patch, sent upstream where possible. |
| Readback cost misses E7 at `internal_scale` 1. | Low / Med | 640×480×4 bytes per frame is small. One-frame-delayed readback first, then the GPU hand-off ADR. |
| The separate Dawn device complicates the later GPU hand-off. | Med / Low | Phase 1 is `cpu_rgba8`. Decided in the hand-off ADR (WebGPU presenter or native interop). |
| Our assumption about which aurora getters are CPU-only breaks at a later pin. | Low / High | Gate G-c becomes a CI test (P1-21): null and real give the same GX-side state and the same `stateHash`. |
| No Linux machine was used. The Linux SDL_GPU finding is inferred. | — | It only matters for (b)/(c). G-b verifies Linux for (a). |

## 11. Housekeeping

- No prototype code is committed. The probe and the survey script stayed in the scratchpad. `runtimes/gc_wii/spike/` was not needed.
- `docs/titles/mp4.md` is updated: §1 (aurora's status), §8 (m415 copy read) and the S1 line under "Sections still to fill".
- Requests for architect (root files): add `abseil`, `xxhash`, `fmt`, `libpng`, `sqlite3`, `zstd` and `tracy` to `vcpkg.json` when P1-14 starts (G-a), and **only under fallback (c)** the `sdl3` `vulkan` feature. Request for qa-reviewer: once `external/` is gitignored (P1-20e1), make the compliance scanner also reject a Dawn source tree.
