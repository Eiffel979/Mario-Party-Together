# Phase 1 plan: Vertical slice, Mario Party 4

**Status:** Approved plan. User decisions recorded in §10. **Gate G1 signed off 2026-10-08** (§6, §10). The MP4 game source is **Party Board** (ADR 0009).
**Author:** architect, 2026-10-08. Decisions folded in 2026-10-08, then P1-01 findings folded in 2026-10-08.
**Baseline:** `main` at 4093e42 (Phase 0 merged, ADR 0002 accepted)

## 1. Goal and scope

From `docs/PLAN.md` §7:

> **Goal:** the MP4 decomp running behind `ITitleRuntime` with a PC GX layer.
> **Exit:** a full local 4-player party on keyboard/controller, and a deterministic replay test that passes.

Since P1-01, the MP4 game source is **Party Board** (`mariopartyrd/partyboard`), the decomp organisation's CC0 PC port of the decomp's game sources. We port it onto our SDK layer and determinism model. The decomp (`mariopartyrd/marioparty4`) is a read-only reference (ADR 0009).

Phase 1 proves the whole architecture end to end on one title: user dump, then local build, then runtime, then shell (video, audio, input), then the Party Director mirror, then replay determinism. Every later title reuses what this phase builds. The GameCube SDK replacement layer, the session loop, the replay format and the dump-gated test lane are all shared infrastructure, not MP4-specific code.

### In scope
- `docs/titles/mp4.md` feasibility notes. **This is the gate:** no MP4 runtime code is written until they are accepted (P1-01). **Signed off by architect on 2026-10-08.**
- Core contract v1: native frame rate, persistent save media, party phase (ADRs 0003, 0004, 0005).
- `Session` and `PartyDirector` v0 in `core/`. The Director is a **read-only** mirror in this phase (ADR 0005).
- Replay format v0 and a dump-gated test lane (ADR 0006).
- The shell: CPU-frame presenter, audio output, frame pacing from the runtime's rate, replay record/playback, title registration, user data and cache directories.
- `input/` v0: keyboard plus SDL gamepads mapped onto logical pads for 4 local ports, and a default MP4 profile.
- `asset-import`: GameCube disc file-system extraction, a build-orchestration framework, and MP4 entries in `known_dumps.json`.
- `runtimes/gc_wii/`: the shared Dolphin-SDK replacement layer (OS, DVD, AR/ARQ, PAD, VI, CARD, AI with **MusyX audio**, **THP movie decoding**, MTX, GX), with GX built on the backend chosen in spike S1 (ADR 0007).
- `adapters/mp4/` and the MP4 game module: `Mp4Runtime`, `Mp4Adapter` (catalog, `readParty`).
- `ui/` v0: a minimal launcher (D6): library and import flow, local-party seating, pause overlay.
- Test infrastructure: a runtime conformance suite, sequential and cross-process determinism harnesses, golden tests for the SDK layer, replay fixtures, and compliance-scanner updates.

### Non-goals (explicitly out of Phase 1)
- Online play of any kind, a lobby, the room browser, `server/` (Phase 2).
- The ruleset schema, presets or applying rules. `applyRuleset` only has to accept an empty (all-default) ruleset. `writeParty` is never called (Phase 2).
- **Cross-platform `SaveState` portability.** Phase 1 save states are same-build, same-platform only. This is a time-boxed exception to ADR 0002 rule 5, recorded in ADR 0008, and it expires at Phase 2 entry.
- A GPU texture hand-off from the runtime to the presenter. Phase 1 uses `cpu_rgba8` (ADR 0002 rule 8). A GPU handshake ADR is written only if exit criterion E7 cannot be met with readback.
- Compressed disc containers (RVZ, WIA, GCZ, WBFS, CISO) (D9). Plain `.iso` only. NKit images are also out, because they do not hash as retail dumps.
- Every MP4 dump except USA Rev 0 (`GMPE01`, version byte `0x00`, decomp version `GMPE01_00`) (D3). That includes USA Rev 1, which the decomp also matches, and PAL/JP, which it does not. PAL 50 Hz is supported by the contract (ADR 0003) but not exit-tested.
- MP5–MP8, the N64 titles, cross-title play, `launchMinigame`, and more than 4 players.
- End-user distribution: an installer, a bundled toolchain, a public release. Phase 1 builds are for developers only (D1, ADR 0009). No build of the MP4 game module, and nothing built from the pinned game source, leaves a developer's machine.

## 2. Starting point: what Phase 0 actually delivered

Read from the code on `main`, not from the Phase 0 goals.

| Area | What exists | Gap or debt Phase 1 must absorb | Absorbed by |
|---|---|---|---|
| `core/` contracts | v0 headers per ADR 0002. `TitleRegistry`. No Director, no session. | No frame rate (shell hard-codes 60 Hz, `core/shell/app.cpp:25`). No save-media channel (rule 11, due before Phase 1 exit). `PartySnapshot` cannot say "party over". Header hygiene from the ADR 0002 review is not done (`InputFrame` static_asserts, comments for rules 2/3/6/7/9/10, the seed relation). | P1-03, P1-04 |
| Shell (`core/shell/`) | SDL3 window, fixed 60 Hz loop that clears the screen, `--headless`, `--frames`, `--identify`. | No presenter for `FrameOutput`, no audio device, no input sampling, no title selection or registration path, no user data or cache directory, no replay I/O. `--identify` loads `known_dumps.json` from next to the executable. | P1-06 |
| `asset-import/` | Format detection, N64 normalization, streaming SHA-256, `known_dumps.json` loader, `make_verified()`. | `known_dumps.json` is **empty**. No GC disc FST reader, no extraction, no build step, no cache management. Public dump databases (Redump) publish SHA-1/MD5/CRC32, not SHA-256. | P1-09, P1-10, P1-11 |
| Tests | `FakeRuntime`, `run_replay`, side-by-side determinism tests, compliance scanner and self-test, headless smoke test. | Determinism tests run instances **side by side**, which ADR 0002 rule 10 forbids for real runtimes. No cross-process or cross-platform hash comparison. No conformance suite. No way to run tests that need a user dump. | P1-20b1, P1-20b2, P1-20c, P1-20d, P1-21 |
| Compliance | Scanner (extensions, signatures, NUL bytes, 1 MB cap) in CI and pre-commit. Empty allowlist. | It does not know about memory-card images (`.gci`, `.gcs`, `.sav`, `.raw`), game-source checkouts (`external/`), or replay files. Synthetic GC disc fixtures carry the disc magic and would be flagged, so they must be generated at test time. | P1-20e1, P1-20e2 |
| Build | CMake + vcpkg (SDL3 with trimmed features, nlohmann-json, doctest). | SDL3 is built with `default-features: false`. S1 measured SDL_GPU with the current features: only `direct3d12`, so there is no Linux driver. That matters only under ADR 0007 §6 fallback (c), which would add the `sdl3` `vulkan` feature. No `input/`, `ui/`, `runtimes/` or `adapters/` targets. | P1-06, P1-12 |
| Docs | PLAN, ADR 0001, ADR 0002, BUILDING. | **No `docs/titles/` at all.** The MP4 gate (architect responsibility 5) is unmet. No phase-plan convention (this document starts one). | P1-01, PLAN.md §9 |

## 3. Phase 1 architecture

