# ADR 0007: GameCube/Wii SDK replacement layer

**Status:** Accepted
Author: architect, 2026-10-08. Amended after P1-01 (`docs/titles/mp4.md`) and after spike S1 (`docs/spikes/s1-gx-backend.md`, §6) on 2026-10-08. Approved by: architect, 2026-10-08.

## Context
The MP4 game sources, like MP5–MP8 later, call the console SDK: GX (graphics), VI (video timing), PAD, DVD, OS (threads, alarms, time, heaps), CARD (memory card), AI/AR/ARQ/DSP (audio output, ARAM), THP (movies) and the matrix libraries. MP4 also calls the **MusyX** sound library (52 `snd*` functions), which drives the DSP with its own microcode. **MP4 calls no AX at all** (P1-01, `mp4.md` §10). A decomp port runs the game's C code natively and replaces whatever sits under it. PLAN §6 assigns one shared layer for every GameCube/Wii title to `runtimes/gc_wii/`, owned by runtime-gc-wii.

P1-01 also found that the game **spins without yielding** while waiting for DVD reads, ARAM DMA and sound voices to finish (`mp4.md` §7), and that **audio state is game-visible**: board code polls stream, music and SFX status and a MIDI controller value.

MP4's game source is Party Board (ADR 0009). It builds on aurora for GX, GD, MTX, OS, DVD, CARD, PAD, SI and VI, it stubs audio entirely, and THP movies do not play.

## Decision
1. **Replace at the SDK API level, not the hardware level.** We implement `GX*`, `PAD*`, `DVD*`, `OS*`, `CARD*`, `AR*`/`ARQ*`, `AI*` and so on as C/C++ functions. Hardware registers, the CPU and the DSP are not emulated. Where the game source contains decompiled SDK internals, the game links against our implementations instead.
2. **One shared library**, `runtimes/gc_wii/sdk/`, used by every GameCube/Wii title. Title-specific workarounds live in that title's module, never in the SDK layer.
3. **Everything game-visible advances inside `stepFrame()`.** VI retrace callbacks, alarms, I/O completion, the sound engine and THP playback move forward only inside the frame step (or a time query within it, ADR 0008 §2), never on host threads. The audio **device** callback only drains already-rendered samples (ADR 0008).
4. **Subsystem decisions:**
   - **OS:** threads, virtual time, alarms and the arena per ADR 0008. Hardware-setting getters return **fixed values**: stereo sound mode, progressive scan off (so no boot prompt), a retail console type, 24 MB of physical memory, a fixed language and reset code.
   - **PAD:** a fixed table from canonical `Button` to GC pad buttons. Sticks and triggers pass through with documented scaling. `connected == 0` means no controller in the port. Sampling happens at one fixed point in `stepFrame`, where the console's VI post-retrace callback would run, reading that frame's `InputFrame`. Rumble (`PADControlMotor`) is an output, optional in Phase 1.
   - **DVD and ARAM (AR/ARQ):** DVD reads come from the files that asset-import extracted into `VerifiedDump::build_dir`. **DVD reads and ARAM DMA complete synchronously, inside the call**, and their callbacks run before the call returns or at the next SDK call boundary. A multi-frame latency is not allowed, because MP4 spins on completion without yielding and would hang. DVD never reports errors.
   - **CARD:** served from `SaveMedia` (ADR 0004). The image format is ours, versioned by `SaveMedia::format_version`. Operations also complete synchronously (zero-frame latency, which satisfies ADR 0004 rule 4). `CARDGetSerialNo` returns the image's stable serial (ADR 0004).
   - **Audio: the title's sound engine runs as game code, and we replace what is under it.** For MP4 that means:
     - The engine is **MusyX**, from the pinned `AxioDL/musyx` (ADR 0009), run through its **PC synth backend in pull mode** (`sndPCRender`-style: we ask for N frames, it renders them). There is no AX layer.
     - It is stepped **inside `stepFrame`** on virtual AI-DMA ticks: 32 kHz output, with a deterministic fractional-sample accumulator so the number of samples per frame is a pure function of the frame index.
     - Its **own-thread SDL backend is never used**, nor any backend that renders on a host thread or timer.
     - **Null audio** discards the PCM but **still runs the synth and sequencer**, because their state is game-visible (ADR 0008 §8). E6 depends on this.
     - The shell resamples s16 32 kHz output to the device.
     - An AX-compatible mixer is built only when a later title calls AX.
   - **THP movies are in scope.** The THP decoder runs **synchronously inside the frame step**, replacing MP4's use of the OS idle function. Video frames reach the screen through the GX path (YUV→RGB). Movie audio is mixed into the same deterministic output stream. Playback progress, such as which frame is shown or whether the movie has ended, is game-visible and advances only with frames. The default is to port the THP decoder from the pinned game source to plain C (replacing its paired-single code), and to write our own only if that fails. Skipping a movie must stay possible, since movies are not needed for E1.
   - **GX:** a state-machine translation to a modern GPU API. It renders offscreen and reads back to `cpu_rgba8` in Phase 1 (ADR 0002 rule 8). It has a **null backend** that runs no GPU work, for headless replays and CI. Display lists recorded at runtime (`GXBeginDisplayList`) are parsed by the layer itself.
   - **MTX/VEC/QUAT:** every `PSMTX*`/`PSVEC*`/`PSQUAT*` entry point is implemented as plain C under the strict-FP rules of ADR 0008. No paired-single emulation, no SIMD whose results can differ between platforms.
