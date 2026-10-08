# ADR 0007 §6: proposed amendment (graphics backend)

**Status:** Proposed. For architect to review and fold into ADR 0007 §6. This file is not part of ADR 0007 until then, and should be deleted once its text has been folded in.
Author: runtime-gc-wii, 2026-10-08, from spike S1 (P1-12). Evidence: `docs/spikes/s1-gx-backend.md`.

---

## Proposed replacement text for §6

6. **Graphics backend: aurora's GX and MTX on Dawn, without `aurora_core`** (decided by spike S1, `docs/spikes/s1-gx-backend.md`).
   - **What we take from `encounter/aurora`** (MIT, pinned at `514339438178ef2bed1b14e5149d90ece0c6e0cc`, the commit Party Board pins):
     - its Dolphin headers;
     - the GX front end (`lib/dolphin/gx`, `lib/gx` FIFO and command processor);
     - its WebGPU renderer (`lib/gx` backend, `lib/gfx`, and `lib/webgpu` with the patch below);
     - its MTX sources (`lib/dolphin/mtx`).
     - GD (`lib/dolphin/gd`) may be built for later titles. MP4 calls no GD function.
   - **What we do not take:** `aurora_core` (the window, SDL events, input, ImGui, RmlUi and `aurora_initialize`), and aurora's OS, VI, PAD, DVD, CARD, SI, THP and time code. They are not linked.
   - **Host shim.** `runtimes/gc_wii/gx/` provides the symbols the renderer expects from `aurora_core`, about 20 of them: window size queries, device and queue, framebuffer textures, `vi::configured_fb_size`, config paths and logging. It creates a **Dawn device with no window or surface** and an offscreen EFB of `internal_scale` × the logical framebuffer.
   - **Readback.** At the end of each frame the EFB is copied to a mapped buffer and returned as `FrameOutput::cpu_rgba8` (ADR 0002 rules 7 and 8). Readback may lag by one frame, because it affects only the picture.
   - **Null backend.** The same front end (GX API, FIFO, command processor) with the renderer replaced by no-op stubs. It does not link Dawn. Every `GXGet*` reads only the API-side CPU state, so the null and real backends cannot differ in anything the game can see. A CI test checks that both give identical GX-side state (`__gx` plus the FIFO stream) on the synthetic scenes, and the same `stateHash` (P1-21).
   - **Dawn** is built from source at the commit aurora records (`13abc3bc8ea2d3c2050f9e77a12d012108ceee24`), with its dependencies pinned by Dawn's `DEPS`. It is linked as one shared library outside the game module's fixed base (ADR 0008 §11). **aurora's prebuilt Dawn package is not used**, because it is fetched by tag with no hash. aurora's other dependencies come from our vcpkg manifest, not from FetchContent.
   - **MTX** is compiled into the game module under ADR 0008 §4. `sinf`, `cosf`, `tanf` and `acosf` are redirected to the pinned MSL math through a forced-include header (ADR 0008 §5). The `PSVEC*` reciprocal-square-root path stays as is: it uses aurora's bit-exact `frsqrte` table and an explicit, correctly rounded `fmaf`. [Architect to confirm that an explicit `fmaf` is consistent with "no FMA contraction". If not, those three functions use our C versions.]
   - **Patch set** against the pin (ADR 0009 §5), kept small and offered upstream:
     1. Backport `e2fc845`: `GXAdjustForOverscan` computed from the render mode, never from the window. At the pin it returns the window size, which reaches game-visible viewport state and heap allocation sizes, and would break E6.
     2. Backport `4143fc1` and `f8573d3` (MTX fixes).
     3. Make the surface optional in `webgpu::initialize` (headless device).
     4. A synchronous-pipeline mode for golden images, because asynchronous pipeline compilation skips draws.
     5. `GXPeekZ` traps: aurora samples it on a wall clock. MP4 never calls it.
     6. Clear the renderer caches on `loadState`. The texture-object ID counter becomes a registered `SaveState` global and is never hashed.
     7. Implement `GXSetZTexture` (m419). Optionally backport `9c0bf66` (partial `GXCopyTex` clears, m427).
   - **Our SDK adds** the entry points aurora lacks, with the semantics in §5:
     - `GXSetDrawSync` and `GXSetDrawSyncCallback` (the callback fires synchronously);
     - `GXWaitDrawDone` and `GXResetWriteGatherPipe` (no-ops);
     - the perf-metric calls (they return 0);
     - the `GXNtsc480Prog` mode object.
   - **Gate, at the start of P1-14 and inside the S1 time-box.** All of these must pass:
     - Dawn and aurora build pinned with Clang on Windows and Linux;
     - headless readback works on both OSes;
     - the null backend links without Dawn and matches the real backend's GX-side state;
     - the synthetic scenes render (TEV swap/konst/S10, indirect, fog, lights, CI/TLUT, runtime display lists, EFB copies; Z textures after patch 7).

     If the build or headless readback cannot be achieved within 5 working days, **fallback (c)** applies:
     - keep aurora's backend-independent front end, headers and MTX;
     - write our own renderer on SDL_GPU, with runtime shader compilation;
     - add the `sdl3` `vulkan` vcpkg feature. Without it, SDL_GPU has no Linux driver; our current build has only `direct3d12`, as measured by S1.

     Fallback (c) is recorded as its own amendment.
   - **Pin bumps** follow ADR 0009 §1. A bump to a commit that already contains patches 1, 2 and 7b, and `db7a3d6` (external render targets), is encouraged once Party Board's game code builds against it.