```
            ┌──────────── shell (core/shell, SDL3) ─────────────┐
 SDL events │ input::Mapper ──► InputFrame ──► ReplayWriter      │
            │        │                                            │
            │        ▼                                            │
            │  Session::step(InputFrame)    (core, no SDL, pure)  │
            │     ├─ ITitleRuntime::stepFrame                     │
            │     └─ PartyDirector::observe(adapter.readParty())  │
            │        │                                            │
            │        ▼                                            │
            │  video() ─► Presenter (SDL texture)  audio() ─► SDL_AudioStream
            │  frameRate() ─► pacing     saveMedia() ─► user save dir
            └─────────────────────────────────────────────────────┘
                     ▲ registered via generated register_titles()
   runtimes/gc_wii/mp4  (host side: Mp4Runtime, loads the module at a fixed base)
         │ C ABI
   MP4 game module   (Party Board game sources + MusyX + our SDK layer and shims,
                      built locally from pinned sources, loaded from build_dir;
                      DOL data blobs read at runtime from build_dir)
```

The rules that make this hold together:
- **`InputFrame` is the determinism boundary.** Everything left of it (SDL, devices, wall-clock pacing) is host-side and nondeterministic. Everything right of it is a pure function of dump, seed, initial save media and the `InputFrame` sequence (ADR 0002 rule 2, as amended by ADR 0004).
- **`Session` is pure.** It has no clock, no SDL and no filesystem access, so the same object runs live, under replay, in tests and later under netplay.
- **`core/` never names a title.** The shell calls a `register_titles(TitleRegistry&)` function that CMake generates from the enabled runtime targets (P1-06).

## 4. Exit criteria

Phase 1 is done when all of these hold. Each says how it is checked.