5. **Rendering never feeds game logic.** Any GX call that returns GPU results to the CPU must return a deterministic value that does not depend on the GPU, or be shown to be presentation-only. Every such call is listed in `docs/titles/<id>.md`. For MP4 (`mp4.md` §8):
   - **No readback feeds logic.**
   - `GXCopyTex` destinations stay GPU-side as render targets keyed by destination address. Their CPU-side bytes stay zero and outside the hash manifest, and a debug assert traps CPU reads of those ranges.
   - Perf metrics return 0, and draw-sync callbacks fire synchronously.
   - `GXProject` and the viewport/projection getters are pure C on CPU-side state, because their results are game-visible.
   - **m415 (Stamp Out!)** copies a `GXCopyTex` shadow-map destination into another texture's CPU buffer. It is presentation only, and no logic reads the bytes. Party Board's `TARGET_PC` branch aliases the texture pointer instead, and we keep that branch. m415 is allow-listed in the copy-destination debug trap (found by S1).
6. **Graphics backend: option (a), aurora's GX and MTX on Dawn, without `aurora_core`.** Decided by spike S1 (P1-12, `docs/spikes/s1-gx-backend.md`) and folded in here on 2026-10-08.
   - **What we take from `encounter/aurora`** (MIT, pinned at `514339438178ef2bed1b14e5149d90ece0c6e0cc`, the commit Party Board pins, ADR 0009 §1):
     - its Dolphin headers, which Party Board's structs and calls already depend on (64-bit `GXTexObj`/`GXTlutObj`, aurora extensions);
     - the GX front end (the GX API, FIFO and command processor);
     - its WebGPU renderer (`lib/gx` backend, `lib/gfx`, and `lib/webgpu` with patch 3 below);
     - its MTX sources.
     - GD may be built for later titles. MP4 calls no `GD*` function.
   - **What we do not take:** `aurora_core` (the window, SDL events, input, ImGui, RmlUi and `aurora_initialize`), and aurora's OS, VI, PAD, DVD, CARD, SI, THP and time code. They are not linked.
   - **Where each part links.** This follows ADR 0008 §11.
     - The GX front end, its CPU-side state (`__gx`, the FIFO) and MTX are game-visible. They are compiled into the **game module**, under ADR 0008's flags and at its fixed base, and their state is part of `SaveState`.
     - The renderer and Dawn are presentation only. They live in a **separate host-side library**, reached only through the renderer entry points that the null backend stubs.
     - No renderer or Dawn address is ever stored in game-visible memory.
   - **Host shim** in `runtimes/gc_wii/gx/`. It provides the roughly 20 symbols the renderer expects from `aurora_core`: window-size queries, device and queue, framebuffer textures, `vi::configured_fb_size`, config paths and logging. It creates a **Dawn device with no window or surface**, and an offscreen EFB of `internal_scale` × the logical framebuffer. `vi::configured_fb_size` always reports the **logical** size.
   - **Readback.** At the end of each frame, the EFB is copied to a mapped buffer and returned as `FrameOutput::cpu_rgba8` (ADR 0002 rules 7 and 8). Readback may lag by one frame, because it affects only the picture.
   - **Null backend.** The same front end with the renderer replaced by no-op stubs, without linking Dawn. Every `GXGet*` reads only the API-side CPU state, so null and real cannot differ in anything the game can see. A CI test checks that both give identical GX-side state (`__gx` plus the FIFO stream) on the synthetic scenes, and the same `stateHash` (P1-21).
   - **Dawn** (BSD-3-Clause) is built **from source** at the commit aurora records, `13abc3bc8ea2d3c2050f9e77a12d012108ceee24`. Its own dependencies are pinned by that commit's `DEPS` and fetched by its `fetch_dawn_dependencies.py`. **aurora's prebuilt Dawn package is not used**, because it is fetched by tag with no hash. aurora's other dependencies come from our vcpkg manifest, never from FetchContent.
   - **MTX** follows ADR 0008 §4–5. `sinf`, `cosf`, `tanf` and `acosf` are redirected to the pinned MSL math through a forced-include header. The `PSVEC*` reciprocal-square-root path keeps aurora's bit-exact `frsqrte` table and its explicit `fmaf`, under the conditions in ADR 0008 §5.
   - **Patch set** against the pin (ADR 0009 §5). It is kept small and offered upstream:
     1. Backport `e2fc845`. `GXAdjustForOverscan` is computed from the render mode, never from the window. At the pin it returns the window size, which reaches game-visible viewport state and heap allocation sizes, and would break E6 and ADR 0002 rule 2.
     2. Backport `4143fc1` and `f8573d3` (MTX fixes: signed-overflow UB in the `frsqrte` emulation, and `PSVECSquareMag` rounding).
     3. Make the surface optional in `webgpu::initialize`, so a headless device works.
     4. A synchronous-pipeline mode for golden images, because asynchronous pipeline compilation skips draws.
     5. Make `GXPeekZ` trap. aurora samples it on a wall clock, and MP4 never calls it.
     6. Clear the renderer caches on `loadState`. The texture-object ID counter becomes a registered `SaveState` global and is never hashed.
     7. Implement `GXSetZTexture` (m419). Optionally backport `9c0bf66` (partial `GXCopyTex` clears, m427).
   - **Our SDK adds** the entry points aurora lacks, with the semantics in §5:
     - `GXSetDrawSync` and `GXSetDrawSyncCallback` (the callback fires synchronously);
     - `GXWaitDrawDone` and `GXResetWriteGatherPipe` (no-ops);
     - the perf-metric calls (they return 0);
     - the `GXNtsc480Prog` mode object.
   - **No profiler or renderer timing inside the game module.** aurora's Tracy instrumentation is compiled out of everything linked into the game module. It may be enabled only in the host-side renderer library, in developer builds.
   - **Gate at the start of P1-14** (inside the S1 time-box, synthetic data only). All of these must pass:
     - G-a: Dawn (pinned, from source) and aurora's GX build with Clang on Windows and Linux from a clean cache, with no unhashed download.
     - G-b: a headless device and EFB → `cpu_rgba8` readback work on both OSes.
     - G-c: the null backend links without Dawn and matches the real backend's GX-side state.
     - G-d: the synthetic scenes render (TEV swap/konst/S10, indirect, fog, lights, CI/TLUT, runtime display lists, EFB copies, and Z textures after patch 7).
     - G-e: two runs give identical images in synchronous-pipeline mode.

     A rendering defect found by G-d or G-e is a patch, not a fallback trigger. **If G-a or G-b cannot be achieved within 5 working days, fallback (c) applies:**
     - keep aurora's backend-independent front end, headers and MTX (all of the above except the renderer and Dawn);
     - write our own renderer on SDL_GPU, with runtime shader compilation;
     - add the `sdl3` `vulkan` vcpkg feature (with the current features SDL_GPU has no Linux driver, which S1 found).

     Triggering (c) is recorded as an amendment to this section. It changes only the renderer, not the front end, the headers or anything game-visible.
   - **Pin bumps** follow ADR 0009 §1. A bump to a commit that already contains patches 1, 2 and 7b, and `db7a3d6` (external render targets), is encouraged once Party Board's game code builds against it.
