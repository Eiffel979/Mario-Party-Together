# ADR 0007: GameCube/Wii SDK replacement layer

**Status:** Accepted, **except §6** (graphics backend), which stays open until spike S1 (P1-12) reports and is then added as an amendment.
Author: architect, 2026-10-08. Amended after P1-01 (`docs/titles/mp4.md`) on 2026-10-08. Approved by: architect, 2026-10-08 (§1–5, §7, Alternatives and Consequences).

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
6. **Graphics backend: open, decided by spike S1** (P1-12, 2 weeks), then recorded here as an amendment. Candidates:
   - **(a) Adopt aurora** (`encounter/aurora`, MIT, candidate pin `514339438178ef2bed1b14e5149d90ece0c6e0cc`, the commit Party Board pins), **using only its GX, GD and MTX parts**. Its OS, VI, PAD, DVD, CARD and SI parts are **not** used: its OS time reads the host clock, and its DVD/CARD paths bypass host-owned save media.
   - **(b) Our own GX layer on SDL_GPU** (Vulkan/D3D12/Metal through the SDL3 we already ship).

   **S1 should weigh (a) heavily.** Party Board's game code is already written against aurora's headers, and its open graphical bugs (m402, m406, m427) mark where aurora is still incomplete. Criteria:
   - (a) can be used as GX/GD/MTX alone, without owning the window and without pulling in its OS layer;
   - its MTX results meet ADR 0008's strict-FP rules (no FMA contraction, no platform-dependent SIMD), or are replaced by our C versions;
   - license;
   - coverage of the GX features MP4 uses (TEV including swap tables and konst colours, indirect textures, Z textures, fog, lights, CI textures and TLUTs, EFB copies);
   - headless/null support;
   - sharing the shell's window/device later;
   - build weight (aurora brings Dawn);
   - Linux availability with our trimmed SDL3 vcpkg features;
   - pinnability.

   If (a) wins, its pin moves from "conditional" to required in `decomp.lock` (ADR 0009 §1) and `docs/titles/mp4.md`.
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

## Consequences
- GX golden-image tests use **our own** synthetic display lists and textures (P1-21), never game assets. MusyX and THP tests use **synthetic** sound data and synthetic THP streams that we generate, never game files.
- P1-13 includes MusyX audio stepping and THP decoding, not only OS/DVD/PAD/VI/CARD/AR.
- The MusyX sequencer and voice state, and THP playback state, are hash-manifest areas (ADR 0008 §8).
- A GPU texture hand-off (instead of readback) needs its own ADR. It is written only if E7 performance fails, or when the presenter moves to the GPU.
- MP8's Wii additions (WPAD, motion) extend this same layer in Phase 4.

## Review (architect, 2026-10-08)
**Verdict: Accepted for §1–5, §7, the Alternatives and the Consequences. §6 stays open.**
- First review: §1–5 do not depend on the backend, so P1-13 (SDK core) can start at gate G1. P1-14 (GX renderer) waits for the §6 amendment.
- **Amended after P1-01:**
  - Audio is MusyX's PC synth in pull mode, stepped inside `stepFrame`. This replaces both the AX mixer and my earlier "MusyX over our AX" default, which P1-01 showed to be wrong: MP4 has no AX.
  - DVD, ARAM and CARD complete synchronously.
  - THP movie decoding is in scope.
  - Hardware-setting getters return fixed values.
  - The MP4 readback findings are recorded.
  - S1 now weighs aurora heavily, but only its GX/GD/MTX parts.
- Nothing in this layer reads RVZ or other containers. Disc containers are asset-import's concern (Phase 1 plan D9, §12).