| # | Criterion | How it is verified |
|---|---|---|
| E1 | **Full local party.** A tester with their own verified MP4 dump (`GMPE01_00`) imports it through the launcher UI, then starts and finishes a party: 4 human seats on a mix of keyboard and at least 1 controller, at least 10 turns, any regular board, through to the final results. Done once on Windows and once on Linux, with no crash or soft-lock. | Manual sign-off checklist in `docs/titles/mp4.md`. The session's replay is kept (locally) as evidence. The Director reports `phase == finished`. |
| E2 | **Input coverage.** Every **party-eligible** minigame (the categories a regular party can launch, as confirmed by P1-17 from the game's minigame-setup code; Extra Room and Story-only minigames are excluded) has a working mapping on keyboard and on a standard controller. | Coverage table in `docs/titles/mp4.md`, one row per party-eligible minigame from the `docs/titles/mp4.md` §12 catalog, each signed off (input-mapping + runtime-gc-wii). |
| E3 | **Replay determinism.** A recorded full-party replay (from E1) played back headless produces a hash trail (every 60 frames) that is identical (a) to the trail recorded live, (b) across 2 runs in separate processes, and (c) on Windows and Linux. Part (c) is required at Phase 1 exit (D5). | Dump-gated test `replay_mp4_full_party` (label `dump`), run on the manual local lane (D4) on both OSes. The golden trail is committed next to the replay fixture, and both OSes must match it. |
| E4 | **Hash coverage.** Changing the seed, or flipping one input bit at frame N, changes the trail at the first checkpoint after N. A perturbation test exists for each area of the MP4 hash manifest (ADR 0008). | Dump-gated tests `mp4_hash_perturbation_*`. |
| E5 | **Save state round trip** (same build, same platform). Save at frame N, load into a **fresh process**, continue: the trail matches an uninterrupted run. **Narrowing rule:** if fixed load bases for the game module and arena (ADR 0008 §11) are not demonstrated on both OSes by milestone M1, E5 narrows to **in-process** (save, destroy the runtime, create a fresh runtime in the same process, load). architect records the narrowing here, and the fresh-process case moves to Phase 2. | Dump-gated cross-process test (or in-process test if narrowed). |
| E6 | **Presentation independence.** The trail is identical with renderer null vs real, `internal_scale` 1 vs 2, and widescreen on vs off. | Dump-gated test matrix. |
| E7 | **Performance.** At `internal_scale` 1, with CPU readback, on the reference machine (D7, the user's Windows 11 development PC): p99 frame time under one frame period (16.68 ms at MP4's 60000/1001 Hz) and no audio underruns over a 10-turn party. | Shell `--stats` output from a replay run with real rendering and audio. The hardware is recorded in the D7 table now. Its measurement fields (driver, RAM speed, build, pins, date) are completed when E7 is first measured. A result without them does not count. |
| E8 | **Compliance.** CI's scanner passes on every Phase 1 commit. No game source (Party Board or decomp), extracted disc files, save images, game-module binaries or dumps are tracked. `external/` is ignored and rejected by the scanner. No wall-clock, unseeded RNG or thread-timing reads in `runtimes/gc_wii/`, `adapters/mp4/` or the game-logic side of `input/`. | CI compliance job, plus the qa-reviewer audit (P1-22) with a recorded grep checklist. |
| E9 | **Contracts.** Core contract v1 is merged (`kCoreContractVersion == 1`). ADRs 0003–0009 are Accepted (all done except ADR 0007 §6, which spike S1 settles). `FakeRuntime` and `Mp4Runtime` both pass the shared runtime conformance suite. | CI (fake) plus the dump lane (MP4). |
| E10 | **Public CI is green** on Windows and Linux, including SDK-layer synthetic tests: GX golden images from our own display lists, math-library golden values, synthetic FST extraction, CARD over SaveMedia, cooperative-thread scheduling determinism, virtual-time quantum and interrupt ordering, MusyX stepping on synthetic sound data, THP decoding of a synthetic stream we generate. | `ci.yml`. |
| E11 | **Import.** Unknown, truncated and wrong-title dumps are rejected with a clear message. A known but unsupported MP4 revision or region (for example `GMPE01` Rev 1, PAL, JP) and an RVZ/compressed image each get their own message naming the reason, not "unknown dump". A known MP4 dump builds once into the cache and is not rebuilt unless the dump hash, the runtime build version or the recipe version changes. | `asset-import` unit tests with synthetic discs, plus a dump-gated end-to-end test. |

## 5. Work breakdown

Each item has exactly one owner and stays inside that owner's paths (`PLAN.md` §6). "Contract" names what the item must satisfy. "Test" says how acceptance is checked.

### architect (`core/`, `docs/adr/`, `docs/PLAN.md`, `docs/phases/`)

| ID | Item | Files | Contract | Depends on | Test / acceptance |
|---|---|---|---|---|---|
| P1-02 | Take ADRs 0003–0009 from Proposed to Accepted. **Done.** 0003–0009 are all Accepted (0009 rewritten around Party Board with real pins; 0007 §6 folded in from spike S1). P1-01 findings folded into 0004, 0007 and 0008. | `docs/adr/` | — | D1–D9, P1-01, P1-12 | Each ADR has a Review section and Accepted status. |
| P1-03 | **Core contract v1 headers.** **Done on branch `p1-03-contract-v1` (524145e, 8b8f1a4), merges together with P1-20a.** qa-reviewer's review findings are folded into ADR 0002 amendments 12–14, ADR 0004 rules 9–10 and the ADR 0005 `current_minigame` invariant (b248e90). `FrameRate` + `frameRate()` (0003). `SaveMedia`, `RuntimeConfig::save_media`, `saveMedia()` / `saveMediaGeneration()` (0004). `PartyPhase` and `current_minigame` in `PartySnapshot` (0005). ADR 0002 review hygiene (`InputFrame` size/trivially-copyable/endianness static_asserts, comments for rules 2/3/6/7/9/10, seed relation). **Document the pad conventions in `input.h`** (requested by input-mapping, P1-07): sticks are +X right and +Y up; `input/` produces stick values in the symmetric range -32767..32767, and runtimes must accept -32768 and treat it as -32767; triggers are 0 (released) to 255 (fully pressed). This is a comment-level clarification of the existing fields, not a layout change. Bump `kCoreContractVersion` to 1. | `core/include/mpt/core/*.h` | ADRs 0003–0005 | 0003–0005 Accepted | Lands **in the same PR as P1-20a** (FakeRuntime update) so `main` stays green. |
| P1-04 | **`Session` + `PartyDirector` v0.** **Done on branch `p1-04-session-director` (stacked on P1-20b2, PR #10). Details pinned in the ADR 0005 P1-04 amendment.** Single-title session, a read-only Director mirror, `PartyEvent`s derived from snapshot diffs. | `core/include/mpt/core/session.h`, `party_director.h`, `core/src/` | ADR 0005 | P1-03 | Unit tests with `FakeRuntime` + a `FakeAdapter` (P1-20b2): frame numbering, events, refusal to create a second instance of a single-instance runtime. |
| P1-05 | **Replay library v0.** `ReplayWriter` / `ReplayReader`, RLE input stream, hash-trail checkpoints. **Done on branch `p1-05-replay`.** Details in the ADR 0006 P1-05 amendment: each checkpoint carries a `party_digest` of the Director's events; save-media mapping through `make_replay_header` / `session_config_for`; `ReplayRecorder`, `verify_replay`; `validate_replay_fixture` and the `mpt_replay_check` tool for P1-20e2 and P1-20d. The golden encoding is `tests/golden/replay_v0_synthetic.mptr.hex`. | `core/include/mpt/core/replay.h`, `core/src/replay.cpp` | ADR 0006 | P1-03 | Round-trip and corruption tests on synthetic replays. Byte-identical output on Windows and Linux (golden file). |
| P1-06 | **Shell integration.** (a) Generated `register_titles()` from enabled runtime targets. (b) User data, cache and save directories via `SDL_GetPrefPath`. (c) Presenter for `cpu_rgba8` (aspect-correct letterbox). (d) Audio output via `SDL_AudioStream` with resampling. (e) Pacing from `frameRate()` using integer nanoseconds. (f) `--title`, `--dump`, `--record`, `--replay`, `--verify-replay` (headless, prints the trail), `--stats`. (g) Persist save media when its generation changes. Compare the generation only across each `stepFrame()`. A `loadState` (rewind, rollback) is a resync point that resets the baseline and persists nothing (ADR 0004 rules 9–10). (h) Pause and quit. (i) **UI seams for ADR 0010:** the shell-internal `IOverlay` (one overlay pass per presented frame, on the presenter's render target) and `IShellCommands` (start/pause/resume/stop requests from `ui/`). SDL events go to the UI while a screen has focus, and to `input/` otherwise. The session is not stepped while a screen has focus. The host pause action is consumed before the mapper. | `core/shell/`, `core/CMakeLists.txt`, root `CMakeLists.txt` | ADRs 0003–0006 | P1-04, P1-05, P1-07 | Headless smoke tests with `FakeRuntime` registered through a test-only registration. `--verify-replay` on a synthetic replay prints the golden trail. A replay's hash trail is identical with the UI enabled and disabled, including a pause and resume mid-run (ADR 0010 §3.5). |

### runtime-gc-wii (`runtimes/gc_wii/`, `adapters/mp4/`, `docs/titles/mp4.md`)

| ID | Item | Files | Contract | Depends on | Test / acceptance |
|---|---|---|---|---|---|
| **P1-01** | **GATE: MP4 feasibility notes.** **Done, signed off 2026-10-08** (`docs/titles/mp4.md`, recommendation "go with conditions"). It answered every question ADRs 0007–0009 put to P1-01 (now recorded as "Answered by P1-01" or folded into their Context), and its conditions are folded into ADRs 0004, 0007, 0008 and 0009 and into P1-13 and P1-15–P1-17. | `docs/titles/mp4.md` | — | — | architect sign-off (given). |
| P1-12 | **Spike S1: GX backend.** Time-boxed to 2 weeks. Compare adopting **aurora's GX, GD and MTX parts only** (candidate pin `5143394`, MIT) with our own GX layer on SDL_GPU, **weighing aurora heavily** because Party Board's game code is written against its headers. Criteria (ADR 0007 §6): usable without its OS/VI/PAD/DVD/CARD layers and without owning the window, MTX meets strict FP or is replaced by our C versions, license, renders our synthetic GX test scenes (including indirect and Z textures, EFB copies), a null/headless mode, device sharing later, build weight (Dawn vs SDL_GPU), Linux availability with our SDL3 feature set. | `runtimes/gc_wii/spike/` (deleted after), notes in `docs/titles/mp4.md` | ADR 0007 | P1-01 | Written recommendation. architect amends ADR 0007. **Done:** `docs/spikes/s1-gx-backend.md`. Option (a) is folded into ADR 0007 §6, and the gate G-a…G-e runs at the start of P1-14. |
| P1-13a | **SDK layer core** (branch `p1-13-sdk-core`, in review). Split from P1-13 on 2026-10-09 so that the row is not marked done while audio and movies are missing. **OS:** virtual clock (seed-derived epoch, field clock as a floor, fixed quantum per time query, ADR 0008 amendment after P1-13 §2), virtual interrupts at SDK call boundaries, coroutine threads and game processes on **arena-allocated stacks** with **our own context switch** (ADR 0008 amendment §1), alarms, the single arena, fixed hardware answers. **DVD and AR/ARQ:** over the extracted FS, **synchronous only**. Callbacks run at the next SDK call boundary by default, with a per-title before-return option (amendment §3). **PAD:** canonical `Button` → GC pad, analog triggers, sampling at the VI post-retrace point. **VI:** retrace callbacks driven by `stepFrame`. **CARD:** over `SaveMedia`, synchronous, stable serial (ADR 0004 r7). **Math:** correctly rounded `mpt_fmaf`, our trig, saturating fast casts under strict FP. **No `PS*`/`C_*` MTX symbols:** aurora's MTX owns them (P1-14, amendment §4). **Build:** `runtimes/gc_wii` from the root (commit `09ec567` approved). A missing Clang is a warning for local configures and a **configure error when `MPT_REQUIRE_GC_WII=ON`**, which the `ci-*` presets set. runtime-gc-wii adds the option, and the preset change, in the same commit. **History:** no squash. The root build does not include `runtimes/gc_wii` until `09ec567`, so `main` bisects cleanly. The PR description says the standalone Clang build first succeeds at `f6fe8e7`. | `runtimes/gc_wii/sdk/`, `runtimes/gc_wii/CMakeLists.txt`, the root line | ADRs 0004, 0007, 0008 | P1-01, P1-03 | Synthetic unit tests (with P1-21): scheduling determinism, time-quantum and interrupt ordering, a busy-wait loop that terminates, CARD round trip with stable serial, `mpt_fmaf` and trig goldens, PAD mapping. **Context-switch tests** on both CI OSes: every callee-saved register (Win64: including `xmm6`–`xmm15` and the TIB stack fields), MXCSR and the x87 control word survive a switch. |
| P1-13b | **SDK audio and movies.** **MusyX PC synth in pull mode**, stepped inside `stepFrame` on virtual AI-DMA ticks (the `ai` interrupt source and the `host_tick` event kind are already reserved), with a null output that still runs the sequencer, and never MusyX's threaded SDL backend. **THP decoding** inside the frame step, with movie audio in the same stream. MusyX and THP state go into the SDK's hash area. | `runtimes/gc_wii/sdk/` | ADRs 0007 §4, 0008 §8 | P1-13a | MusyX output and sequencer state identical with null vs real output, on synthetic sound data. THP decode of a synthetic stream we generate. Both required by E10. |
| P1-14 | **GX renderer** on the S1 backend (ADR 0007 §6: aurora GX front end, renderer and MTX on Dawn, without `aurora_core`). It starts with **gate G-a…G-e** (at most 5 working days for G-a/G-b, otherwise fallback (c)). **Gate: passes on Windows (PR #11, `docs/spikes/p1-14-gate.md`). The Linux leg is the first P1-14 CI job** (lavapipe plus Dawn's Null adapter, ADR 0007, P1-14 gate amendment). Then: the host shim, offscreen render → `cpu_rgba8` readback, the null backend without Dawn, patches 1–7 plus 8 (FIFO observer), 8b (`comp_type_size`/`comp_cnt_count` into the front end) and 9 (`use_dxc` off, FXC), **synchronous pipelines in play** with the persistent cache in the user cache directory, the MTX target (the only `PS*`/`C_*` implementation, ADR 0008 amendment after P1-13 §4), and the missing SDK entry points. **Prerequisites:** clang-cl is installed on the reference machine (22.1.3). Root build changes landed by architect in the first P1-14 PR: vcpkg `abseil`, `xxhash`, `fmt`, `libpng`, `sqlite3`, `tracy` (headers only) in a `gc-wii` manifest feature, with **no `zstd`**; `MPT_GX_BACKEND=dawn|null` (default `null`) and `MPT_DAWN_PREFIX`; `windows-clang-*` presets; and the `sdl3` `vulkan` feature only under fallback (c). Dawn is built outside the tree into `external/` from `dawn.lock` (ADR 0009 amendment §2), with `DAWN_FORCE_SYSTEM_COMPONENT_LOAD=ON`. qa-reviewer extends the scanner to reject a Dawn source tree, `webgpu_dawn.dll` and `pipeline_cache.db`/`dawn_cache.db`. | `runtimes/gc_wii/gx/` | ADRs 0002 r7/r8, 0007, 0008 §5 | P1-12, P1-13a, S1 gate | Golden-image tests from **our own** synthetic display lists (P1-21). Null and real backends give the same `stateHash`. MTX golden values against aurora's MTX, including the hard rounding cases for `PSVEC*` (ADR 0008 §5 condition 2). |
| P1-15 | **MP4 game module: port Party Board onto our layer** (not a port of the decomp from scratch). **Sources:** `decomp.lock` with the Party Board, musyx (and, when adopted, aurora/libco) pins. Fetched by hash, verified, tree-hash check for archives (ADR 0009 §1). **Build:** recipe for asset-import targeting `GMPE01_00` only, with no data-extraction step. Its game sources are compiled with Clang under strict FP, with our target macro where its `TARGET_PC` code assumes its own port layer. Its main loop, UI, frame limiter, per-REL DLL loader and aurora OS/DVD/CARD parts are not used. **Required pieces:** (1) **REL symbol isolation**: every REL linked partially and localised except a renamed entry point, all in one module. (2) **Pristine `.data` restore** and `.bss` zeroing on each overlay load, through a generated overlay table and a loader shim replacing `omDLLLink`/`omDLLUnlink`. (3) The **runtime DOL data reader**, adapted from Party Board's `dolassets`, which reads blobs from the extracted DOL/RELs in `build_dir` through a per-revision offset table. (4) The **64-bit HSF model-format conversion**, from Party Board's `byteswap`, plus review of the pointer-to-integer casts. (5) A constant caller address in heap headers. (6) The `ztardll` low-memory read patched or mapped. (7) The C ABI to the host runtime. Patch files against the CC0 pins are allowed (ADR 0009 §5). Nothing from `marioparty4` is committed. | `runtimes/gc_wii/mp4/` (recipe, shims, patches, lock file) | ADRs 0007, 0008, 0009 | P1-01, P1-10, P1-13, P1-20e1 | Developer build succeeds on Windows and Linux from a clean cache, from the pinned hashes. Nothing from the game source or the dump appears in `git status`. A build from a local override that does not match the lock is marked `unpinned` in its manifest. A unit check that two REL-local symbols with the same name resolve independently, and that reloading an overlay restores its `.data`. |
| P1-16 | **`Mp4Runtime : ITitleRuntime`.** boot, stepFrame (one VI field per step), video/audio, `frameRate()` = **60000/1001**, save media (stable serial, synthetic save dates, ADR 0004 r7/r8), same-platform `saveState`/`loadState`, `stateHash` over the hash manifest (including MusyX and THP state). **Fixed load base for the module and a fixed arena address** on both OSes (ADR 0008 §11). Registration function. Check that no game logic compares save timestamps against the clock (ADR 0004 r8). Choose MP4's I/O callback mode by auditing every completion-wait loop (`game/dvd.c`, `game/data.c`, `game/armem.c`, `game/card.c`, `msm/msmstream.c`), and record it in `docs/titles/mp4.md` (ADR 0008 amendment after P1-13 §3). **Function-pointer registration table.** | `runtimes/gc_wii/mp4/` | ADRs 0002, 0003, 0004, 0008 | P1-13b, P1-14, P1-15 | Conformance suite (P1-20c) on the dump lane. Milestones M1–M2. **By M1:** fresh-process save/load demonstrated on both OSes, or the E5 narrowing is triggered. **For E4:** today the SDK hashes every function pointer and every out-of-arena pointer as one sentinel. The registration table must hash function pointers as stable IDs, and turn an out-of-arena pointer into a fault or a registered ID, so that perturbation tests can see a changed callback (ADR 0008 amendment §5). |
| P1-17 | **`Mp4Adapter : ITitleAdapter`.** catalog (content IDs + display names as text, no assets, per `mp4.md` §12), the confirmed list of **party-eligible minigames** for E2, `readParty` including phase, `applyRuleset` accepts only an empty ruleset, `launchMinigame` returns failure, `writeParty` documented as unused in Phase 1. | `adapters/mp4/` | ADRs 0002, 0005 | P1-16 | Dump-gated test: replaying a full-party fixture yields the expected `PartyEvent` sequence and a final `PartySnapshot`. All IDs parse and have the expected kind. |
| P1-18 | Keep `docs/titles/mp4.md` current: hash manifest, known issues, E1 sign-off checklist, coverage table (input-mapping fills the input columns). | `docs/titles/mp4.md` | — | ongoing | Reviewed at the exit review (P1-22). |

### asset-import (`asset-import/`)

| ID | Item | Files | Contract | Depends on | Test / acceptance |
|---|---|---|---|---|---|
| P1-09 | **GC disc reader.** Disc header, FST parsing, extraction into the cache. Reject path traversal, overlapping or out-of-range entries, and oversize files. | `asset-import/src/gc_disc.*` | ADR 0001 | — | Synthetic discs **generated in the test at run time** (never committed, because the disc magic trips the scanner). Malformed-FST fuzz cases. |
| P1-10 | **Build orchestration.** A `BuildRecipe` interface that runtime owners implement. Cache key = dump sha256 + game-source pins + runtime build version + recipe version + **toolchain identity** (compiler ID, full version and target, never the path; `check_toolchain` runs on cache hits too, ADR 0009 amendment §1). **Before merge:** `Importer::make_verified` builds the `VerifiedDump` from the `BuildOutcome`, and the `cache_root` overload goes (amendment §3). The lock gains `deps`/`skipped_deps` for Dawn (amendment §2), needed by P1-14. Source fetch by commit hash with verification, tree-hash verification of user-supplied archives, and `unpinned` marking for local overrides (ADR 0009 §1). Build into a temp dir, then rename atomically. Manifest file. Progress callback for the UI. Clear errors (for example "toolchain not found"). | `asset-import/include/.../build.h`, `src/build.cpp` | ADR 0009 | P1-09 | Unit tests with a fake recipe: caching, invalidation, interrupted builds (E11). |
| P1-11 | **MP4 known-dump entries.** Schema v2: add `sha1` (and optionally `md5`) so entries can be cross-checked against public databases (D8: public SHA-1 is allowed as metadata), and compute SHA-1 and SHA-256 in one pass. Record the disc ID and version byte per entry. One **supported** entry, `GMPE01` / `0x00` (`GMPE01_00`). Its SHA-256 is computed locally from a verified disc and committed as a hash only. Other MP4 revisions and regions may be listed as **known but unsupported** so E11 can name them. | `asset-import/known_dumps.json`, `known_dumps.*` | ADR 0001 | D3, D8 | Parser tests. The real dump identifies as `GMPE01_00` on the dump lane, and its SHA-1 matches the public record. |

### input-mapping (`input/`)

| ID | Item | Files | Contract | Depends on | Test / acceptance |
|---|---|---|---|---|---|
| P1-07 | **`input/` v0.** SDL3 keyboard and gamepad devices with hot-plug. Assignment of devices to ports 0–3. Logical pads → `PadState`. Keyboard stick emulation (full and half deflection), triggers. Profile file format (JSON). Sampled once per frame by the shell, and `InputFrame` is the only output. | `input/` | ADR 0002 `input.h` | — | Unit tests on mapping functions with synthetic device state. Profile parse tests. No SDL calls in the mapping core (pure functions). |
| P1-08 | **MP4 profile and coverage.** The default GC-pad profile for MP4, per-minigame overrides where needed, and the input columns of the coverage table in `docs/titles/mp4.md` (via a PR reviewed by runtime-gc-wii). | `input/profiles/mp4/` | — | P1-07, P1-17 (minigame list) | E2 sign-off. |

### frontend-ui (`ui/`)

| ID | Item | Files | Contract | Depends on | Test / acceptance |
|---|---|---|---|---|---|
| P1-19 | **UI stack ADR** (frontend-ui writes it, architect approves). **Done:** ADR 0010 Accepted 2026-10-08. It chooses RmlUi for player-facing screens, with Dear ImGui later for developer tools only. | `docs/adr/0010-ui-stack.md` | — | D6 | Accepted ADR (given). |
| P1-23 | **UI v0: minimal launcher** (D6). Exactly the screens listed here. Room browser, ruleset editor and settings screens beyond input assignment are Phase 2. Library screen ("Add game dump" → identify → build progress), local-party seating (devices to ports, CPU fill), launch, pause overlay (resume, quit), controller-navigable. No Nintendo art, title names shown as text. **Starts with a bring-up of at most 2 days** (the RmlUi render interface on the shell's `SDL_Renderer`, one RML screen with gamepad spatial navigation, and the UI-on/UI-off replay check against `FakeRuntime`). If it fails, ADR 0010 is amended to use ImGui for Phase 1 before any screen is built. **Prerequisite, landed by architect in the first P1-23 PR:** `rmlui` (default features) in `vcpkg.json` and `add_subdirectory(ui)` in the root `CMakeLists.txt`. | `ui/` | ADR 0010. Public APIs of `core/` (Session, via the P1-06 shell seams), `asset-import/`, `input/` | P1-06, P1-07, P1-10, P1-19 | Headless UI-logic tests where the stack allows. Manual E1 walkthrough. |

### qa-reviewer (`tests/`, `.github/`, compliance)

| ID | Item | Files | Contract | Depends on | Test / acceptance |
|---|---|---|---|---|---|
| P1-20a | Update `FakeRuntime` to contract v1. Paired with P1-03. **Done on branch `p1-03-contract-v1` (faa1153, 808ab77), merges with P1-03.** It adds a 60/50 rate switch on Select, an 8-byte counter save image written on Start, `SaveState` v2 with field-wise serialization, and stick -32768 folded to -32767. | `tests/support/` | ADRs 0003–0005 | P1-03 | CI green. |
| P1-20b1 | Sequential and **cross-process** determinism harness (ADR 0002 rule 10 follow-up), against the current `FakeRuntime`. No dependency, Wave A. | `tests/support/`, `tests/unit/` | ADR 0002 | — | Harness runs instances one after another and in two processes, and compares trails. CI green. |
| P1-20b2 | `FakeAdapter` plus a golden cross-platform hash trail for the contract-v1 `FakeRuntime`. | `tests/support/`, `tests/unit/` | ADRs 0002, 0005 | P1-20a, P1-20b1 | The same golden values on the Windows and Linux CI jobs. |
| P1-20c | **Runtime conformance suite**, parameterized by a runtime factory: rule 3 frame numbering, view lifetime, `loadState` → `stateHash` equality, presentation options not changing the hash, `frameRate` validity, save-media generation. **Also (P1-03 review):** (1) stick folding: a frame with -32768 on any stick axis gives the same `stateHash()` as the same frame with -32767 (ADR 0002 amendment 12); (2) after `loadState(s)`, `saveMedia()` and `saveMediaGeneration()` equal the producer's values at `s.frame` (ADR 0004 rule 9); (3) with an adapter factory, every `readParty()` has `current_minigame` set exactly when `phase == minigame` (ADR 0005). | `tests/conformance/` | ADRs 0002–0004 | P1-20a | Passes for `FakeRuntime` in CI and for `Mp4Runtime` on the dump lane. |
| P1-20d | **Dump-gated lane (manual, local only, D4).** CTest label `dump`. Tests skip cleanly (CTest `SKIP_RETURN_CODE`) when `MPT_DUMP_DIR` is unset. Replay fixtures and golden trails under `tests/replays/`. A documented local command (for example `ctest -L dump` with `MPT_DUMP_DIR` set to a directory outside the repo) and a PR checklist line for its result. | `tests/`, `tests/CMakeLists.txt`, PR template | ADR 0006 | P1-05 | Public CI reports the tests as skipped, not failed. The lane runs on a developer machine without any network access to CI. |
| P1-20e1 | **Scanner and `.gitignore`: forbidden paths.** Ignore and reject `external/` and memory-card image extensions (`.gci`, `.gcs`, `.sav`, `.raw`). No dependency, Wave A. | `tests/compliance/`, `.gitignore` | ADRs 0004, 0009 | — | Scanner self-tests with synthetic cases. |
| P1-20e2 | **Scanner: `.mptr` validator.** Magic, full parse, no trailing bytes, **no embedded save media**. | `tests/compliance/` | ADR 0006 | P1-05 | Scanner self-tests with synthetic replays. |
| P1-21 | **CI for the SDK layer.** Build `runtimes/gc_wii` (SDK, GX null + golden images, math goldens) on both OSes, with `MPT_REQUIRE_GC_WII=ON`, so a runner without Clang fails instead of skipping. The **first P1-14 job** is the Linux leg of the P1-14 gate: Dawn and aurora from the pins, lavapipe and Dawn's Null adapter, and null against real. Optional: a **compile-only** build of the MP4 game module from the pins (no dump, nothing runs it, no artifacts kept or uploaded, ADR 0009 Consequences). Otherwise the target is skipped cleanly. The switch is a root CMake option (for example `MPT_BUILD_MP4_GAME_MODULE`, off by default), which qa-reviewer requests from architect because root build files are architect-owned. **No dump lane on any CI infrastructure**, hosted or self-hosted (D4). | `.github/workflows/`, `tests/` | ADRs 0006–0008 | P1-13a, P1-13b, P1-14 | E10. |
| P1-22 | **Phase 1 exit review.** Compliance and determinism audit, the grep checklist in game-logic paths (`time(`, `clock`, `chrono::system_clock`, `steady_clock`, `QueryPerformanceCounter`, `GetTickCount`, `SDL_GetTicks`, `SDL_GetPerformanceCounter`, `gettimeofday`, `clock_gettime`, `rand(`, `std::random_device`, default-seeded `std::mt19937`, `std::thread`, `std::async`, and calls to `OSGetTime`/`OSGetTick` from inside our SDK layer, which ADR 0008 §2 forbids), and contract conformance. | — | CLAUDE.md, all ADRs | everything | Written review. Merge-blocking findings fixed. |

### Not in Phase 1 (optional pre-work, never on the critical path)
- **rules-engine:** draft the ruleset JSON Schema v0 and the MP4 rule-hook list, ready for Phase 2. Must not touch `ResolvedRuleset` without an ADR. The title-specific rule-options follow-up from ADR 0002 lands in Phase 2.
- **netplay:** a transport and lockstep prototype against `FakeRuntime` + `Session`, plus the network simulation harness design with qa-reviewer. Note that netplay's 8-seat goal conflicts with `kMaxPlayers == 4`. Raise an ADR before Phase 2 design freezes.
- **runtime-n64:** write `docs/titles/mp1.md`–`mp3.md` (the Phase 3 gate), including the N64Recomp and RT64 versions to pin.

## 6. Order and parallelization

```
Wave A (now, in parallel)
  P1-01 MP4 feasibility (GATE) ──────────────┐
  user decisions D1–D9 (recorded, §10)       │
  P1-07 input v0        P1-09 GC disc reader │   P1-19 UI stack ADR
  P1-20b1 harness       ADR review (P1-02)   │
  P1-20e1 scanner paths                      │
                                             ▼
Gate G1: SIGNED OFF 2026-10-08. mp4.md accepted, Party Board + musyx pinned,
         0003–0006, 0008, 0009 Accepted, 0007 Accepted except §6
                                             │
Wave B                                        ▼
  P1-03 contract v1 + P1-20a ─► P1-20b2 ─► P1-04 Session/Director ─► P1-05 replay ─► P1-06 shell
  P1-12 spike S1 (done) ─► ADR 0007 §6 (done) ─► S1 gate ─► P1-14 GX
  P1-13a/b SDK            P1-10 build orchestration          P1-20c/d, P1-20e2, P1-21
                                             │
Wave C                                        ▼
  P1-15 MP4 game module ─► P1-16 Mp4Runtime ─► P1-17 Mp4Adapter
  P1-11 known dumps       P1-08 MP4 profile       P1-23 UI v0
                                             │
Wave D (integration and hardening)            ▼
  M1 boots headless, stable trail for 10k frames, 2 processes
  M2 title screen and menus rendered, input works
  M3 one board turn plus one minigame
  M4 full local party (E1), coverage (E2)
  M5 cross-platform replay identical (E3–E6), perf (E7)
  P1-22 exit review
```

**G1 sign-off (architect, 2026-10-08).** `docs/titles/mp4.md` answers every question the plan and ADRs 0007–0009 put to P1-01. Its "go with conditions" recommendation is accepted, and each condition is now a decision in an ADR:
- license and source: ADR 0009, Party Board;
- audio, I/O and S1 weighting: ADR 0007;
- time, interrupts, stacks and heap headers: ADR 0008;
- E5 feasibility: ADR 0008 §11 and the E5 narrowing rule;
- scope: D3, with audio and THP budgeted in P1-13.

Wave B starts now. The hash manifest, the E1 checklist and the E2 table remain P1-18 work.

**Critical path:** P1-13a (SDK core) → P1-13b (MusyX audio and THP) ∥ P1-14 (after S1) ∥ P1-15 → P1-16 (fixed bases by M1) → M4 → M5. Everything in `core/`, `input/`, `asset-import/`, `ui/` and `tests/` runs in parallel with it, against `FakeRuntime`.

**Rule for every handoff:** the receiving agent gets the item ID, its files, its contract (the ADRs listed) and its test. Work that would cross an ownership boundary goes back to the owner, or to architect if it touches an interface.

## 7. Phase 1 ADRs

| ADR | Title | Changes a core contract? | Status |
|---|---|---|---|
| [0003](../adr/0003-runtime-frame-rate.md) | Runtime-reported native frame rate | Yes (v1) | Accepted |
| [0004](../adr/0004-persistent-save-media.md) | Persistent save media through the host | Yes (v1), amends ADR 0002 rules 2 and 11 | Accepted (stable card serial, synthetic save dates added after P1-01) |
| [0005](../adr/0005-session-loop-and-party-director-v0.md) | Session loop and Party Director v0 | Yes (v1: `PartySnapshot`, new core types) | Accepted |
| [0006](../adr/0006-replay-format-v0.md) | Replay format v0 and the dump-gated test lane | New core type, not an interface change | Accepted (D4: manual local lane) |
| [0007](../adr/0007-gc-sdk-replacement-layer.md) | GameCube/Wii SDK replacement layer | No | Accepted. Amended after P1-01 (MusyX, synchronous I/O, THP) and after S1 (§6: aurora GX/MTX on Dawn, gate, fallback (c)) |
| [0008](../adr/0008-decomp-port-determinism-and-state.md) | Determinism and state model for decomp ports | No. Time-boxed exception to ADR 0002 rule 5 | Accepted (D5: identical trails required; amended after P1-01) |
| [0009](../adr/0009-mp4-decomp-dependency-and-local-build.md) | MP4 game-source dependency and local build model | No | Accepted (Party Board, CC0, pinned) |
| [0010](../adr/0010-ui-stack.md) | UI stack for the launcher and overlays | No (shell-internal seams only) | Accepted (RmlUi; ImGui for dev tools only) |

ADRs 0003–0005 together bump `kCoreContractVersion` from 0 to 1, in one change (P1-03).

## 8. Test strategy

- **Public CI (GitHub-hosted, no Nintendo material):** core, Session, Director, replay, `input/` mapping, `asset-import` with synthetic discs generated at run time, the SDK layer with synthetic programs and our own GX display lists, math goldens, the conformance suite against `FakeRuntime`, and cross-platform golden trails for `FakeRuntime`. Compliance scanning of the tree and of every new commit.
- **Dump lane (manual and local only, `MPT_DUMP_DIR`, D4):** everything that needs the real game, which is E1 evidence and E3–E7. It runs on a developer's own machine, with `MPT_DUMP_DIR` pointing at a directory **outside the repo** (for example `MPT_DUMP_DIR=D:\path\to\dumps`). No dump is ever placed on CI infrastructure, hosted or self-hosted. Replay fixtures and golden trails are committed. They hold inputs and 64-bit hashes only, never save media or game data. Real-time tests run each runtime instance **in its own process** (ADR 0002 rule 10).
- **Cadence:** the PR author runs the dump lane locally before every merge that touches `runtimes/gc_wii/`, `adapters/mp4/` or the replay format, on Windows, and on Linux as well when the change can affect numerics or the hash manifest. The result (the command, OS, pass/fail, and the game-source pins it was built from) goes in the PR description, and qa-reviewer checks it is there. There is no nightly run. A self-hosted runner can be reconsidered later through an amendment to ADR 0006.
- **UBSan float-cast-overflow run** of the full-party replay on the dump lane, before exit and after any change to game-module numerics (ADR 0008 §5).

## 9. Risks

| # | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| R1 | The game source is not portable as is: data extracted from the DOL, compiler-specific code, asm leftovers. | **Low** / Med (was Med / High) | P1-01 found that Party Board already compiles and runs the game sources natively under Clang on Windows and Linux, reads DOL data at runtime and has replaced the asm. Remaining risk is in our own layer, not the sources. Fallback: the decomp after PR #663 (ADR 0009). |
| R2 | 64-bit and endianness porting (the HSF model format's 68 pointer fields, about 107 pointer-to-integer casts, overlay isolation) is larger than expected. | Med / Med | Reuse Party Board's `byteswap` conversion. Fixed-width types (no `long` in game headers, confirmed). Overlays linked statically with symbol isolation. Fallback: per-REL shared libraries, at the cost of E5. |
| R3 | Floating-point results differ between Windows and Linux. | Med / High | One compiler (Clang) for the game module, strict FP flags, no FMA contraction, MSL math from the pinned source instead of the platform libm (about 3,300 double trig calls), C versions of the paired-single math, golden tests (ADR 0008). |
| R4 | The hash manifest misses state, so desyncs go undetected. | Med / High (Phase 2) | Perturbation tests per manifest area (E4). A deep-hash debug mode for bisecting. Review in P1-22. |
| R5 | Cross-platform `SaveState` portability stays unsolved and blocks Phase 2 desync recovery. | Med / High | ADR 0008 time-boxes the exception and lists the paths to close it. Phase 2 cannot enter without an ADR. |
| R6 | GX coverage (TEV, indirect and Z textures, EFB copies) is incomplete. | Med / Med | S1: 104 of 131 GX entry points implemented by aurora at the pin. The rest are no-ops or ADR-defined stubs, with one real gap (Z textures, m419, patch 7). Party Board's open graphical bugs (m402, m406, m427) mark the other known gaps. P1-01 found **no readback that feeds game logic**, so this is a presentation risk only. |
| R7 | Legal: game code distribution, trademark. | **Low** / Med at release (was Low now / High at release) | Strict local build (D1). The game source is CC0 and the game module holds no dump-derived bytes (ADR 0009). No public release in Phase 1. Rename before release (PLAN §2.4). |
| R8 | CI cannot run the real game, and the dump lane is manual (D4), so regressions show up late or a PR skips the lane. | High / Med | Synthetic SDK coverage, the conformance suite, the PR checklist line for the dump-lane result checked by qa-reviewer. Revisit a self-hosted runner if regressions slip through. |
| R9 | MP4's threads, game processes and non-yielding busy-waits do not map cleanly onto cooperative scheduling. | Low / Med | P1-01 inventoried them (`mp4.md` §6–7). The fixed time quantum and virtual interrupts at SDK call boundaries handle the spin loops, and I/O is synchronous (ADR 0008 §2–3). |
| R10 | End users need a C++ toolchain (strict local build). | Certain (D1) / Med | Developer-only in Phase 1. A bundled toolchain or a lighter build path is decided before the first release beyond developers. |
| R11 | **License provenance.** Our build inputs are CC0 (Party Board, musyx), MIT (aurora) and ISC, to be confirmed if adopted (libco). But Party Board's game sources descend from `marioparty4`, which still has no license (CC0 is proposed in PR #663). | Low / Med | `marioparty4` is not a build input, and nothing from it is committed. Party Board's CC0 comes from the same organisation, and PR #663 shows the same intent. The risk closes when #663 merges. The user decides whether Party Board's CC0 meets their D2 condition before any distribution beyond developers (§10). |
| R12 | Unverifiable local source copies (for example the developer's `marioparty4` zip, which has no `.git` and an empty `extern/musyx`). | Low / Low | That zip is no longer a build input. The recipe fetches by commit hash, accepts an archive only if its git tree hash matches the lock, and marks other overrides `unpinned`, which never produce golden trails or exit evidence. |
| R13 | **Audio state is game-visible.** Board code polls MusyX stream, music and SFX status and a MIDI controller value, so a sound-engine divergence is a game desync. | Med / High | MusyX stepped inside `stepFrame` on virtual ticks. Null audio still runs the sequencer. MusyX state in the hash manifest, with perturbation tests on board music (ADR 0007 §4, 0008 §8, E4, E6). |
| R14 | Party Board's game sources are entangled with its own port layer (aurora, UI, frame limiter) under `TARGET_PC`. | High / Med | Our own target macro, shims, and small patch files against the CC0 pin (ADR 0009 §1, §5). Its port-only files are not used. |
| R15 | Fixed load bases for fresh-process save states fail on one OS (Linux has no fixed-address `dlopen`). | Med / Med | ADR 0008 §11 options (non-PIE plus our own loader, or relocation on load). Decided by M1. Otherwise the E5 narrowing rule applies. |
| R16 | THP movie decoding and MusyX audio are new work that Party Board never did. | Med / Low | Budgeted in P1-13b. Movies are skippable and not needed for E1. Audio is needed for E1 and E7. |
| R17 | Dawn's from-source build is too heavy or fragile for local developer builds, or Clang is missing. | Med / Med | clang-cl 22.1.3 is installed on the reference machine. Gate G-a passed on Windows (cold start about 30 min, 1.8 GB); the Linux leg runs in CI. A missing Clang fails CI configure (`MPT_REQUIRE_GC_WII`). Fallback (c) keeps the front end and replaces only the renderer (ADR 0007 §6). |

## 10. Decisions

The user answered Q1–Q9 on 2026-10-08 ("go with the architect's recommendations", plus the facts below). Each decision Dn answers question Qn. The original questions are kept in short form for traceability.

| # | Question | Decision | Recorded in | Affects |
|---|---|---|---|---|
| **D1** | How does MP4's game code reach users? | **Strict local build on the user's machine is the architecture** (option A). **Phase 1 delivers developer-only builds** (option C). Prebuilt game code (option B) is not pursued. A bundled toolchain or other end-user path is decided before any release beyond developers. | ADR 0009 | P1-15, R7, R10 |
| **D2** | Depend on community MP4 game source as a pinned external? | **Yes, by reference only, never vendored, pinned by upstream commit hash.** The user delegated the choice of source. After P1-01 it is **Party Board** (`mariopartyrd/partyboard` @ `98ac07fd1e8f7ab938bbb6fe75789fad99419010`, CC0), with musyx @ `2a0e01aeff0d06a9ee9e12966c435cb75144af9e` (CC0), and aurora @ `514339438178ef2bed1b14e5149d90ece0c6e0cc` (MIT) and libco @ `e18e09d634d612a01781168ad4d76be10a7e3bad` (ISC, to confirm) if adopted. `marioparty4` (`main` @ `147b165a83187ac9e6cfdc3bf52f2e73437b1ffd`, unlicensed, CC0 proposed in PR #663) is a read-only reference. Its merged-CC0 state is the documented fallback. The user's original condition (license confirmed before any distribution beyond developers) still stands, see A1. | ADR 0009 | P1-15, R11, R12, E9 |
| D3 | Which MP4 dump does Phase 1 support? | **USA Rev 0 only:** disc ID `GMPE01`, version byte `0x00`, decomp version `GMPE01_00`. The user owns this dump, and P1-01 confirmed its header. The decomp reports USA Rev 0 and Rev 1 as fully matching and PAL/JP as not matching. Party Board supports both USA revisions. Rev 1 is a candidate for a later phase. | ADR 0009, P1-11 | P1-11, P1-15, E1, E11 |
| D4 | Self-hosted CI runner or manual dump lane? | **Manual local dump lane only.** No self-hosted runner, and no dump is ever placed on CI infrastructure. Can be revisited later by amending ADR 0006. | ADR 0006, §8 | P1-20d, P1-21, R8 |
| D5 | Identical Windows and Linux hash trails at Phase 1 exit? | **Yes, required** (E3c). | ADR 0008 | E3, P1-15 |
| D6 | Minimal launcher UI or CLI-only? | **Minimal launcher UI** in Phase 1 (library and import, seating, launch, pause). | this plan | P1-19, P1-23, E1 |
| D7 | Reference machine for E7? | **The user's own development PC (Windows 11).** Hardware recorded now (table below). Measurement fields are completed when E7 is first measured. | this plan | E7 |
| D8 | May `known_dumps.json` carry SHA-1 from public dump databases? | **Yes**, as metadata only (no game bytes). SHA-256 stays the identity and is computed locally from a verified disc. | P1-11 | P1-11 |
| D9 | RVZ input in Phase 1? | **No.** Plain `.iso` only (the MP4 dump is an `.iso`). RVZ becomes a **requirement before MP8 and MP9** (see §12), because the developer dumps of those titles are RVZ. | §1 non-goals, §12 | P1-09, E11 |

### D7: reference machine specs (hardware supplied by the user; finish the remaining fields when E7 is first measured)

| Field | Value |
|---|---|
| OS and build | Windows 11 Pro, build 10.0.26200 |
| CPU (model, cores/threads, base/boost clock) | AMD Ryzen 5 3600, 6 cores / 12 threads, 3.6 / 4.2 GHz |
| GPU (model, driver version) | AMD Radeon RX 5700, 8 GB (driver: _tbd_) |
| RAM (size, speed) | 16 GB (speed: _tbd_) |
| Storage holding the cache | 1 TB SSD |
| Display refresh rate | _tbd_ |
| MPT build (commit), game-source pins, toolchain version | _tbd_ |
| Date measured, by | _tbd_ |

### Decisions after P1-01 (architect, 2026-10-08)

The user delegated the choice of MP4 game source. These follow from P1-01 and are recorded in the ADRs named.

| # | Decision | Recorded in |
|---|---|---|
| A1 | **Game source = Party Board** (P1-01 option a), pinned as in D2. The decomp is a read-only reference, and its post-PR #663 CC0 state is the fallback. **Open for the user before any distribution beyond developers:** does Party Board's CC0 grant satisfy D2's "confirm the license with upstream", or should PR #663 merge first? Phase 1 is developer-only either way. | ADR 0009 |
| A2 | **No data-extraction step.** DOL/REL data blobs are read at runtime from `build_dir`, so the game module holds no dump-derived bytes. | ADR 0009 §3 |
| A3 | **Audio = MusyX PC synth in pull mode inside `stepFrame`.** DVD, ARAM and CARD are synchronous. THP is in scope. S1 weighs aurora's GX/GD/MTX heavily. | ADR 0007 |
| A4 | **Virtual time:** a fixed quantum per query, virtual interrupts at SDK call boundaries, seed-derived epoch, arena-allocated coroutine stacks, constant heap-header caller addresses, a fixed module base and arena address for fresh-process save states, and a UBSan float-cast-overflow run. | ADR 0008 |
| A5 | **Save media:** a stable card serial, and **synthetic save dates** in Phase 1 (real dates later only as a new determinism input, through an ADR). | ADR 0004 r7–r8 |
| A6 | **E5 narrowing rule** (in-process if fixed bases fail by M1), **E2 = party-eligible minigames**, MP4 frame rate **60000/1001**. | §4, P1-16, P1-17 |

### Local dump locations

Developers keep dumps and any game-source checkout **outside the repo** and point the tools at them through `MPT_DUMP_DIR` (and, for development only, a game-source override directory such as `MPT_MP4_SOURCE_DIR`). Committed files never contain a developer's personal path, and nothing from those directories is ever copied into the repo. Docs use a generic example such as `MPT_DUMP_DIR=D:\path\to\dumps`.

## 11. Phase 2 hand-forward (tracked, not done here)
- Close the ADR 0008 `SaveState` portability exception before netplay desync recovery.
- Save media in netplay: the host's image sent to peers, and peers never write back (ADR 0004 consequences). GC save files contain game-made icons and banners, so they are user-local data and must never reach `server/` or the repo.
- State hash speed (ADR 0002 follow-up): the manifest keeps it small, but measure it.
- Title-specific rule options and an adapter capability query (ADR 0002 follow-up).
- More than 4 seats (netplay's 8-seat goal vs `kMaxPlayers`).
- If E5 was narrowed: fresh-process save states, folded into the ADR that closes the ADR 0008 §10 exception.
- MP4 USA Rev 1 (`GMPE01_01`) as a second supported revision, if wanted. The decomp matches it and Party Board supports it, so it costs a known-dumps entry, an offset-table variant and its own replay fixtures.
- Watch `marioparty4` PR #663. When it merges, close R11, and note the decomp as a viable fallback source in ADR 0009.

## 12. Future requirements (beyond Phase 2)
- **RVZ input in asset-import before MP8 (Phase 4) and MP9 (runtime-modern).** The developer dumps of those titles are RVZ (D9), as are most Dolphin users' dumps. The import must decompress locally to a hashable retail image and identify it by the retail hash, never by the container's hash. WIA, GCZ and WBFS can be scoped at the same time. Owner: asset-import. It needs its own ADR (container formats and which decompression library and version are pinned).
- **Real dates on saves** (ADR 0004 rule 8), only if players want them. It needs a contract ADR that makes the RTC epoch a recorded determinism input.
- **ARM64 targets** need the UBSan float-cast-overflow findings (ADR 0008 §5) to be clean first.