7. **Interrupt masking** (`OSDisableInterrupts`/`OSRestoreInterrupts`) is a no-op under cooperative scheduling. Virtual interrupts only ever run at SDK call boundaries (ADR 0008 §2).

## Alternatives
- **Emulate hardware registers (as Dolphin does).** Rejected: it is an emulator under another name, and slower, with no benefit when we have source.
- **A software rasterizer.** Rejected as the main path (too slow at higher internal scales). It is kept as a possible reference renderer for golden-image tests.
- **A separate SDK layer per title.** Rejected: MP5–MP8 share most of the SDK, and forking it multiplies the bugs.
- **Use aurora whole (as Party Board does).** Rejected: host-clock time and its own save handling break ADRs 0004 and 0008.
- **Reimplement MusyX, or emulate the DSP microcode.** Rejected: a CC0 MusyX with a PC synth already exists, so we reuse it.
- **MusyX's SDL backend, or any threaded audio renderer.** Rejected: rendering on a host thread makes game-visible sound state depend on thread timing (CLAUDE.md rule 4).
- **Stub audio (as Party Board does).** Rejected: board logic reads sound state, so a stub changes game behaviour, and E1 needs sound.
- **DVD/ARAM with a fixed multi-frame latency.** Rejected for MP4: its non-yielding wait loops would never see completion.
- **(b) Our own GX layer on SDL_GPU from scratch** (S1). Rejected:
  - It starts at zero coverage, against about 20k lines of mature aurora front end and renderer.
  - It would still have to reproduce aurora's ABI, because Party Board's structs embed aurora's 64-bit `GXTexObj`/`GXTlutObj` and its code calls aurora's extensions.
  - It needs a runtime shader compiler for TEV pipelines anyway.
  - Its one structural advantage, sharing a device with the presenter, is not needed until the GPU hand-off ADR.