## Proposed additions to §5 (MP4 readback findings)

- `GXCopyTex`: **m415 (Stamp Out!) copies the shadow-map copy destination into another texture on the CPU**. It is presentation only, and no logic reads the bytes. Party Board's `TARGET_PC` branch aliases the texture pointer instead, and we keep that branch. m415 is allow-listed in the copy-destination debug trap.

## Proposed changes elsewhere (for architect)

- **ADR 0009 §1 lock table:** `aurora` moves from "only if spike S1 adopts it" to **required**. Its role becomes "GX front end, WebGPU renderer, MTX and Dolphin headers (GD for later titles)". Add a row for **`dawn`** (`https://dawn.googlesource.com/dawn`, or the GitHub mirror `google/dawn`; commit `13abc3bc8ea2d3c2050f9e77a12d012108ceee24`; `BSD-3-Clause`; role: WebGPU implementation, built from source, never prebuilt).
- **Phase 1 plan:**
  - P1-14's dependencies gain "S1 gate (ADR 0007 §6)".
  - R6 notes the S1 coverage numbers: 104/131 implemented at the pin, one real gap (Z textures), three open Party Board issues.
  - The build row's SDL3 check is answered: SDL_GPU has no Linux driver with the current features. That matters only under fallback (c).
- **`vcpkg.json` (architect-owned):**
  - add `abseil`, `xxhash`, `fmt`, `libpng`, `sqlite3`, `zstd` and `tracy` for P1-14;
  - add the `sdl3` `vulkan` feature only if fallback (c) is triggered.
- **Toolchain note for P1-14/P1-15:** ADR 0008 §4 requires Clang. The reference development machine currently has Visual Studio without its LLVM/clang-cl component.

## Alternatives considered (to add to ADR 0007's Alternatives)

- **(b) Our own GX layer on SDL_GPU from scratch.** Rejected:
  - It starts at zero coverage against about 20k lines of mature aurora front end and renderer.
  - It would still have to reproduce aurora's ABI, because Party Board's structs embed aurora's 64-bit `GXTexObj`/`GXTlutObj` and its code calls aurora's extensions.
  - It needs a runtime shader compiler for TEV pipelines anyway.
  - Its one structural advantage, sharing a device with the presenter, is not needed until the GPU hand-off ADR.
- **aurora including `aurora_core`.** Rejected: it owns the window, events and input, and it requires a presentable surface.
- **aurora's prebuilt Dawn package.** Rejected: it is fetched by tag with no hash, so it cannot be pinned.