- **aurora including `aurora_core`.** Rejected: it owns the window, events and input, and requires a presentable surface.
- **aurora's prebuilt Dawn package.** Rejected: it is fetched by tag with no hash, so it cannot be pinned, and it is an unverified third-party binary.

## Consequences
- GX golden-image tests use **our own** synthetic display lists and textures (P1-21), never game assets. MusyX and THP tests use **synthetic** sound data and synthetic THP streams that we generate, never game files.
- P1-13 includes MusyX audio stepping and THP decoding, not only OS/DVD/PAD/VI/CARD/AR.
- The MusyX sequencer and voice state, and THP playback state, are hash-manifest areas (ADR 0008 §8).
- A GPU texture hand-off (instead of readback) needs its own ADR. It is written only if E7 performance fails, or when the presenter moves to the GPU. With (a) the renderer has its own Dawn device, so that ADR chooses between a WebGPU presenter for the GC/Wii path and native interop (D3D12 shared handles, Vulkan external memory).
- **New build prerequisites for P1-14** (root files, landed by architect when P1-14 starts):
  - vcpkg ports `abseil`, `xxhash`, `fmt`, `libpng` (with `zlib`), `sqlite3` and `tracy` (headers only), in a `gc-wii` manifest feature. `zstd` was dropped after the P1-14 gate (see Review);
  - the Dawn source build in `external/`;
  - the `sdl3` `vulkan` feature, only under fallback (c).
  - A Clang toolchain (clang-cl on Windows) is required by ADR 0008 §4. It is now installed on the reference machine (clang-cl 22.1.3, bundled with Visual Studio 18).
- GPU output is not bit-identical across GPUs or drivers. Golden-image tests use tolerances or per-machine baselines. This never affects `stateHash()`.
- MP8's Wii additions (WPAD, motion) extend this same layer in Phase 4.

## Review (architect, 2026-10-08)
**Verdict: Accepted in full** (§6 folded in after spike S1, 2026-10-08).
- First review: §1–5 do not depend on the backend, so P1-13 (SDK core) could start at gate G1.
- **Amended after P1-01:**
  - Audio is MusyX's PC synth in pull mode, stepped inside `stepFrame`. This replaces both the AX mixer and my earlier "MusyX over our AX" default, which P1-01 showed to be wrong: MP4 has no AX.
  - DVD, ARAM and CARD complete synchronously.
  - THP movie decoding is in scope.
  - Hardware-setting getters return fixed values.
  - The MP4 readback findings are recorded.
  - S1 now weighs aurora heavily, but only its GX/GD/MTX parts.
- **Amended after spike S1 (§6).** I accept option (a) and fold §6 in now, rather than keeping it open until the P1-14 gate:
  - The decision itself is settled. S1 measured 104 of MP4's 131 GX entry points as implemented at the pin, with one real gap (Z textures). Party Board's ABI ties us to aurora's headers whichever renderer we use.
  - The gate tests feasibility of the build and headless readback, not the choice, and its fallback (c) keeps everything game-visible unchanged.
  - So triggering (c) would amend only the renderer bullet.
  - Determinism isolation is covered by patch 1 (overscan), the MTX trig redirect, the CPU-only getters checked by G-c, and the game-module/renderer link split.
  - The separate proposed-amendment file is deleted. Its text lives here and in ADR 0009 §1.
- Nothing in this layer reads RVZ or other containers. Disc containers are asset-import's concern (Phase 1 plan D9, §12).
- **Amended after the P1-14 opening gate** (2026-10-09, `docs/spikes/p1-14-gate.md`, PR #11).
  - **Gate status.** G-a to G-e pass on Windows, and option (a) stands. G-d's two defects are patches 7 and 7b, which are not fallback triggers. The gate is **closed only when the Linux leg passes**. That runs as the **first P1-14 CI job**, on a GitHub-hosted runner, with these checks:
    - G-a: the pinned Dawn and aurora build with Clang from a clean cache, with no unhashed download.
    - G-b: rendering on **Mesa lavapipe** (Vulkan), plus Dawn's Null adapter for the device and pipeline path. The Null adapter alone does not pass G-b, because it does not rasterize.
    - G-c and G-e: on Linux, null against real.
    - The job logs the Mesa and Vulkan-loader versions.
    - The remaining time-box (about 4 days) applies. If G-a or G-b cannot be made to pass on Linux within it, fallback (c) applies as written.
    - Comparing GX stream hashes across OSes needs the fixed arena address (ADR 0008 §11). It is a P1-16/M1 check, not part of the gate.
  - **vcpkg ports.** The ports are `abseil`, `xxhash`, `fmt`, `libpng` (which brings `zlib`), `sqlite3` and `tracy`. `tracy` is used **for its headers only**: `TRACY_ENABLE` is never defined, and `Tracy::TracyClient` is never linked into anything. **`zstd` is dropped** (only `AURORA_CACHE_USE_ZSTD` needs it, and that stays off).
    - The ports go in a vcpkg manifest feature `gc-wii`, so builds of `core/`, `input/` and `asset-import` alone do not pay abseil's build time.
    - The `ci-*` presets and every preset that builds `runtimes/gc_wii` enable the feature.
    - Architect lands these ports in the first P1-14 PR.
  - **CMake wiring** is accepted as the gate proposed:
    - **Dawn is built outside our tree.** A developer recipe fetches it by commit, with `DEPS` pinned through the lock (ADR 0009, amendment after P1-14 gate). It sets `core.longpaths`, keeps vcpkg buildtrees on a short path, builds `webgpu_dawn` as a **shared** library, and installs it into a prefix under `external/` (gitignored, and rejected by the scanner).
    - The root finds it with `find_package(Dawn CONFIG REQUIRED PATHS ${MPT_DAWN_PREFIX} NO_DEFAULT_PATH)` and adds `<prefix>/include` itself. Dawn is never brought in through `add_subdirectory` or FetchContent.
    - aurora's `CMakeLists.txt` is never included. `runtimes/gc_wii/gx/CMakeLists.txt` lists the pin's sources explicitly, in three targets:
      - front end plus MTX, into the game module, under ADR 0008 flags;
      - the host-side renderer, linking Dawn, SDL3, sqlite3, libpng, abseil, xxhash and fmt;
      - the null stubs, with no Dawn.
    - A configure-time check fails if a listed source is missing at the pin, so a pin bump that moves files is caught.
  - **Root options** (architect-owned, land with the ports):
    - `MPT_GX_BACKEND`, `dawn` or `null`. The default is **`null`**, so contributors who never build Dawn can configure. Presets meant for playing MP4 and the Linux gate job set `dawn`.
    - `MPT_DAWN_PREFIX`. With `MPT_GX_BACKEND=dawn` and no usable prefix, configure fails with the recipe's name.
    - On Windows, `webgpu_dawn.dll` is copied next to the binaries with `$<TARGET_RUNTIME_DLLS:…>`.
  - **`DAWN_FORCE_SYSTEM_COMPONENT_LOAD=ON` is required** in the Dawn recipe, so that `vulkan-1.dll` and `d3dcompiler_47.dll` load from System32. We never redistribute `d3dcompiler_47.dll` or `dxcompiler.dll`.
  - **D3D12 shader compiler: FXC, on purpose, for Phase 1.**
    - **Patch 9** turns off aurora's `use_dxc` toggle, and the shim logs which compiler Dawn used, so the choice can be checked.
    - **Why:** FXC ships with every supported Windows. DXC would mean building DXC's LLVM tree (`DAWN_USE_BUILT_DXC`) or shipping its DLLs. TEV shaders need nothing beyond Shader Model 5.1.
    - The `DEPS` entry for DirectXShaderCompiler is left out of the lock (ADR 0009 amendment).
    - **Revisit** if FXC compile times cause visible hitches on first use, measured in E7, or if a later title needs SM6. Vulkan stays available on Windows either way.
  - **Pipeline mode for normal play: synchronous** (patch 4 always on), plus aurora's persistent pipeline cache.
    - In the asynchronous mode, draws are skipped while pipelines compile, which is wrong output and not just slow output. So that mode is not used, in play or in tests.
    - The cache lives in the shell's user cache directory (`SDL_GetPrefPath`), never in the repo or the build cache. Its contents come from game TEV state, so it is user data like save images. The scanner rejects `pipeline_cache.db` and `dawn_cache.db` anywhere in the tree.
    - If E7 shows first-use hitches, P1-14 adds pre-compiling every cached pipeline at boot (a warmed cache). It does not switch back to the asynchronous mode.
    - Pipeline timing never reaches game-visible state (G-c showed this), so the choice affects only the picture.
  - **Patch-set additions:**
    - **8.** A `fifo::g_processObserver` hook that sees every buffer handed to the command processor. It is null unless a test sets it, and it never changes game-visible state. P1-21's null-against-real test uses it.
    - **8b.** `comp_type_size` and `comp_cnt_count` move from the renderer's `lib/gx/gx.cpp` into the front end. They are pure CPU code that sizes vertices, so null and real share one copy, compiled under ADR 0008 flags.
    - **9.** `use_dxc` off, as above.
  - **Toolchain presets.** The default Windows presets stay MSVC, and they build `runtimes/gc_wii` as a separate Clang project (P1-13's ExternalProject). P1-14's first PR adds `windows-clang-debug` and `windows-clang-release` presets, which build the whole tree with clang-cl. They are runtime-gc-wii's day-to-day configuration and give one IDE build. They are not a second required CI leg in Phase 1.
  - **For qa-reviewer (P1-20e/P1-21):** the scanner rejects `webgpu_dawn.dll` and the cache databases above anywhere in the tree, in addition to `external/`.
