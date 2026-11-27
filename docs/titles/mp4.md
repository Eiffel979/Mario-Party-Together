# Mario Party 4 (`mp4`)

**Owner:** runtime-gc-wii
**Strategy:** decomp port, game source = Party Board (ADR 0009), on the shared GameCube SDK layer (ADR 0007)
**Status:** P1-01 feasibility note, 2026-10-08. **Signed off by architect on 2026-10-08 (gate G1).** Its conditions are now decisions in ADRs 0004, 0007, 0008 and 0009 (Phase 1 plan §10, A1–A6).
**Recommendation:** **Go with conditions** (see §13).

This note was written by reading the community decomp and its PC-port fork in place, and through read-only GitHub queries. Nothing from either repository, and nothing from a disc, is copied into this repo. Identifiers appear below only where a decision depends on them.

---

## 1. Sources and pinning

| Repository | Role | Latest commit seen (2026-10-08) | License |
|---|---|---|---|
| `https://github.com/mariopartyrd/marioparty4` | Matching decomp (the reference) | `main` = `147b165a83187ac9e6cfdc3bf52f2e73437b1ffd` (2026-06-04, "Backport mp5 macros") | **None on `main`.** A CC0 1.0 license is proposed in open PR #663 (branch `license-change`, `db93f095a90dcc2e5fc27e1a66e313e752a3a06d`, 2026-09-24), not merged. |
| `https://github.com/mariopartyrd/partyboard` | Upstream's own PC port ("Party Board"). Same organisation, game sources forked from the decomp. | `main` = `98ac07fd1e8f7ab938bbb6fe75789fad99419010` (2026-09-29) | **CC0 1.0** (`LICENSE` at the root) |
| `https://github.com/AxioDL/musyx` | MusyX sound-library decomp (submodule `extern/musyx` of both repos) | **We pin `86851a3a8d51788d166ea42d137b49526f570400`** (upstream `main`, tree `7177c167f5a5b1e5f63ab927e44d412982f96588`, ADR 0009 amendment after P1-13b). Party Board's gitlink stays `2a0e01aeff0d06a9ee9e12966c435cb75144af9e`; the lock records the override. | CC0 1.0 at our pin (checked in its `LICENSE`). `2a0e01a` is MIT; CC0 starts at `8cb588d`. |
| `https://github.com/encounter/aurora` | GameCube/Wii source-level compatibility layer used by Party Board. **Adopted after S1: its GX front end, Dawn renderer, MTX and headers, without `aurora_core`** (ADR 0007 §6, ADR 0009 §1). | Party Board pins `514339438178ef2bed1b14e5149d90ece0c6e0cc`. Upstream HEAD on 2026-10-08 is `50e5be30ec12cc72768bc564bc016b55e82be868`, 132 commits later. | MIT |
| `https://github.com/higan-emu/libco` | Coroutines used by Party Board for game processes | Party Board pins `e18e09d634d612a01781168ad4d76be10a7e3bad` | ISC. **Not used:** P1-15 provides libco's API on the SDK's own context switch (§18) |

The decomp also has branches `port` (now a stub whose README points to the Party Board repo), `pal` and `mod` (2024, unrelated).

**The local copy the user supplied** is a zip of `marioparty4` with no `.git`. I compared the git blob hashes of seven files against `main` at `147b165`, including all three files touched by that commit, and every one matched. So the copy is consistent with `main` at `147b165`, but a zip cannot prove a commit.

**How we pin** (ADR 0009 §1):
- `runtimes/gc_wii/mp4/decomp.lock` records, for each source: URL, full commit hash, license and SPDX ID, plus the submodule commits we use (musyx, and aurora/libco if adopted).
- The build recipe gets sources with `git clone --filter=blob:none <url>`, then `git checkout <sha>`, into a gitignored cache directory. It refuses to build if `git rev-parse HEAD` differs from the lock.
- A user-supplied zip is accepted only if its computed git tree hash equals the tree of the locked commit. Otherwise the recipe asks for a git checkout.
- To bump a pin, a PR changes `decomp.lock` and `docs/titles/mp4.md` together and must pass the dump lane.

**Decided (ADR 0009, Phase 1 plan A1):** the game source is **Party Board** at `98ac07fd1e8f7ab938bbb6fe75789fad99419010`, with musyx at `86851a3a8d51788d166ea42d137b49526f570400` (bumped from Party Board's `2a0e01a` by the ADR 0009 amendment after P1-13b), and aurora at `514339438178ef2bed1b14e5149d90ece0c6e0cc` and libco at `e18e09d634d612a01781168ad4d76be10a7e3bad` only if adopted (S1, P1-13). `marioparty4` is a read-only reference. Pinning it after PR #663 merges is the documented fallback.

## 2. License status

- `marioparty4` `main`: **no license file anywhere** (verified in the local copy and on GitHub, where `license: null`). By default that means all rights are reserved by the contributors. Reading it is fine. Building and distributing anything derived from it is not covered until a license lands.
- Upstream intends to use CC0: a maintainer opened PR #663 on 2026-09-24. It has no review comments yet.
- `partyboard` is CC0 1.0 today. It contains the same game sources (adapted for PC), so it is the only MP4 game-source tree with a clear license right now.
- musyx is CC0 at our pin `86851a3` (MIT before `8cb588d`, including Party Board's `2a0e01a`). aurora is MIT. All are compatible with this project.
- CC0 only covers the contributors' own rights. It does not touch Nintendo's rights in the game itself. That is why ADR 0001/0009 still apply: no game data in the repo, and the build depends on the user's dump.

## 3. Matching revision and the user's dump

| Game ID | Revision | Decomp status |
|---|---|---|
| `GMPE01_00` | USA Rev 0 | **Fully matching** (default `configure.py` target) |
| `GMPE01_01` | USA Rev 1 | Fully matching |
| `GMPP01_00/01/02` | PAL Rev 0/1/2 | Not complete per the README. 238 objects are tagged as matching for USA and PAL. PR #662 ("PAL is 100%") was closed without merging. |
| `GMPJ01_00` | JP Rev 0 | Not complete |

`configure.py` tags every object as `Matching` (245) or `MatchingFor(USA, PAL)` (238). No object is tagged non-matching. The decomp builds `main.dol` plus 99 RELs (`config/GMPE01_00/build.sha1`). Party Board supports `GMPE01_00` and `GMPE01_01`, with partial PAL.

**User's dump:** a full-size ISO whose disc header reads game ID `GMPE01`, disc 0, version `0x00`, which is **USA Rev 0**. That is exactly the decomp's primary matching target. **Decided (Phase 1 plan D3):** Phase 1 supports `GMPE01_00` only. Its SHA-256 is computed locally by P1-11, never copied from here.

Frame rate: NTSC, one game-loop iteration per VI field (every `HuSysVWaitSet` call in the decomp passes 1). So `frameRate()` = 60000/1001 Hz (ADR 0003).

## 4. Code and data completeness, build inputs, toolchain

**Size:** 510 `.c` + 5 C++ files, about 400k lines. Of these, roughly 256 files are in RELs (minigames, boards, modes), 80 in game core (`src/game`, including `board/`), 88 in the Dolphin SDK, 48 in MSL C, 22 in the TRK debugger, and 6 in `msm` (the game's sound manager over MusyX). About 221 of the 256 REL files still use placeholder names (`fn_1_*`, `lbl_1_*`). The code is complete but mostly undocumented, as the README says.

**Assembly left over:** only the TRK debugger (two `.s` files, not needed on PC) and inline asm in SDK libraries, the runtime, `game/jmp.c` (setjmp/longjmp for game processes), `game/kerent.c` (a 1,019-entry REL→DOL jump table) and `game/malloc.c` (`mflr` to record the caller in heap headers). The PC build drops or replaces all of them. Party Board already does.

**Data still taken from the user's DOL/RELs.** The matching build runs decomp-toolkit's `extract` step on the user's `main.dol` and `bootDll.rel` and `#include`s the results (`.inc`). The data is the debug font, toon/reflection/highlight maps used by the 3D renderer, the disc-error screens and the boot logo (about 20 blobs in `config.yml`). **So the matching build cannot compile without a dump**, which confirms the ADR 0009 assumption. Party Board removes the build-time dependency: under its PC flag it reads these blobs **at runtime** from the user's disc image, using a per-revision address table (`src/port/dolassets.cpp`). The game module can then be compiled with no dump and no Nintendo bytes in it. **Decided (ADR 0009 §3):** we do the same. The runtime reads the blobs from the extracted DOL/RELs in `build_dir`.

**Matching toolchain (not needed by us):** Python 3, ninja, decomp-toolkit v1.6.1, Metrowerks CodeWarrior GC compilers 1.2.5/1.2.5n/1.3/1.3.2 with the 2.6 linker (fetched by the tooling as a bundle tagged `20250520`), binutils 2.42-1, `wibo` or wine on Linux, sjiswrap and objdiff. These are proprietary and Windows-only. Our native build uses none of them.

**Native toolchain** (Party Board's evidence): CMake 3.25+, C11/C++20, MSVC, clang-cl, GCC or Clang. Party Board's Windows default is MSVC at `/Od`, and it also has `windows-clang-*` and `linux-clang-*` presets, so the sources compile under Clang. Our ADR 0008 choice (Clang everywhere, `-ffp-contract=off`) is consistent with that. Its PC macros are `TARGET_PC`, `NON_MATCHING`, `BYTESWAPPING` and `OPTIMIZED_TEXTURE_LOADING`. Decomp `main` already contains backported `NON_MATCHING` fixes (PR #654) but no `TARGET_PC` paths.

**64-bit and endianness:** no `long` in game headers. 49 bitfields in game-state headers, some of which are persisted to the memory card. The HSF model format has 68 pointer fields that are fixed up in place on load, so they must be converted to host structs (Party Board's `byteswap.cpp`, about 1,100 lines). About 107 pointer-to-integer casts need review. One debug REL (`ztardll`) reads the bus clock from fixed low memory (`0x800000F8`). It must be patched or the address mapped.

## 5. REL/DLL loading model and how it maps onto a native build

**Console:** `main.dol` plus 99 RELs (`dll/*.rel`). Five of them (`m300`, `m302`, `m303`, `m330`, `m333`) are empty placeholders built from one stub. The overlay manager (`game/objdll.c`) reads a REL into the main heap, allocates its `.bss`, `OSLink`s it, and calls `_prolog`, which runs constructors and then `ObjectSetup`. On an overlay change it calls `_epilog`, then `OSUnlink`, then frees. `.bss` is zeroed on every load, and `.data` is fresh each time because the REL is re-read from disc. RELs reach the DOL through the `kerent` jump table. Boards (`w01`–`w06`, `w10`, `w20`, `w21`) share a board executor, and minigames share `REL/executor.c`.

**Party Board's model:** the DOL is a shared library (`dol`, exports from `dol.def`), and each REL is its own shared library exporting only `ObjectSetup`. They are loaded and unloaded with the OS loader, so `.data` resets naturally.

**Decided (ADR 0009 §3), built in P1-15:** **one game module, with all RELs statically linked in and isolated**.
- Compile each REL as its own unit, link it partially (`ld.lld -r` / `lld-link /lib`), and localise every symbol except a renamed entry point (`mp4_ovl_<name>_setup`). REL-local names (`fn_1_*`, `lbl_1_*`) collide across RELs, so isolation is required.
- Put each REL's `.data` and `.bss` in named sections. A generated table maps overlay ID to {entry, prolog/epilog, data range, pristine `.data` copy, bss range}.
- Replace `omDLLLink`/`omDLLUnlink` with a loader shim: on "load", restore pristine `.data`, zero `.bss`, run ctors, call the entry. On "unload", run the epilog. Nothing is read into the game heap.
- Why not Party Board's per-REL DLLs: about 95 OS-loaded modules each get a different base address per load (ASLR). Save states (ADR 0008 §10) store pointers to module code and data in the arena, so they would only survive in-process. With one module there is one base to pin.
- Fallback: Party Board's per-REL shared libraries if symbol localisation proves fragile on COFF. That costs E5 (see §11).
- Side effect: REL images no longer use the main heap, so arena addresses differ from the console. That is harmless for determinism but must be the same on every platform. It is, because the loader never allocates from the arena.

## 6. Threading model

| Mechanism | Where | What it does | Port plan |
|---|---|---|---|
| **HuPrc game processes** | `game/process.c` | Nearly all game logic. Cooperative "processes", each with its own stack from the game heap, switched by asm setjmp/longjmp once per frame in `HuPrcCall`. `HuPrcVSleep`/`HuPrcSleep` yield. | Coroutines with deterministic ordering (Party Board uses libco). **Stacks must be allocated from the arena** (libco `co_derive`, or minicoro with a custom allocator) so that save states capture them. |
| Main thread loop | `game/main.c` | `while (1)`: pad, `HuPrcCall`, minigame sequencer, 3D exec, `HuSysDoneRender` (waits on `VIWaitForRetrace` until `minimumVcount` fields have passed). | Run `main()` as the root coroutine and yield at the retrace wait. **One `stepFrame` = one VI field = one loop iteration.** |
| OS thread "Toe" | `game/sreset.c` | Disc-error display thread (priority 8): message queue, sleep/wakeup, plus a VI pre-retrace callback. Idle unless DVD errors occur. | Cooperative OSThread (ADR 0008 §1). Our DVD never errors, so it stays asleep. |
| THP decode | `game/thpmain.c` | Movie decode runs as the OS **idle function** (`OSSetIdleFunction`) and is cancelled on exit. | Decode synchronously inside the frame step. |
| VI post-retrace callback | `game/pad.c` (`PadReadVSync`) | Pad sampling happens in the VI interrupt. | Fire at a fixed point in `stepFrame`, reading that frame's `InputFrame`. |
| AI DMA callback | `msm/msmsys.c`, `game/THPSimple.c` | Drives the MusyX synth (`msmSysServer`) and THP audio mixing. | Virtual interrupt at deterministic virtual times (§7). |
| DVD / ARQ / CARD callbacks | `game/dvd.c`, `game/data.c`, `game/armem.c`, `game/card.c`, `msm/msmstream.c` | Async I/O completion. | Complete synchronously (§7). |

Interrupt masking (`OSDisableInterrupts`/`OSRestoreInterrupts`) appears in `pad.c`, `sreset.c`, `THPSimple.c`, `msm` and the debugger glue. Under cooperative scheduling these are no-ops.

## 7. Timing, clocks and RNG that feed game logic

**RNGs (hash manifest):** `rnd_seed`/`rand8` (LCG, `game/main.c`); `frand_seed` (`game/frand.c`, **re-seeded from `OSGetTime()` whenever it is 0**); `boardRandSeed` (`game/board/main.c`, **seeded from `OSGetTime()`**); and a REL-local LCG in `m420dll` (`rand.c`). No MSL `rand()` calls in game code. With virtual time (ADR 0008 §2), all of these become functions of the session seed. The virtual clock's epoch should come from `RuntimeConfig::seed`.

**Wall-clock and tick reads:**
- `OSGetTime`: the RNG seeds above, plus save-file timestamps (`game/saveload.c`, shown in `modeseldll/filesel.c` through `OSTicksToCalendarTime`). Because the timestamps go into save media, the virtual RTC epoch is part of the save-media content (ADR 0004). Phase 1 uses synthetic dates. Real dates would have to become a determinism input (ADR 0004 rule 8; Phase 1 plan §12).
- `OSGetTick` **busy-wait loops with no yield**: `game/audio.c` (`SNDGRP_WAIT`: spins until music and SFX voices stop, with a 500 ms timeout) and `ztardll`. On the console the audio interrupt runs during the spin. In a single-threaded port nothing advances unless the SDK makes it. **Rule needed:** each `OSGetTick`/`OSGetTime` call advances virtual time by a fixed quantum, and due virtual interrupts (AI DMA/MusyX tick, alarms) run when virtual time passes their deadline, only at SDK call boundaries. That stays deterministic because the number of calls is deterministic.
- `OSGetTick` loops that do yield (`bootDll` logo waits of 3000 ms and 1000 ms with `HuPrcVSleep`) work with a frame-driven clock.
- Debug-only timing: `safDll`, `modeltestDll`, and `OSStopwatch` in the perf code.

**Blocking waits on I/O completion** (no yield): `game/dvd.c` spins on a DVD callback flag. `game/data.c` spins on `DVDGetCommandBlockStatus` and on ARAM DMA (`HuARDMACheck`). So **DVD and ARAM DMA must complete synchronously inside the call** (callbacks invoked before return or at the next SDK boundary). A fixed multi-frame latency would hang these loops. ADR 0007 §4 and ADR 0008 §3 now require synchronous completion for DVD, ARAM and CARD.

**Hardware settings read by logic:** `OSGetSoundMode`, `OSGetProgressiveMode`/`VIGetDTVStatus` (boot asks about progressive scan), `OSGetLanguage` (PAL only), `OSGetResetCode`, `OSGetConsoleType`, physical memory size. All must return fixed values: stereo, progressive off, so no prompt appears; a retail console type; 24 MB.

**Audio state is game-visible.** Board and story code polls the sound engine: stream status (`HuAudSStreamStatGet` in every board and in story), music status, SFX status, and a **MIDI controller value** (`HuAudSeqMidiCtrlGet` in Boo's Haunted Bash). So the MusyX sequencer and voice state are game logic:
- They must advance inside `stepFrame` on virtual AI-DMA ticks (32 kHz output, a deterministic fractional-sample accumulator).
- They go in the hash manifest.
- A "null audio" mode may drop the PCM, but it **must still run the synth/sequencer**, or E6 breaks.

AxioDL musyx has a PC backend with a pull API (`sndPCRender(out, frames)`) and a null host backend, which fits exactly. Its SDL backend renders on its own SDL thread and **must not be used**. **Correction (P1-13b, §15):** that renderer exists only from `9fc772d` on, not at Party Board's `2a0e01a`. Our pin moved to `86851a3`, which has it (§17).

## 8. GPU readbacks and GX state read back by the CPU

| Call | Game-code users | Feeds logic? | Handling |
|---|---|---|---|
| `GXPeekZ`, `GXPeekARGB`, `GXPoke*`, `GXReadDrawSync`, `GXGetGPStatus` | none | — | — |
| `GXCopyTex` (EFB to texture) | `hsfman.c` (shadow maps), `wipe.c` (screen transitions), `board/boo.c`, and 20 RELs: m405, m410, m411, m416, m417, m419, m421, m423, m427, m428, m430, m434, m439, m440, m442, m448, m455, m456, m460, modeltest | No game-logic reads of the destination buffers. They are used as textures for later draws. m434 invalidates the cache when it allocates, then the GPU writes. **Correction from S1:** m415 (Stamp Out!) `memcpy`s the shadow-map copy destination into another texture's CPU buffer. That is presentation only. Party Board aliases the pointer under `TARGET_PC`, and we keep that branch. | GX layer keeps copies GPU-side as render targets keyed by destination address. The CPU-side bytes of the destination stay deterministic (zero) and outside the hash manifest. P1-14 should add a debug assert that traps CPU reads of those ranges. |
| `GXRead{GP,VCache,Pix,Mem}Metric` | `game/main.c` every frame, `game/perf.c` | Stored in globals for the perf overlay only | Return 0. |
| `GXSetDrawSync` + `GXSetDrawSyncCallback` | `game/perf.c` | Perf timing only | Fire the callback synchronously. |
| `GXDrawDone`, `GXSetDrawDone`/`GXWaitDrawDone` | `init.c`, `hsfman.c` | Synchronisation only | No-op / immediate. |
| `GXProject`, `GXGetViewportv`, `GXGetProjectionv` | game core, m427, m428 | **Yes** (CPU math on shadowed GX state, used for 2D placement) | Pure C on CPU-side state, built with the strict-FP flags. |
| `GXBeginDisplayList`/`GXCallDisplayList` | HSF draw/anim and about 10 RELs build display lists at runtime | No | The GX layer must parse its own recorded lists. |

Conclusion: **no GPU-to-CPU readback feeds game logic.** ADR 0007 §5 holds for MP4. Under a null renderer the game behaves the same as long as the GX layer keeps all CPU-visible state on the CPU.

## 9. Floating point and paired singles

- Game logic is float-heavy: `f32` positions and velocities everywhere, plus **about 3,300 double-precision trig calls** through `sind`/`cosd`/`atan2d` macros (`include/ext_math.h`, which wrap `sin`/`cos`/`atan2`). There are also about 320 `sqrtf`, 240 `fabs`, 140 `fmod`/`fmodf` and a few `atan2f`/`acosf`/`asinf`. ADR 0008 §5 applies directly: link the MSL math from the pinned Party Board source (ADR 0009 §2; `s_sin.c`, `k_cos.c`, `e_atan2.c`, `e_pow.c`, ...) built with our flags, **never** the host libm. `sqrt` and `fmod` are exactly specified by IEEE, so host versions are safe. Trig is not.
- **No paired-single asm in game or REL code.** Paired singles appear only in SDK libraries (`mtx`, `gx`, `os` fast-cast, `thp`), which we replace. The game calls `PSMTX*`/`PSVEC*`/`PSQUAT*` directly about 150 times, and the `MTX*`/`VEC*` macros (about 2,900 uses) also resolve to the PS versions in release builds. Implement them all as plain C (`C_MTX*`) under strict FP.
- Console PS math uses fused multiply-add, so our results will **not** bit-match a GameCube or Dolphin. That is irrelevant to our determinism, but it means console and Dolphin input recordings can never drive our runtime.
- Fast float↔int casts (`OSf32tos16`, `OSs16tof32`, `OSu8tof32` and so on, about 125 uses) need C versions with the console's saturating semantics.
- Cross-platform risk: out-of-range float→int casts are UB in C and behave differently on x86-64 (INT_MIN) and ARM64 (saturating). This is not a Phase 1 issue (Windows and Linux are both x86-64) but it matters for any later ARM64 support. Add a UBSan `float-cast-overflow` run to the dump lane.
- `HuMemDirectMalloc` stores the caller's return address in heap block headers (`mflr` asm). On PC it must be a constant, otherwise arena bytes differ between runs. Party Board uses a fake value.

## 10. SDK surface the game calls (sizing P1-13/P1-14)

Distinct SDK functions called from game core, RELs, `libhu` and `msm` (excluding the `kerent` jump table):

| Library | # | Notes |
|---|---|---|
| GX | ~127 | TEV (incl. swap tables, konst colours, S10 colours), **indirect textures** (`GXSetIndTexMtx`, `GXSetTevIndWarp`, `GXSetTevIndTile`), Z textures, fog, lights (spot, distance attenuation, specular), CI textures + TLUTs, runtime display lists, `GXCopyTex`, scissor, viewport jitter, line width, perf metrics. Immediate-mode vertex formats including `GXPosition1x8/1x16` (indexed) and the non-standard `GXUnknownu16`. |
| OS | 52 | Arena/heap (`OSInitAlloc`, `OSCreateHeap`, `OSAllocFixed`), threads (create/resume/sleep/wakeup/yield/cancel, idle function), message queue, interrupts, time (`OSGetTime`/`OSGetTick`/calendar), `OSLink`/`OSUnlink`, reset, settings getters, stopwatch, `OSReport`/`OSPanic`. |
| THP | ~6 SDK + game's THPSimple | `THPInit`, `THPVideoDecode`, `THPAudioDecode`, YUV→RGB GX helpers. Movies: opening, character endings, credits, mode select. |
| CARD | 23 | Probe/mount/check/format/open/create/read/write/delete/status/icon/banner, plus `CARDGetSerialNo` (save files are tied to the card serial, so `SaveMedia` must return a stable serial). |
| MTX/VEC/QUAT (+PS/C variants) | ~70 | See §9. |
| VI | 13 | Retrace count/wait/callbacks, configure, next framebuffer, black, DTV status, TV format. |
| DVD | 12 | Open/fast open/read/async/prio/cancel/status, path→entrynum. |
| AR/ARQ | 12 | ARAM allocation and DMA (the game caches data dirs in ARAM through `armem.c`, and MusyX uses ARAM for samples). |
| PAD | 9 | Read, clamp, motor (rumble), reset, recalibrate, spec. |
| AI | 8 | DMA and stream control (MusyX and THP). |
| DC/LC/PPC/SI/DEMO | 12 | Cache ops, locked cache, `PPCHalt`/`PPCSync`, sampling rate, stats. All no-ops. |
| **MusyX** (`snd*`, called from `msm`) | 52 | **No AX at all.** MP4 uses MusyX with its own DSP microcode, so there is no AX mixer for MP4 (ADR 0007 §4 as amended). Use AxioDL musyx's PC synth backend (CC0) driven from `stepFrame` (§7). |

There is no GBA link, no microphone, no keyboard and no Wii hardware. Input is the standard pad only.

**Party Board's split, for comparison:** aurora supplies GX, GD, MTX, OS (arena, alloc, time, report), DVD (straight from the disc image), CARD, PAD, SI and VI. Party Board itself keeps the decomp's CARD/AR/DSP/THP sources plus its own OS, ARQ and audio shims. It **stubs audio entirely** (`msmSysInit` is commented out, issue #14 "No Audio"), and THP movies do not play (#16).

## 11. Memory, heaps and save states

- Game heaps (`game/malloc.c`): `HEAP_HEAP` 2.25 MB, `HEAP_SOUND` 1.25 MB, `HEAP_MODEL` 10.5 MB, `HEAP_DVD` 5.5 MB, plus `HEAP_SPACE`. They are carved from the OS arena after a 1 MB GX FIFO and the framebuffers. ARAM (16 MB) holds cached data dirs and MusyX samples. The ADR 0008 §7 single arena of main RAM plus ARAM fits.
- Heap block headers contain raw pointers and (on console) return addresses, so hashes must never cover raw arena bytes (ADR 0008 §8 already says this).
- **PC game heaps return zeroed blocks** (ADR 0008, "Stale heap bytes: game heaps return zeroed blocks on PC"; a deliberate divergence from the console, like the constant heap-header caller in §7). On the console a fresh block holds whatever its earlier occupants left. On a 64-bit host that is often half of an 8-byte pointer, so it depends on the module layout and differs between Windows and Linux, in the hash and in any field read before it is written. Every allocator below clears the whole block it returns, to the block's end, not just the request: on allocation, never on free, unconditionally in the PC build. The arena itself is zero at boot (ADR 0008 §7).
- **Every allocator that hands out game-heap or emulated-ARAM memory, and what zeroes it:**

  | Allocator | Where it allocates | Zeroing |
  |---|---|---|
  | SDK `OSAllocFromHeap` (`OSAlloc`) | the OS arena's heaps: the HuMem heaps at boot (`HuMemInitAll`), the GX FIFO and the SDK's own blocks | `sdk/src/heap.cpp`, to the block's end |
  | SDK `ARAlloc` | emulated ARAM, a stack from `AR_RESERVED_BYTES` | `sdk/src/ar.cpp` |
  | `HuMemMemoryAlloc2` (`HuMemMemoryAlloc`, `…AllocNum`, so `HuMemDirectMalloc`, `HuMemDirectMallocNum` and `HuPrcMemAlloc`) | each HuMem heap, and each process's own heap (`process.c` runs `HuMemHeapInit` on a HuMem block for the process, its stack and its `HuPrcMemAlloc` memory) | Party Board patch 0026 |
  | `msmMemAlloc` (and patch 0001's `msmSndMalloc`, MusyX's `salMalloc` hook, which only forwards to it) | one `HEAP_MUSIC` block taken by `audio.c`, handed to `msmMemInit`; first fit inside it | patch 0027, up to the next block header, clamped to msm's heap end |
  | `HuARMalloc` (`armem.c`) | emulated ARAM from `HU_AMEM_BASE` (`0x808000`, the console's base; patch 0030) to `ARGetSize()`, first fit over `ARInfo` | patch 0028, through the SDK's PC-only `mpt_gc_ARClear` |

  `OSAllocFixed` is not zeroed: MP4 calls it only at boot (`init.c`), on an arena that is still zero, and it claims a fixed range rather than handing out a fresh block. A game pool that reuses its own fixed slots without an allocation call is not an allocator here (ADR 0008, same ruling, point 6).
- **E5 in process (M4-16, 2026-11-18, Windows).** First run, with the module unloaded between the runtimes as it then was: it failed on the module reload, as ADR 0008 read. `mpt_dump_lane --supervise --boot mp4 --e5-round-trip 5000 --frames 10000 --every 100` (`tests/dump_lane/README.md`): one runtime boots, runs to frame 10,000 with no input and saves its state before frame 5,000 (148,974,061 bytes); the lane destroys it, checked (then) that `mp4_game.dll` was no longer loaded, boots a fresh runtime with the same seed (the module loads again, at its fixed base), loads the state and runs frames 5,000 to 10,000 against the first run's 50 checkpoints. Each line is a separate process.

  | Seed | Module between the runtimes | Guard words in the arena at the save | Cookie after the reload | Result |
  |---|---|---|---|---|
  | 1 | unloaded and loaded again (E5 as narrowed) | 50 | changed | **fail-fast 0xC0000409** before the first checkpoint after the load (frame 5,100) |
  | 2 | unloaded and loaded again | 50 | changed | **fail-fast 0xC0000409** after the checkpoint at frame 5,300 |
  | 1 | kept loaded (a diagnosis option then) | 50 | same | pass: 50 of 50 checkpoints, frame 10,000 `2523e333cd8cc6cc` |
  | 2 | kept loaded (a diagnosis option then) | 50 | same | pass: 50 of 50 checkpoints, frame 10,000 `5677a93bacfda417` |

  - **Cause.** Both failures are a `/GS` stack cookie check, reported by the supervisor in the same place: `__report_gsfailure` called from `mpt_rel_bootDll__BootTitleExec`, under `mpt_rel_bootDll__BootExec` and the coroutine trampoline. At frame 5,000 the title screen's process is suspended inside `BootTitleExec`, a guarded function, so its frame in the arena holds the cookie of the module load that saved it, mixed with the frame's address. The lane counts 50 words in the arena at that save that are the module's cookie XOR an address within 64 KiB of the word (`tests/dump_lane/stack_guard.h`), i.e. guard words of suspended frames. The module's `__security_cookie` (read through its load config directory) is made anew on every load: 5091d29981cd before the reload and 3249d2d3d913 after it on seed 1. The load restores the arena, so the old guard word is back in the frame, and the check against the new cookie fails when `BootTitleExec` returns. On seed 2 that return came later, after frame 5,300. Nothing overran: with the module kept mapped (same cookie) both seeds pass with the M1 trail. So the save state carries everything else, and the module reload is the one thing that breaks the narrowed E5. The existing `--save-load-diff` mode, which also destroys its first runtime, fails the same way at the same place.
  - **Synthetic test, no dump.** `--guard-reload-selftest` (`tests/mp4_loader/guard_stub.cpp`, CTest `dump_lane_guard_reload_keep` and `dump_lane_guard_reload_fails_fast`, Windows x64) suspends a guarded function of a stub module linked at a fixed base on a fiber, finds its guard word with the same scan, and resumes it after keeping the stub loaded (it returns) or reloading it (new cookie, fail-fast in the stub's function).
  - **Not run on Linux** (M4-U2). There the stack protector's canary belongs to the process (ADR 0008, same section, point 6), so a module reload should not change it; that is unverified.
  - **Ruling (ADR 0008, "Game module kept loaded for the process", architect, 2026-11-18).** E5 as narrowed destroys the runtime, not the module: the module is a process resource, like the arena reservation and the fixed base. Once loaded it stays loaded until the process shuts down, and every create starts from the module's load image, so a pinned module cannot carry state from one runtime to the next outside the save state.
  - **What was built** (branch `m4-16-e5-inprocess`):
    - `host/src/module_loader.cpp`: `release_module` drops a hold and never unloads. A second `load_module` of the same path returns the same table without loading again. While a module is loaded, another path, another build ID (`Mp4Runtime` passes the one the recipe installed next to the module, `mp4_game_build_id.txt`) or a module file that changed on disk since the load is refused with "restart Mario Party Together to use the new build", never a fail-fast. `shutdown_module` unloads at exit when no hold is left, reports an error and leaves the module mapped when one is, and does nothing before any load; a load after it works.
    - `host/src/register.cpp`: MP4's `ShellTitle::shutdown` calls `shutdown_module` (it needs no SDL). The shell's `main` and the dump lane's call `shell::shutdown_titles` on every normal exit. A crash or fail-fast runs no teardown, as before.
    - `src/abi.cpp`: at load, before any create (`mpt_mp4_get_api`), the module takes its game-visible state: the game core's and every REL's data, MusyX's data and bss, and the GX layer's object ID counters. Every create starts from them (for the RELs' data only since every boot restores it, 2026-11-26; until then a REL was restored only when linked, "Every REL restored at boot" below); the GX front end's game-visible state is cleared and the SDK made anew at every boot, as before. Host-side state (aurora, Dawn, the CRT, the renderer) is not reset. **One known exception is game-visible:** aurora's `g_gxState`, whose projection and viewport m427 and m428 read back (`GXGetProjectionv`, `GXGetViewportv`). A fresh boot is unaffected, since `GXInit` resets it, but a `loadState` into a fresh runtime on the kept module keeps the previous runtime's `g_gxState` until the game sets those values again. It is the same open item as `g_gxState` in save states and the hash manifest (§20); the E5 and reboot runs below save at the title screen, which does not read it back.
    - **Rebuilds while the module is loaded** (`recipe/src/recipe.cpp`, `RecipeConfig::module_in_use`). The recipe refuses, in `check_toolchain` (before the cache lookup), a build of another build ID, and in its first step any build of the module at all, before a tool runs or a file is written. A cache hit of the loaded build still goes through, so a second party in the same process works. **For asset-import and frontend-ui:** Phase 1 has no flow that rebuilds MP4 in a process that has played it. A future one (a "rebuild" button, a dump re-import, a new build after an update) must restart the process or refuse, and should not count on the recipe's refusal alone: for a cache miss, asset-import's own stages (the disc extraction into the new build's temporary folder) run before the recipe's first step.
  - **Runs after the change** (Windows, null backend, each a separate process under `--supervise`; the M1 trails are the ones recorded in §20):

    | Run | Result |
    |---|---|
    | M1, `--boot mp4 --seed 1 --frames 10000 --every 100` | `2523e333cd8cc6cc` at frame 10,000, all 100 checkpoints equal to the trail before the change; `exit: game module unloaded` |
    | `--e5-round-trip 5000`, seed 1 | PASS, 50 of 50, `2523e333cd8cc6cc`; module handle and cookie the same across the two runtimes; 50 guard words at the save |
    | `--e5-round-trip 5000`, seed 2 | PASS, 50 of 50, `5677a93bacfda417`; same handle and cookie; 50 guard words |
    | `--save-load-diff 5000`, seed 1 | the resumed run matches at all 50 checkpoints |
    | `--reboot-trail`, seed 1: runtimes A (seed 1), B (seed 2), C (seed 1) in one process | A and C each equal the M1 seed-1 trail at all 100 checkpoints, B the seed-2 trail; same cookie for all three |
    | `--e5-reload-module` (diagnosis), seed 1 | unloads through MP4's shutdown, new cookie, fail-fast 0xC0000409 in `mpt_rel_bootDll__BootTitleExec`, as expected |
    | Shell, `--verify-replay` of a 3,000-frame autopilot replay with `--dump`, twice | exit 0 in 16 s each, `replay OK: 3000 frames, 50 checkpoints`, nothing on stderr (MP4's shutdown prints an error only when the module stays loaded) |

    Every lane run above that ended normally printed `dump lane: exit: game module unloaded`. Tests without a dump: `mp4_module_loader` (12 cases: the pin, the reuse without a reload, the refused path and build ID, every `shutdown_module` case), `mp4_recipe` (the refusal), `dump_lane_guard_reload_keep` and `dump_lane_guard_reload_fails_fast` (the record of why the module is kept loaded). **E5 as narrowed is met on Windows.** Linux waits on M4-U2.
- **ARAM: who owns which bytes, and what the hash covers** (ADR 0008, "Gate status after the implementation", finding 2; Party Board patch 0030).
  - **On the console the two users are disjoint by construction.** `audio.c` passes `HU_AMEM_BASE` (`0x808000`, `game/armem.h`) to `msmSysInit` as `aramEnd` with `skipARInit`. msm then takes no `ARAlloc` block; it uses ARAM from `ARGetBaseAddress()` for `sys.info->aramSize` bytes, and `msmSysInit` fails with `MSM_ERR_OUTOFAMEM` if that does not end at or below `aramEnd` (`msm/msmsys.c`). `HuARInit` (`armem.c`) hands out ARAM only from `HU_AMEM_BASE` to `ARGetSize()`.
  - **The port had lost that.** Party Board's `TARGET_PC` branch of `HuARInit` started `HuARMalloc` at `0x20`, so its first blocks covered the SDK's reserved base [0, `AR_RESERVED_BYTES` = 0x4000), which the state hash already reads as the `ARAlloc` range, and the console's msm region above it. *Correction (2026-11-09, after the ruling):* an earlier version of this bullet said MusyX DMAs its zero buffer and stream buffers into msm's region on PC. That is the Dolphin path of `hw_aramdma.c` (`MUSY_TARGET_DOLPHIN`). Our MusyX build is `MUSY_TARGET_PC` (`musyx/CMakeLists.txt`), whose `aramInit` touches no ARAM, whose zero buffer is address 0, and whose stream buffers and samples are `salMalloc` blocks in msm's heap (`hw_aramdma.c`, the `MUSY_TARGET_PC` block). So on PC the live overlap was `HuARMalloc` with the SDK's reserved base (bytes hashed twice, and handed to two owners), not with MusyX.
  - **Fix.** Patch 0030 makes `MPT_PC` take the console branch, so `HuARMalloc` starts at `HU_AMEM_BASE` again. The 7.97 MiB from there to ARAM's end are what the console gives it.
  - **The check that fails the run.** After every step the module checks that every in-use `HuARMalloc` block lies in [`HU_AMEM_BASE`, ARAM's end), that the blocks do not overlap, and that the SDK's `ARAlloc` range ends at or below `HU_AMEM_BASE`. If not, the step faults with "ARAM alias check: …". `mp4_armem` tests the base and the zeroing on a synthetic ARAM.
  - **What is hashed.** Every ARAM byte an allocator handed out and has not freed, each once, with the canonical walk: the SDK's `ARAlloc` range from 0 to its top (on MP4 only the 16 KiB reserved base, since msm skips `ARAlloc`), then `HuARMalloc`'s in-use blocks from `ARInfo`, in address order, each by its ARAM address (the `armem` item; patch 0030's `mpt_mp4_armem_in_use` lists them). Free ARAM is not hashed. The deep hash covers the same blocks raw; the perturbation targets `MPT_MP4_PERTURB_ARMEM` (must count) and `MPT_MP4_PERTURB_FREE_ARAM` (must not) and the ARAM samples cover them. Before this, `state_hash` covered ARAM only up to the `ARAlloc` top, which left every `HuARMalloc` block out.
  - **Not hashed: msm's region**, [`ARGetBaseAddress()`, + `aramSize`) (ADR 0008, "Gate status after the writer fixes", point 3). It is excluded from `state_hash` (not from save states: it lies in the arena and is saved with it). The reason: on PC nothing hands out or writes the region (MusyX is built as `MUSY_TARGET_PC`, see the correction above), so it stays at boot zeros. The exclusion holds only while there is no DMA into or out of the region in either direction. **Read list, every path in the pinned sources that reads ARAM:**
    - `game/armem.c`: the three ARAM-to-main-RAM requests, `HuAR_ARAMtoMRAMNum` (line 298, from `aMemP`) and `HuAR_ARAMtoMRAMFileRead` (lines 371 and 396, from `srcAMemP`). Their sources lie inside `HuARMalloc` blocks (`HuAR_ARAMtoMRAMFileRead` reads at an offset into the block `HuARDirCheck` finds in `ARInfo`; `HuAR_ARAMtoMRAMNum` takes an address its callers got from `HuAR_DVDtoARAM` or `HuAR_MRAMtoARAM`), which patch 0030 and the alias check keep at or above `HU_AMEM_BASE`, so none reads msm's region. (Lines 225 and 261 are main RAM to ARAM.)
    - `msm/msmsys.c` (lines 820–838): only `ARInit`, `ARQInit`, `ARAlloc` (not taken, `skipARInit`) and `ARGetBaseAddress` for the size check; no DMA.
    - MusyX (`86851a3`): every `ARQPostRequest`, `ARGetBaseAddress` and stream-buffer DMA is in the `MUSY_TARGET_DOLPHIN` block of `runtime/hw_aramdma.c`, which our build does not compile; the PC block uses host memory only.
    - The SDK (`runtimes/gc_wii/sdk`): `ARStartDMA` and the ARQ copy only what a caller requests (`sdk/src/ar.cpp`), and nothing in the SDK or our MusyX engine reads ARAM on its own.
    None of them reads or writes msm's region. **The run check (M4-03, in place before the seed-2 gate run):** the module passes msm's region, [`ARGetBaseAddress()` = `AR_RESERVED_BYTES`, `HU_AMEM_BASE`) = [0x4000, 0x808000), to the SDK as an ARAM guard (`SdkConfig::aram_guards`, `mp4/src/aram_checks.h`). Any `ARStartDMA` or `ARQPostRequest`, main RAM to ARAM or ARAM to main RAM, whose ARAM range overlaps it faults the step with "ARAM msm region: …" before a byte moves (`sdk/src/ar.cpp`). The SDK's `gc_sdk_unit` tests fault it in each direction, by DMA and by ARQ, pass transfers that end at the range or start at its end, and check `create()`'s validation; with the check disabled four of them fail. `mp4_aram_checks` checks the range the module passes. If the check ever fires in real play, the region is game state and goes back to architect. No run in M4-03 has fired it.
  - **The alias check's test (qa's #77 follow-up).** The check is now a pure function (`aramAliasCheck`, `mp4/src/aram_checks.cpp`) that `step()` calls after every step and turns into `MPT_MP4_STEP_FAULTED` on any message. `mp4_aram_checks` (standalone, so public CI runs it) feeds it synthetic block lists: valid lists pass, and a block below `HU_AMEM_BASE` or straddling it, overlapping or out-of-order blocks, a block past ARAM's end, an `ARAlloc` top above the floor and a broken block count each give "ARAM alias check: …".
- **msm's PC block header (fixed in M4-15, Party Board patch 0035).** On a 64-bit host `MSMBLOCK` is 48 bytes, but `msmMemAlloc` sized and placed blocks for the console's 32-byte header, so each block used 16 bytes more than msm reserved and a heap filled close to its end would have run past it into the next HuMem block (ADR 0008, "Gate status after the implementation", finding 1). Patch 0035 pads `MSMBLOCK` to 64 bytes on PC, so the user area keeps the console's 32-byte alignment; a static assertion pins `sizeof(MSMBLOCK)` to 64 on every 64-bit host (it holds on Windows and in the Linux CI job); and `msmMemAlloc` sizes, places and zeroes blocks with `sizeof(MSMBLOCK)`. A request larger than the heap, or a block whose end would pass the heap's end, returns `NULL`. `mp4_msm_heap` (synthetic, guard bytes on both sides of a 64 KiB heap) fills the heap to exactly full (256 blocks of 192 bytes), checks that the next allocation fails and that no guard byte changed, then fills it again with mixed sizes; against `msmmem.c` without the patch it fails 9 checks (264 blocks fit, user areas misaligned, blocks outside the heap, guard bytes written). **Peak use over the full party** (a local-only build that reported each new high-water mark of msm's heap, never committed; Windows, default board, the gate's autopilot): seed 1 reaches 17,295,072 of the heap's 20,706,304 bytes (83.5 %), seed 2 18,029,600 (87.1 %). On neither did `msmMemAlloc`'s search for a free block come back empty. That is the only `NULL` the build reported: it did not count the two returns the patch adds (a request larger than the heap, a block whose end would pass the heap's end), so the runs say nothing about those. The patch moves state hashes (§20, "Results with the M4-15 fix"). **Party Board patch 0037** (qa's #82 review) closes two 32-bit squeezes next to it: `msmSndMalloc`, MusyX's allocation hook, refuses a `size_t` request above `UINT32_MAX` instead of wrapping it, and `msmMemInit` aligns the heap's start and computes its size in `uintptr_t` instead of through `(s32)` casts of the pointer. Neither changes a value MP4 reaches; the M1 trails are unchanged (§20).
- **Save states into a fresh process (E5)** need: the arena reserved at a fixed virtual address; HuPrc coroutine stacks inside the arena; and the game module loaded at a fixed base (ADR 0008 §11. Windows: fixed image base, no dynamic base. Linux: no fixed-address `dlopen`, so either a non-PIE module mapped at its fixed address by our own loader, or relocation on load); and the stack guard value the state was saved under (below). Without a fixed base, every function pointer saved in the arena (process entry points, object callbacks, overlay hooks) is wrong in a new process. **Flagged as a risk to E5.**
- **Equal addresses are not enough: the stack guard value** (ADR 0008, "Stack guard values in save states"; `docs/phases/phase-1.md`, ruling M4-I). The module is built with `/GS`. A game function that is suspended at the save has the guard value in its frame, which lies in the arena and so in the state. On Windows that value comes from the module's `__security_cookie`, made anew every time the module is loaded; on Linux the stack protector's canary is made once per process. A state loaded under another value fails fast (0xC0000409 on Windows) when that function returns, although nothing overran. Seen with `--party-resume` (§21 "M4-03"). Fresh-process save states are a Phase 2 item, with the ADR that closes ADR 0008 §10's exception. Phase 1 has one check, M4-16: the in-process E5 round trip, saved at a frame with a suspended guarded function. It failed while the module reloaded between runtimes, and passes with the module kept loaded for the process (next bullet).

- **Every REL restored at boot (2026-11-26, `mp4-rel-reset-at-boot`).** `boot()` (`src/abi.cpp`) restored the game core's writable data but not the RELs'. A REL's data and bss were restored only when that REL was linked (`mpt_mp4_rel_link`, `src/rel_loader.c`). Every REL's range is hashed and saved whether it is linked or not, so a fresh runtime on the kept module booted with the previous runtime's REL state. That broke ADR 0008 point 3 for any runtime after the first.
  - **Found by** qa-reviewer, from the P1-17 party-events test (§21, "Full-party event test"): a replay verified in the process that had just recorded a whole party differed at frame 60.
  - **Why the old checks missed it.** `--reboot-trail` compared only from frame 100, and the RELs a title-screen run touches are linked again before then.
  - **Fix.** `mpt_mp4_rel_reset_all` restores every REL's pristine copy at every boot, next to `mpt_mp4_game_data_reset`. The `LoadImage` comment in `abi.cpp` now says so.
  - **Rest of the load image.** The GX object ID counters (`game.boot_ids`, set at every boot) and MusyX's data (`MusyxEngine::reset`) were already restored at boot. The loader keeps no other per-REL state: `mpt_mp4_rel_unlink` frees nothing, and objdll's tables are game core data.
  - **Stronger checks.**
    - `--reboot-trail` now compares from the boot-time hash (frame 0).
    - `--e5-round-trip` requires the fresh runtime to boot at the first runtime's hash before it loads the state.
    - The new `--reboot-dirty` boots a clean runtime, then runtimes that link many RELs, then a second clean runtime that must match the first from frame 0 (`tests/dump_lane/README.md`).
  - **Results** (Windows, Release, null backend, the user's dump, one lane process at a time):

    | Run | Before the fix (main's runtime) | With the fix |
    |---|---|---|
    | `--reboot-dirty --frames 600 --every 60`, seed 1 (12 overlays in between) | FAIL: C differs from A at frame 0 (`527954ba68dab883` against `723f6a9552168d2e`) | PASS, 11 of 11 checkpoints from frame 0 |
    | `--reboot-dirty --reboot-dirty-party 252000`, seed 1 (the whole party then four minigames, 30 overlays) | not run | PASS, 11 of 11 |
    | `--e5-round-trip 5000 --frames 10000 --every 100`, seed 1 | FAIL: the fresh runtime boots at `b8bcc2214f420fef`, not `723f6a9552168d2e` | PASS: boot-time hashes equal; 50 of 50; `2523e333cd8cc6cc`; same handle and cookie |
    | the same, seed 2 | not run | PASS: boot-time hashes equal (`0e8fd1f3ad9b3bc5`); 50 of 50; `5677a93bacfda417` |
    | `--reboot-trail --frames 10000 --every 100`, seed 1 | — | PASS, 101 checkpoints from frame 0; A and C end at `2523e333cd8cc6cc`, B at `5677a93bacfda417`; one cookie |
    | M1, fresh process, `--boot mp4 --frames 10000 --every 100`, seeds 1 and 2 | — | `2523e333cd8cc6cc` and `5677a93bacfda417`, unchanged: a fresh process's first boot already starts from pristine RELs |

  - **Build ID.** The fix changes the module's build ID (`mp4-ac8abdfd59c65439` to `mp4-3573ec54fa757b78`), but no fresh-process hash.

## 12. Catalog: content IDs

IDs follow `mp4.<kind>.<snake_name>`. Display names are the English USA names, shown as text only. The DLL and category come from the decomp's minigame table (`mgInfoTbl`, `game/objsub.c`, field `type`) and from REL labels in `configure.py`. Category names for `type` values are inferred from how board code launches them (type 4 from battle spaces, 3 from Bowser events, 5 from fortune events).

### Characters
`mp4.character.mario`, `mp4.character.luigi`, `mp4.character.peach`, `mp4.character.yoshi`, `mp4.character.wario`, `mp4.character.donkey_kong`, `mp4.character.daisy`, `mp4.character.waluigi` (8).

### Boards
| ID | DLL | Display name | Notes |
|---|---|---|---|
| `mp4.board.toads_midway_madness` | w01 | Toad's Midway Madness | Party board |
| `mp4.board.goombas_greedy_gala` | w02 | Goomba's Greedy Gala | Party board |
| `mp4.board.shy_guys_jungle_jam` | w03 | Shy Guy's Jungle Jam | Party board |
| `mp4.board.boos_haunted_bash` | w04 | Boo's Haunted Bash | Party board. Logic reads music MIDI controllers (§7). |
| `mp4.board.koopas_seaside_soiree` | w05 | Koopa's Seaside Soiree | Party board |
| `mp4.board.bowsers_gnarly_party` | w06 | Bowser's Gnarly Party | Party board (unlockable) |
| `mp4.board.mega_board_mayhem` | w20 | Mega Board Mayhem | Extra Room board mode, not a regular party board |
| `mp4.board.mini_board_mad_dash` | w21 | Mini Board Mad Dash | Extra Room board mode, not a regular party board |

w10 is the tutorial board and stays out of the catalog.

### Minigames (61)
| ID | DLL | Display name | Category (`type`) |
|---|---|---|---|
| `mp4.minigame.manta_rings` | m401 | Manta Rings | 4-player (0) |
| `mp4.minigame.slime_time` | m402 | Slime Time | 4-player (0) |
| `mp4.minigame.booksquirm` | m403 | Booksquirm | 4-player (0) |
| `mp4.minigame.trace_race` | m404 | Trace Race | Battle (4) |
| `mp4.minigame.mario_medley` | m405 | Mario Medley | 4-player (0) |
| `mp4.minigame.avalanche` | m406 | Avalanche! | 4-player (0) |
| `mp4.minigame.domination` | m407 | Domination | 4-player (0) |
| `mp4.minigame.paratrooper_plunge` | m408 | Paratrooper Plunge | 4-player (0) |
| `mp4.minigame.toads_quick_draw` | m409 | Toad's Quick Draw | 4-player (0) |
| `mp4.minigame.three_throw` | m410 | Three Throw | 4-player (0) |
| `mp4.minigame.photo_finish` | m411 | Photo Finish | 4-player (0) |
| `mp4.minigame.mr_blizzards_brigade` | m412 | Mr. Blizzard's Brigade | 4-player (0) |
| `mp4.minigame.bob_omb_breakers` | m413 | Bob-omb Breakers | 4-player (0) |
| `mp4.minigame.long_claw_of_the_law` | m414 | Long Claw of the Law | 4-player (0) |
| `mp4.minigame.stamp_out` | m415 | Stamp Out! | 4-player (0) |
| `mp4.minigame.candlelight_flight` | m416 | Candlelight Flight | 1-vs-3 (1) |
| `mp4.minigame.makin_waves` | m417 | Makin' Waves | 1-vs-3 (1) |
| `mp4.minigame.hide_and_go_boom` | m418 | Hide and Go BOOM! | 1-vs-3 (1) |
| `mp4.minigame.tree_stomp` | m419 | Tree Stomp | 1-vs-3 (1) |
| `mp4.minigame.fish_n_drips` | m420 | Fish n' Drips | 1-vs-3 (1) |
| `mp4.minigame.hop_or_pop` | m421 | Hop or Pop | 1-vs-3 (1) |
| `mp4.minigame.money_belts` | m422 | Money Belts | 1-vs-3 (1) |
| `mp4.minigame.goooooooal` | m423 | GOOOOOOOAL!! | 1-vs-3 (1) |
| `mp4.minigame.blame_it_on_the_crane` | m424 | Blame it on the Crane | 1-vs-3 (1) |
| `mp4.minigame.the_great_deflate` | m425 | The Great Deflate | 2-vs-2 (2) |
| `mp4.minigame.revers_a_bomb` | m426 | Revers-a-Bomb | 2-vs-2 (2) |
| `mp4.minigame.right_oar_left` | m427 | Right Oar Left? | 2-vs-2 (2) |
| `mp4.minigame.cliffhangers` | m428 | Cliffhangers | 2-vs-2 (2) |
| `mp4.minigame.team_treasure_trek` | m429 | Team Treasure Trek | 2-vs-2 (2) |
| `mp4.minigame.pair_a_sailing` | m430 | Pair-a-sailing | 2-vs-2 (2) |
| `mp4.minigame.order_up` | m431 | Order Up | 2-vs-2 (2) |
| `mp4.minigame.dungeon_duos` | m432 | Dungeon Duos | 2-vs-2 (2) |
| `mp4.minigame.beach_volley_folly` | m433 | Beach Volley Folly | Extra Room (7) |
| `mp4.minigame.cheep_cheep_sweep` | m434 | Cheep Cheep Sweep | 2-vs-2 (2) |
| `mp4.minigame.darts_of_doom` | m435 | Darts of Doom | Bowser (3) |
| `mp4.minigame.fruits_of_doom` | m436 | Fruits of Doom | Bowser (3) |
| `mp4.minigame.balloon_of_doom` | m437 | Balloon of Doom | Bowser (3) |
| `mp4.minigame.chain_chomp_fever` | m438 | Chain Chomp Fever | Battle (4) |
| `mp4.minigame.paths_of_peril` | m439 | Paths of Peril | Battle (4) |
| `mp4.minigame.bowsers_bigger_blast` | m440 | Bowser's Bigger Blast | Battle (4) |
| `mp4.minigame.butterfly_blitz` | m441 | Butterfly Blitz | Battle (4) |
| `mp4.minigame.barrel_baron` | m442 | Barrel Baron | Extra Room (7) |
| `mp4.minigame.mario_speedwagons` | m443 | Mario Speedwagons | 4-player (0) |
| `mp4.minigame.reversal_of_fortune` | m444 | Reversal of Fortune | Fortune (5) |
| `mp4.minigame.bowser_bop` | m445 | Bowser Bop | Item (6) |
| `mp4.minigame.mystic_match_em` | m446 | Mystic Match 'Em | Item (6) |
| `mp4.minigame.archaeologuess` | m447 | Archaeologuess | Item (6) |
| `mp4.minigame.goombas_chip_flip` | m448 | Goomba's Chip Flip | Item (6) |
| `mp4.minigame.kareening_koopa` | m449 | Kareening Koopa | Item (6) |
| `mp4.minigame.the_final_battle` | m450 | The Final Battle! | Story (8) |
| `mp4.minigame.jigsaw_jitters` | m451 | Jigsaw Jitters | Extra Room (7) |
| `mp4.minigame.challenge_booksquirm` | m453 | Challenge Booksquirm | Extra Room (7) |
| `mp4.minigame.rumble_fishing` | m455 | Rumble Fishing | Battle (4) |
| `mp4.minigame.take_a_breather` | m456 | Take a Breather | 4-player (0) |
| `mp4.minigame.bowser_wrestling` | m457 | Bowser Wrestling | Story (8) |
| `mp4.minigame.panels_of_doom` | m458 | Panels of Doom | Story (8) |
| `mp4.minigame.mushroom_medic` | m459 | Mushroom Medic | Extra Room (7) |
| `mp4.minigame.doors_of_doom` | m460 | Doors of Doom | Extra Room (7) |
| `mp4.minigame.bob_omb_x_ing` | m461 | Bob-omb X-ing | Extra Room (7) |
| `mp4.minigame.goomba_stomp` | m462 | Goomba Stomp | Extra Room (7) |
| `mp4.minigame.panel_panic` | m463 | Panel Panic | Extra Room (7) |

Bowser Wrestling (m457) and Panels of Doom (m458) keep the game's Story type (8), but Bowser's Gnarly Party also launches them in a regular party, on its Bowser space for a Mega-size player, so they are party-eligible (§18.1, P1-17).

Totals: 4-player 16, 1-vs-3 9, 2-vs-2 9, Battle 6, Bowser 3, Item 5, Fortune 1, Extra Room 9, Story 3 = 61. DLL numbering spans 63 slots (m401–m463). The slots m452 and m454 point back at m450 and have no row, so the table has 61 rows. Which categories can appear in a regular party (via `GWMGAvailSet` / `mgListAll` in `board/mg_setup.c`) is confirmed in P1-17. The `E2` coverage table (P1-08) needs at least the party-eligible rows.

**Other REL modes** (not catalog content): boot, instructions, mode select, minigame modes, story modes (`mstory*`), presents room, options, results, credits, character select, plus probable debug/demo modules (`E3setupDLL`, `modeltestDll`, `safDll`, `ztardll`; whether they can be reached in retail is still to be confirmed).

**Input notes for P1-08:** standard pad only. No microphone, motion or GBA. Rumble goes through `omVibrate` → `PADControlMotor` (an output, optional in Phase 1). Analog triggers and the C-stick are read in many modules. Classifying which minigames need analog triggers is P1-08 work.

## 13. Recommendation: **go with conditions**

> **Resolution (architect, 2026-10-08):** accepted. Condition 1 → option (a), Party Board (ADR 0009). Condition 2 → ADR 0007 §4 and §6. Condition 3 → ADR 0008 §1–3 and §7. Condition 4 → ADR 0008 §11 and the E5 narrowing rule in the Phase 1 plan. Condition 5 → D3, with audio and THP budgeted in P1-13.

MP4 is a good first target. The decomp is complete and matching for the user's exact revision (USA Rev 0). Upstream already has a working CC0 PC port, which proves the sources compile and run natively on Windows and Linux under Clang. No GPU readback feeds game logic. Game logic runs in a cooperative scheduler that fits ADR 0008 well.

**Conditions** (each one blocks P1-15, not P1-13/P1-14):
1. **License (Q2).** Do not build on `marioparty4` `main` while it has no license. Choose one:
   - **(a) recommended:** pin **Party Board** (CC0) as the game-source dependency, with `marioparty4` as the read-only reference.
   - **(b)** pin `marioparty4` at the first commit after PR #663 (CC0) merges.
   
   Either way we never vendor sources (ADR 0009).
2. **ADR 0007 amendments.** Audio is MusyX, not AX: use musyx's PC synth in pull mode, stepped in `stepFrame`, with a null output that still runs the sequencer. DVD and ARAM DMA complete synchronously. Spike S1 should treat **aurora as the likely winner**, because Party Board's game code is already written against aurora's Dolphin headers. We would take aurora's GX/GD/MTX only, with our own OS/VI/PAD/DVD/CARD/AR and audio for determinism (aurora's OS time reads the host clock).
3. **ADR 0008 amendments.** Virtual time advances by a fixed quantum on each time query and runs due virtual interrupts at SDK boundaries (needed for the sound-wait spin loops). The virtual RTC epoch is derived from the seed. HuPrc coroutine stacks live in the arena. Heap-header return addresses are constant.
4. **E5 feasibility.** Fresh-process save states need a fixed load base for the game module and the arena (§11). P1-16 must show this works on both OSes early (milestone M1). Otherwise E5 is narrowed to in-process.
5. **Scope.** Phase 1 targets `GMPE01_00` only. Audio and THP movies are new work in our layer, since Party Board has neither. Budget them in P1-13.

**Risks:**

| Risk | Likelihood / impact | Notes |
|---|---|---|
| Decomp license never lands, or Party Board changes license | Low / High | Party Board is CC0 today. Pinning a commit keeps that grant. |
| Party Board's game sources are entangled with aurora, its UI and its frame limiter under `TARGET_PC` | High / Med | Carry a small patch set or use our own target macro. Its port-only files (UI, achievements, settings) are not used. |
| Audio-state determinism (MusyX sequencer is game-visible) | Med / High | Hash the MusyX state. Add perturbation tests on board music. |
| Fresh-process save states (fixed bases) | Med / Med | §11. Fallback: in-process only for Phase 1 and a semantic serializer in Phase 2 (ADR 0008 §10b). |
| Per-REL symbol isolation in a single module | Med / Low | Fallback: Party Board's per-REL shared libraries. |
| GX coverage (indirect textures, Z textures, EFB copies) | Med / Med | Party Board's open graphical bugs in m402, m406 and m427 show where aurora is still incomplete. |
| THP movie decoding | Low / Low | Skippable cutscenes. A decoder is needed for completeness, not for E1. |
| Pace of upstream changes | Med / Low | Pin, then bump deliberately through the dump lane. |

**PLAN §3 snapshot row, re-verified:** MP4 | GameCube | decomp port (100% matching for USA; upstream CC0 PC port exists) | **Low–Med**. The decomp side is better than expected. Determinism, save-state and audio work, which the upstream port does not attempt, keep the risk above "Low".

## 14. SDK layer core (P1-13a, `runtimes/gc_wii/sdk/`)

Shared by every GameCube/Wii title. Built only with Clang (clang-cl on Windows) with `-ffp-contract=off`, no fast-math, an SSE2 baseline and no CET (`/CETCOMPAT:NO` for every image that hosts the SDK, `-fcf-protection=none` elsewhere; `Sdk::create` refuses a process with user shadow stacks) (ADR 0008 §4). From the root build it is a separate Clang project (ExternalProject) whose tests run as the CTest entry `gc_wii_sdk`. `MPT_REQUIRE_GC_WII` (ON in the `ci-*` presets) makes a missing Clang a configure error. It uses header-only core v1 types (`InputFrame`, `SaveMedia`, `FrameRate`, `DeterministicRng`, `StateHasher`, `Result`).

**C API headers.** `mpt_gc/*.h` are our own declarations, written from the public SDK surface. Party Board compiles against aurora's Dolphin headers (ADR 0007 §6), so OS objects (`OSThread`, `OSAlarm`, `OSMessageQueue`, `OSMutex`) are used **only as keys**: their state lives in the SDK's tables, and the game-side layout does not matter. The exception is `OSThreadQueue` head/tail, which we keep current. Structs whose fields we write (`PADStatus`, `DVDFileInfo`/`DVDCommandBlock`, `CARDFileInfo`, `CARDStat`, `ARQRequest`, `OSCalendarTime`, `DVDDir`) follow the public layout. P1-15 must `static_assert` them against the pinned headers.

**Determinism constants** (changing any of them changes every golden trail):

| Constant | Value |
|---|---|
| Time base | 40.5 MHz (bus clock / 4) |
| Time-query quantum | 405 ticks (10 µs) per `OSGetTime`/`OSGetTick` |
| NTSC field | 675,675 ticks exactly (60000/1001 Hz). PAL: 810,000 (50 Hz) |
| Epoch | `2001-01-01 + DeterministicRng(seed ^ 0x47435254_4345504F).below(range)`, range up to 2031-01-01 |
| Watchdog | a frame that runs more than 60 virtual seconds of time queries faults instead of hanging |
| Interrupt order | deadline, then source (VI, AI, DSP, ARAM, DVD, CARD, alarm), then scheduling order |
| Card serial | `0x4D50544743524431` ("MPTGCRD1", `kStableCardSerial`) on every blank or formatted card |
| New-thread MXCSR | `0x1F80` (round to nearest, no FTZ/DAZ) |

**Virtual time** (confirmed by architect as the reading of ADR 0008 §2). Each frame step sets `now = max(now, R_n)` with `R_n = epoch + n * field`: the field clock is a floor, so time spent in queries is never taken back and time never runs backwards. The VI retrace is frame-driven: it fires once at the start of frame `n >= 1`, never because a busy-wait ran past `R_n`, so one `stepFrame` is always one main-loop iteration. When every thread is blocked, the scheduler jumps `now` to the next interrupt deadline if it falls before `R_{n+1}`. SDK code never calls the public time API internally (CARD timestamps read the clock without a quantum).

**Scheduler.** SDK threads and the root coroutine (`main()`, the default thread at priority 16) are coroutines on arena stacks (256 KiB each, 32 threads). We use **our own context switch** (about 40 instructions per ABI, Win64 with TIB stack-bound swap, and SysV, all in `context.cpp` with documented frame layouts), accepted by architect with libco as the named fallback. A test on both OSes checks that every callee-saved register survives a round trip into a coroutine that clobbers them all, including xmm6–15 and the TIB stack fields on Win64, MXCSR and the x87 control word. No exception, `longjmp` or SEH unwind may cross a switch. Each stack has a 256-byte canary at its bottom, checked after every switch, so an overflow that runs through it is a deterministic fault (an overflow that skips over it is not caught). The best ready thread runs (lowest priority value, then FIFO). At every SDK call boundary, due interrupts run on the current thread's stack, and a strictly higher-priority thread that they made ready preempts the caller. Interrupts that fall due while every thread is idle run on a dedicated interrupt coroutine. `OSDisableInterrupts` only tracks its return value (ADR 0007 §7). Blocking inside an interrupt handler is a fault. `OSSetIdleFunction` runs the idle function once per frame step on the lowest-priority thread (P1-13b, §15).

**I/O.** DVD, ARAM DMA/ARQ and CARD complete inside the call. Callbacks run at the **next SDK call boundary** by default, so code that sets its "busy" flag after issuing the call still works. `SdkConfig::io_callbacks_before_return` runs them before the call returns instead. The mode is a **per-title constant** recorded here, never a user option. It reaches the build fingerprint through the source hash: for MP4 it is a constant in `mp4/src/abi.cpp`, which `build_id.cmake` hashes by content (ADR 0009 amendment after P1-16). **MP4's mode: callbacks before return** (P1-16 audit, §19). The disc is read through `DirectoryFileSource`, which takes entry order and sizes from the extracted `sys/fst.bin` (P1-09's layout, `files/` beside it), rejects unsafe names (`.`, `..`, separators, control bytes) and files missing or with the wrong size, and keeps recently used files open. Reads past the end of a file return zeros.

**PAD mapping** (canonical `Button` → GameCube):

| Canonical | GameCube | Notes |
|---|---|---|
| A, B, X, Y, Start, Z | A, B, X, Y, START (MENU), Z | |
| D-pad | D-pad | |
| L / R | L / R click + analog 255 | A digital press reports full travel. Analog 255 also sets the click bit |
| `trigger_l/r` | `triggerL/R` | 0..255 pass through |
| left stick, C stick | `stickX/Y`, `substickX/Y` | `round(v * 127 / 32767)`, -32768 folded to -32767, +Y up |
| C-Up/Down/Left/Right | C stick ±127 | only on an axis whose analog value is 0. Opposite presses cancel |
| Select, Home, ZL | none | |
| `connected == 0` | `err = PAD_ERR_NO_CONTROLLER` | all fields zero |

The `InputFrame` is latched at the post-retrace point: the pre-retrace callback still sees frame `n-1`, and the post-retrace callback and the main loop see frame `n`. Frame 0 latches at the start of the step. `PADClamp` is the SDK's octagon clamp (stick 72/40/15, C stick 59/31/15, triggers 30..180).

**CARD.** Slot A is served from `RuntimeConfig::save_media` (`SaveMedia::format_version` 1, our own little-endian format documented in `card.cpp`), and slot B is always empty. Card size is 4, 8 or 16 Mbit (16 for blank cards, which have 251 user blocks). The generation increments on format, create, write, set-status, rename and delete. Image and generation live in the arena, so `loadState` restores both (ADR 0004 rule 9). Timestamps are virtual seconds since 2000 (synthetic dates, rule 8).

**Save states and hash.** `saveState()` is the whole arena (SDK state, 24 MB main RAM, 16 MB ARAM, the card, stacks and register contexts) plus a header. `loadState` rejects a different build, configuration, arena base or code base. `hashState()` is the SDK's manifest area, by value: pointers as arena offsets, function pointers and pointers outside the arena as the title's stable IDs (`SdkConfig::pointer_id`; for MP4 the module's symbol table, §20) or else a presence sentinel, register contexts excluded. Game memory is the title manifest's job (P1-18).

**Math.** `mpt_fmaf` is a correctly rounded software fmaf (round-to-odd over an exact two-sum in binary64). It matches hardware FMA3 on 2M random and adversarial inputs (where the CPU and OS support FMA3). `gc_math_redirect.h`, forced-included into the game module, includes `<math.h>`/`<cmath>` itself and then maps `fmaf` and `std::fmaf` onto it. A link-time check (`check_no_host_math.cmake`, `llvm-nm`) fails if any object leaves `fma`/`fmaf` or a host transcendental undefined (§17); it runs on the SDK, on a probe that uses the redirect, and (expected to fail) on a probe that does not. P1-15 runs it on the game module. This meets ADR 0008 §5 condition 1. `mpt_sinf/cosf/tanf/acosf/atan2f` are evaluated in double with no host transcendental and are within 2 ulp. **MTX/VEC/QUAT are not in the SDK:** aurora's MTX at the pin (with patch 2) is the only `PS*`/`C_*` implementation (architect ruling, ADR 0007 §6), and its goldens are P1-14's. Fast casts truncate, saturate, and map NaN to 0. Golden hashes pin trig/fmaf and a synthetic-game trail.

**Faults.** A fatal error in game code records the message and stops the runtime: the faulting thread is never resumed and `stepFrame` returns `StepStatus::faulted`. The frame step's own host-side code never calls the fatal path, and nothing in the SDK aborts the process. Calling the C API from host code instead of game code is a programming error, reported with `std::logic_error`.

**Split.** P1-13a (this layer core) and P1-13b (§15). Other follow-ups: `OSLink`/`OSUnlink` (replaced by the P1-15 loader shim), `OSStopwatch`, GD/GX (P1-14), fixed arena and module bases (P1-16), ARM64 context switching.

## 15. SDK audio and movies (P1-13b, `runtimes/gc_wii/sdk/`)

**Status:** the AI layer, engine stepping, null audio, idle-function scheduling and THP decoding are done and tested on synthetic data. MusyX is integrated at the bumped pin (§17).

**AI and virtual AI ticks** (`mpt_gc/ai.h`, `src/ai.cpp`).
- There is one virtual interrupt (source `ai`, event `ai_dma`) per DMA block.
- A block's period is its length in frames at the DSP rate, converted to 40.5 MHz ticks with an integer remainder accumulator. MusyX's block is 160 frames at 32 kHz, which is **202,500 ticks exactly** (5 ms).
- At a tick, in interrupt context:
  1. the block that just played goes to the output (`frames_out` counts it in either output mode);
  2. the registered DMA callback runs and sets up the next block;
  3. the next tick is scheduled from the block length now set.

  So a callback's block plays one tick later (5 ms latency).
- The sample count is a pure function of virtual time. With no busy-waits, frame `n` ends with exactly `floor((R_{n+1} - epoch - 1) / 202,500)` blocks played. Busy-waits that run virtual time ahead also run audio ahead, by at most what they waited, and deterministically.
- `AIGetDMABytesLeft` is derived from virtual time.
- The DVD audio-streaming registers (`AISetStream*`) are stored and hashed but never played. MP4 does not seem to stream disc audio. P1-16 confirms.

**Sound-engine attachment** (`mpt/gc/audio.h`, `SdkConfig::audio_engine`).
- A pull-mode engine is `render(user, out, frames)` (s16 stereo, 32 kHz) plus `hash(user, hasher)`.
- When an engine is attached, the SDK plays the part MusyX's own AI hookup plays on the console: DMA runs from boot, and the engine's callback is the registered DMA callback until game code registers its own.
- `AIRegisterDMACallback` then returns the engine's callback. A game callback that chains to it (THPSimple) therefore renders the engine block first, and can then mix movie audio into `AIGetDMAStartAddr()`, as on the console.
- The engine's two DMA blocks are in `SdkState` (the arena). Their contents are game-visible, so they are saved, and they are identical in both output modes.

**Null audio** (`SdkConfig::audio_output = false`). Only the host's copy of the PCM is dropped. The engine renders every block, every callback runs, and every register and counter is the same. A test checks that null and real output give identical hash trails and `audioFrames()` (E6).

**Registered globals** (`SdkConfig::state_regions`).
- Memory outside the arena that holds game-visible state (MusyX's statics now, the game module's later) is saved and restored with the arena.
- It is never hashed raw. Its owner hashes it by value, through the engine's `hash`.
- The save-state header records the region count and total size, and `loadState` rejects a mismatch.

**Hash manifest additions** (all by value):
- the AI registers: DMA address (as an arena offset), callback (a stable ID through `SdkConfig::pointer_id`, §20), pending tick deadline, period and remainder, tick and frame counters, and the stream registers;
- the attached engine's own hash;
- the THP init flag;
- the idle function (sentinel), its parameter (arena offset), its thread slot and its run count.

These additions changed the SDK's golden trail in `test_state.cpp`.

**Idle function.**
- `OSSetIdleFunction` runs the function on an SDK thread at priority 31, with its own arena stack. The game's stack argument is not used.
- The thread is made ready once at the start of every frame step, after the retrace. It runs when every other thread is waiting, at most once per step.
- If it blocks, it keeps its place and is not re-armed until it has finished. `NULL` stops it.
- **Open for P1-16:** check that the idle function in `game/thpmain.c` returns after a bounded amount of work. A function that loops forever without an SDK call would hang the step. A loop that queries the time trips the watchdog instead.

**THP decoder** (`mpt_gc/thp.h`, `mpt/gc/thp.h`, `src/thp.cpp`). This is **our own** decoder, not a port of Party Board's THP sources, because P1-13b had no approved checkout of the pinned Party Board tree to port from. It is written from the public JPEG (ITU T.81) and DSP-ADPCM formats.
- **Video:** baseline sequential Huffman JPEG, 8-bit, 4:2:0 (Y 2x2, then Cb and Cr 1x1, in that scan order). It handles DQT, DHT, DRI, SOF0, SOS and restart markers, and skips APPn and COM. Width and height must be multiples of 16.
  - Output is GX **I8 tiles** (8x4): Y at full size, U and V at half size. This is what THPSimple binds as textures.
  - The IDCT is a separable integer transform with a fixed Q13 basis (`kDctBasis`), so the output is bit-identical on every platform. It is not bit-identical to the console's float IDCT, but that only affects the picture.
- **Audio:** DSP-ADPCM with an exact integer decode of both channels, either interleaved (`THP_AUDIO_INTERLEAVE`, 0) or one channel after the other (`THP_AUDIO_NOINTERLEAVE`, 1).
- **Container:** the header, the component info (versions 1.0 and 1.1) and the frame chain (`nextTotalSize`), all bounds-checked.
- The C API has no length argument, as on the console. Reads are bounded by the end of the arena, or by 16 MiB for data outside it.
- **To verify on the dump lane (P1-16):**
  - whether real THP streams use byte stuffing exactly as standard JPEG does;
  - the `flag` values of `THPAudioDecode`;
  - how mono movies lay out their audio;
  - per-track sizes in multi-track 1.1 files.

  Our tests only prove self-consistency on streams we generate.

**Tests (E10, synthetic data only).**
- `test_audio.cpp` uses a toy pull-mode engine (a sequencer, voices that end on their own, a MIDI controller) that game code polls in a non-yielding busy-wait shaped like `SNDGRP_WAIT`. It covers:
  - null against real output;
  - exact samples per frame;
  - save states with registered statics;
  - perturbation of engine state;
  - a chained THP-style callback;
  - game-driven DMA at 48 kHz;
  - `AIStopDMA`;
  - the idle thread.
- `test_thp.cpp` builds a THP movie at test time with an integer JPEG and ADPCM writer (`thp_encoder.h`). It covers:
  - decode quality and restart intervals;
  - goldens for video and audio;
  - malformed streams;
  - walking the container;
  - playback through the idle function with movie audio in the AI stream, across save states and with null audio.

**MusyX at Party Board's pin (historical; resolved by the ADR 0009 amendment after P1-13b, §17).** ADR 0009 first pinned musyx at `2a0e01aeff0d06a9ee9e12966c435cb75144af9e` (Party Board's submodule commit). At that commit:
- There is **no PC renderer and no `sndPCRender`**.
  - `hw_pc.c` builds the DSP command list but never mixes it.
  - Nothing advances the DSP voices, so voices never end and `SAMPLE_END` messages never reach the synth. Voice status the game polls would be wrong.
  - The PC branch of `hw_aramdma.c` is all stubs (`aramStoreData` returns `NULL`), so sample upload does not work.
- `hw_pc.c` uses Win32 mutexes or pthreads for its critical section.
- A compile-only probe with clang-cl 22 and our flags (`-ffp-contract=off`, with `gc_math_redirect.h` force-included):
  - 32 of the 33 sources compile;
  - `hw_aramdma.c` fails, because `aramUploadData` is declared with `u32` in `hardware.h` and `unsigned long` in the PC branch;
  - the host libm imports are `powf` (reverb), and `floorf` and `fmodf` (sequencer and volume conversion). `floorf` and `fmodf` are exactly specified, but `powf` would need our math library (ADR 0008 §5).
- The pull-mode renderer the ADRs describe landed upstream in `9fc772d` ("Sdl3 (#14)", 2026-09-23). It brings `sndPCRender`, `sndPCConfigure`, a null host backend (`hw_pc_null.c`) and integer 5 ms control cycles. Upstream `main` is at `86851a3a8d51788d166ea42d137b49526f570400`, and that tree is also CC0.

**Decision (architect, 2026-10-13): option 1, bump to `86851a3`.** The two options were:
1. bump musyx to a commit that has the PC renderer, for example `86851a3`;
2. keep the pin and write our own DSP-voice renderer and ARAM glue. ADR 0007 rejects reimplementing it.

Party Board's `msm` was written against the pinned API, so a bump also needs a check that `msm` builds against the newer headers.

MusyX now plugs into the `AudioEngine` hook as planned (§17):
- `render` is `sndPCRender`, rendering silence before `sndInit`;
- MusyX's statics are registered as state regions;
- allocation goes through `sndSetHooks` into the game heaps;
- `hash` covers voice, sequencer, stream and MIDI-controller state by value.

## 16. GX layer (P1-14, `runtimes/gc_wii/gx/`)

**What is built.** aurora at `514339438178ef2bed1b14e5149d90ece0c6e0cc` (`gx/aurora.lock`) supplies three libraries, built from explicit source lists and without `aurora_core`:
- the GX front end plus MTX, which is game code;
- a null renderer;
- the Dawn renderer, on a headless device with `cpu_rgba8` readback.

Dawn is pinned at `13abc3bc8ea2d3c2050f9e77a12d012108ceee24` by `gx/dawn.lock`, with 8 `DEPS` entries pinned and 73 skipped (DXC among them). It is built by `gx/tools/build_dawn.cmake`. Details are in `runtimes/gc_wii/gx/README.md`.

**Patch set** (`gx/patches/aurora/`):

| ADR 0007 patch | What it does |
|---|---|
| 1 | Overscan from the render mode (backport of `e2fc845`). |
| 2 | MTX fixes (backports of `4143fc1` and `f8573d3`), and `cosf` in `GXInitLightSpot`. |
| 3 | Headless device. |
| 4 | Synchronous pipelines, always on in play and in tests. |
| 5 | `GXPeekZ` traps. |
| 6 | `gx::reset_caches_for_load_state()`, and save-state access to the texture/TLUT object ID counters. |
| 7 | `GXSetZTexture`: BP registers, shader `frag_depth` from the last TEV stage's texel, Z8/Z16/Z24X8 static decode, and Z8/Z24X8 EFB depth copies. |
| 7b | Partial `GXCopyTex` clears, our port of the idea in `9c0bf66`. |
| 8 | FIFO observer. |
| 8b | Vertex-size helpers moved into the front end. |
| 9 | FXC: `use_dxc` is not enabled. |
| (none) | Null renderer stub table. |

**MP4-specific status.**
- m419's pattern (copy EFB depth as Z24X8, then `GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0)` on the last TEV stage) renders correctly in the synthetic `depth_copy_ztexture` scene.
- m427's partial copy clear is fixed (scene `copy_tex_partial`).
- Both still need the dump lane for m419 and m427 themselves.

**Front-end math.** Front-end and MTX trig goes through the SDK's `mpt_*` functions (forced include `mpt_gx/math_redirect.h`), and `fmaf` through `mpt_fmaf`. `gx_frontend_no_host_math` checks this at link level.

**MTX goldens** (`gx_mtx_golden`) are recorded against aurora's MTX. They cover the `PSVEC` reciprocal-square-root hard cases: powers of two ±1 ulp, a mantissa sweep across the `frsqrte` table, subnormals, signed zeros, overflow, infinities and NaN. They also cover trig, linear algebra and quaternions. This is ADR 0008 §5 condition 2.

**Added SDK entry points:**
- `GXSetDrawSync` (the callback fires synchronously) and `GXReadDrawSync`;
- `GXWaitDrawDone` and `GXResetWriteGatherPipe` (no-ops);
- the perf-metric calls (0);
- `GXNtsc480Prog`.

**Determinism notes from P1-14:**
- **MTX goldens hash NaN results as one canonical quiet NaN.** IEEE 754 leaves a NaN's sign and payload unspecified, and x86 returns the NaN of whichever operand the compiler placed first. On the reference machine, `-Od` and `-O2` already disagree on 38 NaN signs in the `vec_c` group, and the first Linux run of the gate failed on that group. Game state may contain NaN floats, and the hash manifest hashes fields by value. **Architect must decide whether `StateHasher` canonicalizes NaN floats** (ADR 0008 §8). If it does not, a NaN in game state can split the hash between builds or platforms.
- `std::log2` in `GXLoadLightObjImm` reached the host libm (patch 0015 replaces it with `std::countr_zero`). The libm link check now also recognises `__imp_` DLL imports; it had been blind on Windows.

**For P1-16 (save states and the manifest).** Front-end state outside the arena must become registered `SaveState` globals and manifest areas:
- `__gx` (the shadow registers);
- the CPU-side `g_gxState`. It holds game-visible state such as the viewport, the projection, matrices, vertex formats, `zTex*` and the copy settings, next to renderer handles that must not be saved;
- the FIFO buffer between drains;
- the draw-sync callback and its last token (`sdk_additions.cpp`);
- the texture/TLUT object ID counters.

Decide whether the ID counters are hashed. They are stored in game memory (`GXTexObj`), so they are game state; the earlier plan was "saved, not hashed". The callback pointer needs P1-16's function-ID table.

**Still open:**
- `GXGetYScaleFactor` is not implemented. It is used on the PAL path only, and we target `GMPE01_00`.
- The copy-destination debug trap with its m415 allow-list belongs with P1-15/P1-16, where game memory exists.
- The object ID counters become registered `SaveState` globals in P1-16 (the accessors are `mpt::gx::object_id_counters()` and `set_object_id_counters()`).

---

## 17. MusyX on the SDK (P1-13b part 2, `runtimes/gc_wii/musyx/`)

**Pin.** musyx `86851a3a8d51788d166ea42d137b49526f570400`, tree `7177c167f5a5b1e5f63ab927e44d412982f96588`, CC0 1.0 (ADR 0009 amendment after P1-13b). The pin is recorded in `runtimes/gc_wii/mp4/decomp.lock`. That lock also has Party Board's entry, whose `extern/musyx` gitlink stays at `2a0e01a` with the override reason. P1-15 added Party Board's tree hash and the aurora pin (§18).

**Getting the source.** Run `mpt_fetch_sources --lock runtimes/gc_wii/mp4/decomp.lock --cache <MPT_SOURCE_CACHE>`. The source lands in `<MPT_SOURCE_CACHE>/musyx/<commit>`, which is the default `MPT_MUSYX_DIR` and the same layout aurora uses. P1-13b fetched it with a musyx-only copy of the lock, because Party Board was not to be fetched yet. Configure checks that the checkout is clean and at the locked commit (`MPT_MUSYX_ALLOW_UNPINNED` is for development only). Without a checkout, the target is skipped, unless `MPT_REQUIRE_MUSYX` is ON. **CI does not fetch musyx yet**, so the musyx tests run only on developer machines until qa-reviewer adds the fetch step and sets `MPT_REQUIRE_MUSYX` in the `ci-*` presets.

**What is linked** (`mpt_musyx_core`, all C, compiled with Clang under the SDK's strict-FP flags):
- upstream's runtime source list at the pin, with the null host backend (`hw_pc_null.c`);
- **not** linked: `hw_pc_sdl.c` (an SDL device on its own thread) and the debugger (`src/musyx/debugger`). The runtime calls the debugger's instrumentation hooks unconditionally, so `musyx_debugger_stubs.c` supplies them as no-ops (never paused, nothing recorded);
- wrappers that `#include` upstream files unchanged:
  - `snd_midictrl.c` and `stream.c`, so the hash can read their file-scope MIDI-controller and stream tables;
  - `synthdata.c` with `malloc`/`free` mapped to `salMalloc`/`salFree`, so `sndConvert32BitSDIRTo64BitSDIR` allocates through the game's hooks;
- `hw_memory.c` replaced by `musyx_memory.c`, which is upstream's code without its host-heap fallback. If MusyX allocates before `sndSetHooks`, that is a game fault (`OSPanic`).

`MPT_MUSYX_VERSION` defaults to 2.0.0, upstream's default. **P1-15 confirmed 2.0.0 for MP4's `msm`** (§19).

**Compiler flags and math.** Every musyx unit is force-included with `musyx_state_section.h`, which pulls in `mpt_gc/libm_redirect.h`. That header sends `powf`, `pow`, `sin`, `cos`, `sinf`, `cosf`, `tanf`, `acosf` and `atan2f` to `mpt_*`:
- The audit of the linked sources found `powf` (the ADSR time-cents curve and reverb comb coefficients), `sin`/`cos` (the resampler's polyphase table) and `cos` (`sndCos`).
- `floorf`, `fmodf`, `lround`, `sqrt`/`sqrtf` and `fabs` are exactly specified, so the host versions stay (ADR 0008 §5).
- `mpt_powf` uses IEEE pow special cases, then `exp2(y log2 x)` in double from exact frexp/ldexp and series. It is within 1 ulp of the double reference over musyx's ranges: 2^(1.27e-8 tc) for time cents up to ±2^31, and 10^(-3 len / (32000 t)) for reverb.
- `mpt_sin`/`mpt_cos` (double) are within 1e-15. Goldens pin all three (`test_math.cpp`).
- The upstream `aramUploadData` declaration conflict is already fixed at this pin (condition (c) on Windows).

**Link checks** (llvm-nm, on `mpt_musyx_core.lib`):
- `gc_musyx_no_host_math` uses `sdk/tests/check_no_host_math.cmake`. It rejects every non-exact libm function in float, double and long-double forms, `*_finite` and `sincos`, with or without the `__imp_` DLL-import prefix. The P1-13a pattern missed `__imp_` imports in C objects, and the GX check found the same gap (#27). Negative probes check that it catches host `fmaf`, `powf` and `log2`.
- `gc_musyx_no_host_services` uses `musyx/check_no_host_services.cmake`. It rejects pthread/C11/Win32 thread, mutex, condition-variable, event, SRW and sleep calls, SDL, the host clocks, `rand`, and the host heap (`malloc`/`calloc`/`realloc`/`free`). `qsort` (used by the asset decoder) is allowed: its comparator plus the conflict check make the result independent of the sort order.

**Attachment** (`mpt/gc/musyx_engine.h`). Before each `Sdk::create`, the runtime calls `MusyxEngine::reset()`, which restores musyx's statics to their process-start image and sets the PC renderer to 32 kHz stereo. It then puts `MusyxEngine::engine()` in `SdkConfig::audio_engine` and `MusyxEngine::stateRegions()` in `state_regions`.
- `render` calls `sndPCRender(out, 160)`, one 5 ms control cycle per AI tick, or writes silence while `!sndActive`. It is called only from the SDK's AI tick inside `stepFrame` (condition (b)). Before any audio device exists, the null backend makes `hwDisableIrq` and the other critical-section calls no-ops.
- **State regions.** The section pragmas put every writable global and static of the musyx units into `.mpxd$m`/`.mpxb$m` on COFF, or `mpt_musyx_data`/`mpt_musyx_bss` on ELF. Linker-sorted markers bound them on COFF, and `__start_`/`__stop_` symbols bound them on ELF. Data and bss need separate groups on COFF, because the linker moves uninitialized contributions to the end of a section. Both link.exe and lld-link were checked, and ELF emission was checked with a cross-compile. The regions hold about 1.7 MB (2.5 KB of it initialised data), mostly the resampler table and mix buffers. They are saved with the arena and never hashed raw.
- **Hash manifest v1** (`musyx_glue.c`, by value):
  - engine flags, real time, global variables and track volumes, `synthInfo`, and the PC render clock;
  - per synth voice: macro state and addresses (as arena offsets), ids, flags, notes, pitch, volume and envelope, timers, local variables and message counters;
  - per DSP voice: state, sample id and position, pitch, volumes and ADSR;
  - per sequencer: state, ids, mutes, volume groups and flags;
  - the MIDI-controller tables per sequencer channel and per SFX voice, last notes and dirty flags;
  - the stream table.

  PCM and mix and resampler history are presentation only and are left out. P1-18 reviews the manifest against MP4's `msm` queries.

**Tests** (`musyx/tests`, synthetic data generated at test time by `synthetic_sound.h`). The data is a project with an SFX group and a song group, a pool of three macros, PCM16 samples, and a looping song with notes and controller-16 events. A synthetic board sets hooks on an arena heap, initialises MusyX, pushes both groups, plays the song, polls the controller and the song's validity every frame, and spins on SFX the way `SNDGRP_WAIT` does. The tests check:
- the spin terminates after about the 50 ms sample;
- the controller follows the song;
- **null and real output give identical hash trails, controller values and wait times** (condition (d));
- two runs match;
- the engine advances only inside `stepFrame` (`sndPCGetRenderInfo`);
- save states restore musyx mid-song;
- the hash reacts to a MIDI controller and to a sequencer pause;
- goldens pin the PCM and the trail.

**Merge conditions** (ADR 0009 amendment after P1-13b):
- (a) Party Board's `msm` builds against the pin: **met at build level by P1-15** (§19). Its disc structures are converted to host byte order (patch 0015).
- (b) met: render on the game thread only, and the services link check passes.
- (c) clang-cl: met. **Linux Clang: not yet run** (there is no Linux machine here, and CI does not fetch musyx).
- (d) met on synthetic data.

## 18. Input coverage (E2, P1-08)

**Owner of this section:** input-mapping. Sign-off is joint with runtime-gc-wii (E2).
**Profile:** `input/profiles/mp4/default.json` (GameCube layout, the same bindings as the built-in default). Every GameCube control has a keyboard source and a gamepad source:
- **Keyboard:** WASD for the stick (left shift gives half tilt), TFGH for the C-stick, arrow keys for the D-pad, K/Space for A, J for B, L for X, I for Y, U for Z, Q/E for L/R (click plus full trigger), Enter for Start.
- **Gamepad:** positional face buttons (A = south, B = east, X = west, Y = north), the right shoulder for Z, both sticks, analog triggers. L/R click at InputFrame trigger 230 of 255 (raw units, before PADClamp). The game's own analog-L/R bit turns on far earlier, at raw 94, but no minigame reads it (below).

So a minigame is covered on both devices unless it needs something a key cannot produce. Per-minigame overrides exist only for those cases.

**How the inputs were found.** We read the minigame modules of the pinned Party Board source (`98ac07f`, §1) in place, and copied nothing. The scan collects the pad arrays each module reads with a player's pad index: buttons, stick, C-stick, analog triggers and the stick-as-D-pad. Reads of the literal pad 0 next to debug prints were ignored, and so were CPU writes into the pad arrays. The button lists come from mask tests on those reads. Rows marked † were traced by hand. Button lists can be incomplete where a module tests buttons through tables or reused locals. That cannot cost coverage, because every button is bound on both devices, but the dump lane should confirm it.

**Trigger units.** The game reads triggers after PADClamp (§18.1 "Facts for the input columns"): raw 0..255 reads 0 at or below raw 30, raw − 30 above it and 150 from raw 180 up. Every trigger threshold below is a value the game compares the **clamped 0..150** trigger against; "raw" is the InputFrame value (0..255) that reaches it. A key press and a full gamepad pull both give raw 255, clamped 150.

**Findings:**
- **Analog triggers matter in one party minigame, Bowser's Bigger Blast.** It reads R pressure: a partial pull holds the plunger part-way, and the digital R click commits. Its override turns on `stick_half_affects_triggers` and leaves R to the trigger threshold. Left shift + E then holds the plunger at half pressure (raw 128, clamped 98) without clicking, and E alone commits. Raw 128 is past the analog-R bit edge, so the half press sets the game's analog-R bit, but no module reads that bit: the module reads the R pressure directly (`HuPadTrigR`). The half press does not click; §18.1 proves R=128 and R=255 differ.
- **Every other trigger read is a threshold or saturates early,** so a full key press is correct (Makin' Waves is not among them: its `L > 80` read sets a flag nothing reads, and its L is the digital L click, §18.1):
  - Photo Finish and Long Claw of the Law: L/R past clamped 20 (raw above 50);
  - Reversal of Fortune: R past clamped 10 (raw above 40);
  - Mario Speedwagons: throttle saturates at clamped 140 (raw 170);
  - Dungeon Duos: L/R strength is the clamped trigger / 150 (full from raw 180).
- **The game turns trigger pressure into digital bits itself, but no minigame reads them.** `HuPadRead` sets its own analog-L/R button bits (0x4000 / 0x2000) when the clamped trigger has a bit of `0xC0` set: **clamped 64 and up, raw 94 and up** (§18.1). The clamped trigger never exceeds 150, so there is no 192 threshold. No m4xx module tests a pad word against those bits (m423 uses 0x4000/0x2000 as collision flags; the free-play menu reads the bits). The 0x40 and 0x20 that minigames test are the digital L and R clicks, which the SDK sets only for a key press or a trigger at raw 255 (a gamepad gets there through our raw 230 click threshold).
- **L click as an alternative to A.** Makin' Waves, Goomba's Chip Flip and Balloon of Doom accept the L click (`HuPadBtnDown & 0x140`: A or the L click) as an alternative to A, each in one state (§18.1). Dungeon Duos also tests 0x140 (A or the L click) and 0x20 (the R click) in some states; its row does not list them as inputs, since §18.1 has not proven them. Both clicks are bound on both devices, so coverage is unaffected.
- **Reversal of Fortune also reacts to L,** which its row does not list as an input: in §18.1's run an analog-L pull on top of stick, A and R changed game state (t1375 on every port). Which L read that is has not been traced, so the row records it as a note, not as an input with a mechanism. L is bound on both devices, so coverage is unaffected.
- **No stick-rotation minigame was found.** Every `atan2` on stick input is a facing direction. Keyboard stick magnitude is full or half tilt (left shift).
- **No microphone, motion or GBA input** (§12).
- **Rapid button presses:** The Great Deflate rewards how fast A is pressed (its CPU path injects A presses). The profile binds A to two keys (K, Space) as on every row. Turbo is an accessibility assist behind a per-ruleset fairness flag, not a profile binding.

**Party-eligible rows** are the minigames a regular party can launch, confirmed by P1-17 from the board code (§18.1 "Which minigames a party can launch"): **46 minigames**. They are every row of the §12 categories 4-player, 1-vs-3, 2-vs-2, Battle, Bowser and Fortune (44), plus two Story minigames, Bowser Wrestling (m457) and Panels of Doom (m458): board 5's Bowser space launches them for a Mega-size player (ruling M4-U3, `docs/phases/phase-1.md` §10, 2026-11-25). Their rows say "Story (party-eligible: board 5's Bowser space)". The other Story minigame and the Extra Room ones are excluded (E2). The five **Item** minigames (m445-m449: Bowser Bop, Mystic Match 'Em, Archaeologuess, Goomba's Chip Flip, Kareening Koopa) are **not party-eligible**: no board code launches them, and they are reached only from Mini-game mode's free play and Story's board clear (§18.1 "Where the Item minigames come from"). Their rows stay in the table, marked "Item (not party-eligible)", because Mini-game mode still needs their mapping and both E2 proofs ran on them. That makes **51 rows**, 46 of them party-eligible.

**Sign-off (E2, ruling M4-E).** Both proofs are in: the device side (M4-08, the Keyboard and Gamepad columns, @ `9dac583`) and the game side (M4-07, §18.1, run at `1545b13`, merged in PR #91 as `c8e6ace`, corrected in §18.1 at `d7c1fb0` with a Makin' Waves rerun: PR #91's Makin' Waves L proof was a false positive; Kareening Koopa proven in §18.1 at `3cf989f` after its port fix, patch 0040; Take a Breather proven with its L and R clicks in §18.1 at `9c51eb0`, rerun at `bda4902`; Bowser Wrestling and Panels of Doom added by P1-17 and ruling M4-U3, proven in §18.1 at `c5f2e62`, rerun at `e7dde31`). Under the M4-E amendment of 2026-11-20 (Option C, `docs/phases/phase-1.md` §10), a party-eligible row with at least one listed control proven game-side, a passing device proof and its unproven controls named in §18.1 is accepted with that reason; a row with nothing proven game-side must be fully proven. Standing:
- **43 of 51 rows fully proven** on both sides.
- **7 accepted under Option C** (§18.1, "Accepted under M4-E (Option C)"): Mario Medley (L click, R click not proven), Long Claw of the Law (A), Makin' Waves (L click), Pair-a-sailing (B), Dungeon Duos (B, X, Y) and Balloon of Doom (L click), all at `b1b2bd7`, and Bowser Wrestling (L click, R click not proven: Z+A diverges with them) at `c5f2e62`.
- **Goomba's Chip Flip** is the only row not proven: partly proven (L click not proven). It is an Item row, not party-eligible, so Option C does not apply to it.
- **Panels of Doom** is fully proven (counted above): stick and A on all four ports.
- **Take a Breather** is fully proven (counted above). Its real inputs are the L and R clicks, not the stick and A this table used to list (§18.1 at `b1b2bd7`). The rerun at `bda4902` proved both clicks on all four ports, each diverging at t415 with the Z null idle (§18.1 at `9c51eb0`).

Of the 46 party-eligible rows, **39 are fully proven and 7 accepted: all 46 are signed off.**

| ID | Display name | Category | Inputs read | Special handling | Keyboard | Gamepad | Sign-off |
|---|---|---|---|---|---|---|---|
| `mp4.minigame.manta_rings` | Manta Rings | 4-player | stick, A | — | default; proof: `mp4 E2 device proof: keyboard` / `manta_rings` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `manta_rings` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.slime_time` | Slime Time | 4-player | A | — | default; proof: `mp4 E2 device proof: keyboard` / `slime_time` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `slime_time` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.booksquirm` | Booksquirm | 4-player | stick | — | default; proof: `mp4 E2 device proof: keyboard` / `booksquirm` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `booksquirm` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.trace_race` | Trace Race | Battle | stick | — | default; proof: `mp4 E2 device proof: keyboard` / `trace_race` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `trace_race` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.mario_medley` | Mario Medley | 4-player | A, B, L click, R click (prompted)† | L/R as digital clicks | default; proof: `mp4 E2 device proof: keyboard` / `mario_medley` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `mario_medley` @ `9dac583` | accepted (M4-E Option C): game side partly proven, reason §18.1 @ `b1b2bd7` |
| `mp4.minigame.avalanche` | Avalanche! | 4-player | stick, A | — | default; proof: `mp4 E2 device proof: keyboard` / `avalanche` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `avalanche` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.domination` | Domination | 4-player | A | — | default; proof: `mp4 E2 device proof: keyboard` / `domination` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `domination` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.paratrooper_plunge` | Paratrooper Plunge | 4-player | stick | — | default; proof: `mp4 E2 device proof: keyboard` / `paratrooper_plunge` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `paratrooper_plunge` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.toads_quick_draw` | Toad's Quick Draw | 4-player | stick, A | — | default; proof: `mp4 E2 device proof: keyboard` / `toads_quick_draw` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `toads_quick_draw` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.three_throw` | Three Throw | 4-player | A, B | — | default; proof: `mp4 E2 device proof: keyboard` / `three_throw` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `three_throw` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.photo_finish` | Photo Finish | 4-player | stick, A, L/R past clamped 20 (raw above 50)† | trigger as threshold | default; proof: `mp4 E2 device proof: keyboard` / `photo_finish` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `photo_finish` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.mr_blizzards_brigade` | Mr. Blizzard's Brigade | 4-player | stick | — | default; proof: `mp4 E2 device proof: keyboard` / `mr_blizzards_brigade` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `mr_blizzards_brigade` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.bob_omb_breakers` | Bob-omb Breakers | 4-player | stick, A, B, X, Y | — | default; proof: `mp4 E2 device proof: keyboard` / `bob_omb_breakers` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `bob_omb_breakers` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.long_claw_of_the_law` | Long Claw of the Law | 4-player | stick, A, L/R past clamped 20 (raw above 50)† | trigger as threshold | default; proof: `mp4 E2 device proof: keyboard` / `long_claw_of_the_law` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `long_claw_of_the_law` @ `9dac583` | accepted (M4-E Option C): game side partly proven, reason §18.1 @ `b1b2bd7` |
| `mp4.minigame.stamp_out` | Stamp Out! | 4-player | stick, A† | — | default; proof: `mp4 E2 device proof: keyboard` / `stamp_out` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `stamp_out` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.candlelight_flight` | Candlelight Flight | 1-vs-3 | stick, A | — | default; proof: `mp4 E2 device proof: keyboard` / `candlelight_flight` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `candlelight_flight` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.makin_waves` | Makin' Waves | 1-vs-3 | stick, A, L click† | L click as an alternative to A in one state (the `L > 80` read is dead) | default; proof: `mp4 E2 device proof: keyboard` / `makin_waves` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `makin_waves` @ `9dac583` | accepted (M4-E Option C): game side partly proven, reason §18.1 @ `b1b2bd7` |
| `mp4.minigame.hide_and_go_boom` | Hide and Go BOOM! | 1-vs-3 | A, B, X, Y | — | default; proof: `mp4 E2 device proof: keyboard` / `hide_and_go_boom` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `hide_and_go_boom` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.tree_stomp` | Tree Stomp | 1-vs-3 | stick, A, B† | — | default; proof: `mp4 E2 device proof: keyboard` / `tree_stomp` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `tree_stomp` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.fish_n_drips` | Fish n' Drips | 1-vs-3 | any of A, B, X, Y, L click, R click | — | default; proof: `mp4 E2 device proof: keyboard` / `fish_n_drips` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `fish_n_drips` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.hop_or_pop` | Hop or Pop | 1-vs-3 | stick, A | — | default; proof: `mp4 E2 device proof: keyboard` / `hop_or_pop` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `hop_or_pop` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.money_belts` | Money Belts | 1-vs-3 | stick | — | default; proof: `mp4 E2 device proof: keyboard` / `money_belts` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `money_belts` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.goooooooal` | GOOOOOOOAL!! | 1-vs-3 | stick, A | — | default; proof: `mp4 E2 device proof: keyboard` / `goooooooal` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `goooooooal` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.blame_it_on_the_crane` | Blame it on the Crane | 1-vs-3 | stick, A | — | default; proof: `mp4 E2 device proof: keyboard` / `blame_it_on_the_crane` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `blame_it_on_the_crane` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.the_great_deflate` | The Great Deflate | 2-vs-2 | A (press rate)† | rapid presses; turbo is a ruleset assist | default; proof: `mp4 E2 device proof: keyboard` / `the_great_deflate` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `the_great_deflate` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.revers_a_bomb` | Revers-a-Bomb | 2-vs-2 | stick, A | — | default; proof: `mp4 E2 device proof: keyboard` / `revers_a_bomb` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `revers_a_bomb` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.right_oar_left` | Right Oar Left? | 2-vs-2 | A, B | — | default; proof: `mp4 E2 device proof: keyboard` / `right_oar_left` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `right_oar_left` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.cliffhangers` | Cliffhangers | 2-vs-2 | stick, A | — | default; proof: `mp4 E2 device proof: keyboard` / `cliffhangers` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `cliffhangers` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.team_treasure_trek` | Team Treasure Trek | 2-vs-2 | stick, X, Y | — | default; proof: `mp4 E2 device proof: keyboard` / `team_treasure_trek` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `team_treasure_trek` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.pair_a_sailing` | Pair-a-sailing | 2-vs-2 | stick, A, B | — | default; proof: `mp4 E2 device proof: keyboard` / `pair_a_sailing` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `pair_a_sailing` @ `9dac583` | accepted (M4-E Option C): game side partly proven, reason §18.1 @ `b1b2bd7` |
| `mp4.minigame.order_up` | Order Up | 2-vs-2 | stick, A | — | default; proof: `mp4 E2 device proof: keyboard` / `order_up` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `order_up` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.dungeon_duos` | Dungeon Duos | 2-vs-2 | stick, A, B, X, Y, L/R analog (clamped strength / 150)† | strength saturates on a full press; also tests the L click (0x140, with A) and the R click (0x20) in some states, not listed (§18.1) | default; proof: `mp4 E2 device proof: keyboard` / `dungeon_duos` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `dungeon_duos` @ `9dac583` | accepted (M4-E Option C): game side partly proven, reason §18.1 @ `b1b2bd7` |
| `mp4.minigame.cheep_cheep_sweep` | Cheep Cheep Sweep | 2-vs-2 | stick, A, B | — | default; proof: `mp4 E2 device proof: keyboard` / `cheep_cheep_sweep` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `cheep_cheep_sweep` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.darts_of_doom` | Darts of Doom | Bowser | A | — | default; proof: `mp4 E2 device proof: keyboard` / `darts_of_doom` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `darts_of_doom` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.fruits_of_doom` | Fruits of Doom | Bowser | stick, A | — | default; proof: `mp4 E2 device proof: keyboard` / `fruits_of_doom` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `fruits_of_doom` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.balloon_of_doom` | Balloon of Doom | Bowser | A, L click | the L click as an alternative to A in one state (§18.1) | default; proof: `mp4 E2 device proof: keyboard` / `balloon_of_doom` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `balloon_of_doom` @ `9dac583` | accepted (M4-E Option C): game side partly proven, reason §18.1 @ `b1b2bd7` |
| `mp4.minigame.chain_chomp_fever` | Chain Chomp Fever | Battle | stick | — | default; proof: `mp4 E2 device proof: keyboard` / `chain_chomp_fever` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `chain_chomp_fever` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.paths_of_peril` | Paths of Peril | Battle | stick | — | default; proof: `mp4 E2 device proof: keyboard` / `paths_of_peril` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `paths_of_peril` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.bowsers_bigger_blast` | Bowser's Bigger Blast | Battle | R pressure (held part-way) and R click† | partial trigger | override: lshift+E = half pressure, E = commit; proof: `mp4 E2 device proof: keyboard` / `bowsers_bigger_blast` @ `9dac583` | default (analog trigger); proof: `mp4 E2 device proof: gamepad` / `bowsers_bigger_blast` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.butterfly_blitz` | Butterfly Blitz | Battle | stick, A, B | — | default; proof: `mp4 E2 device proof: keyboard` / `butterfly_blitz` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `butterfly_blitz` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.mario_speedwagons` | Mario Speedwagons | 4-player | A, R throttle (saturates at clamped 140 / raw 170)† | full press = full throttle | default; proof: `mp4 E2 device proof: keyboard` / `mario_speedwagons` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `mario_speedwagons` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.reversal_of_fortune` | Reversal of Fortune | Fortune | stick, A, R past clamped 10 (raw above 40)† | trigger as threshold; also reacts to L, read not traced (§18.1) | default; proof: `mp4 E2 device proof: keyboard` / `reversal_of_fortune` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `reversal_of_fortune` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.bowser_bop` | Bowser Bop | Item (not party-eligible) | stick, A† | — | default; proof: `mp4 E2 device proof: keyboard` / `bowser_bop` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `bowser_bop` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.mystic_match_em` | Mystic Match 'Em | Item (not party-eligible) | A, stick as D-pad (repeat) | — | default; proof: `mp4 E2 device proof: keyboard` / `mystic_match_em` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `mystic_match_em` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.archaeologuess` | Archaeologuess | Item (not party-eligible) | stick, A | — | default; proof: `mp4 E2 device proof: keyboard` / `archaeologuess` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `archaeologuess` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.goombas_chip_flip` | Goomba's Chip Flip | Item (not party-eligible) | stick, A, L click | the L click as an alternative to A in one state (§18.1) | default; proof: `mp4 E2 device proof: keyboard` / `goombas_chip_flip` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `goombas_chip_flip` @ `9dac583` | not proven: game side partly proven, not L click (§18.1 @ `3cf989f`; PR #91, merge `c8e6ace`) |
| `mp4.minigame.kareening_koopa` | Kareening Koopa | Item (not party-eligible) | stick | — | default; proof: `mp4 E2 device proof: keyboard` / `kareening_koopa` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `kareening_koopa` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.rumble_fishing` | Rumble Fishing | Battle | stick, A | — | default; proof: `mp4 E2 device proof: keyboard` / `rumble_fishing` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `rumble_fishing` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `3cf989f` (PR #91, merge `c8e6ace`) |
| `mp4.minigame.take_a_breather` | Take a Breather | 4-player | L click, R click (pressed in turn)† | the game copies the stick and held buttons every frame but never reads them; its only live input is the L and R clicks (`HuPadBtnDown & 0x60`) expected in turn (§18.1) | default; proof: `mp4 E2 device proof: keyboard` / `take_a_breather` @ `9dac583` | default; proof: `mp4 E2 device proof: gamepad` / `take_a_breather` @ `9dac583` | proven: device @ `9dac583`, game side §18.1 @ `9c51eb0` (rerun at `bda4902`; PR #91, merge `c8e6ace`) |
| `mp4.minigame.bowser_wrestling` | Bowser Wrestling | Story (party-eligible: board 5's Bowser space) | A, L click, R click (L and R in turn)† | the L and R clicks alternate, each expected after the other (§18.1) | default; proof: `mp4 E2 device proof: keyboard` / `bowser_wrestling` @ `248350b` | default; proof: `mp4 E2 device proof: gamepad` / `bowser_wrestling` @ `248350b` | accepted (M4-E Option C): game side partly proven, A proven, L/R clicks not proven, reason §18.1 @ `c5f2e62`; device @ `248350b` |
| `mp4.minigame.panels_of_doom` | Panels of Doom | Story (party-eligible: board 5's Bowser space) | stick, A† | the stick is read as a direction past ±20 (`HuPadStkX`/`HuPadStkY`), in its pick phases (§18.1) | default; proof: `mp4 E2 device proof: keyboard` / `panels_of_doom` @ `248350b` | default; proof: `mp4 E2 device proof: gamepad` / `panels_of_doom` @ `248350b` | proven: device @ `248350b`, game side §18.1 @ `c5f2e62` (rerun at `e7dde31`) |

"default" means the base bindings of `input/profiles/mp4/default.json`. "override" means a `minigames` entry in that profile. `input/tests/test_mp4_profile.cpp` checks that the profile parses, binds no reserved input (Esc, Guide), and that its overrides apply. It also checks that every ID here and in the profile is in the §12 catalog, that this table has exactly the 51 rows (everything outside Extra Room and Story, plus the two M4-U3 Story rows) with the five Item rows marked not party-eligible, and that every Sign-off cell cites §18.1: 43 proven, the 7 accepted and the 1 not proven above.


### 18.1 Game-side proof (M4-07, E2)

**Owner of this subsection:** runtime-gc-wii. It is M4-07's half of the joint E2 sign-off (ruling M4-E); the table above, its input columns and its Sign-off column stay input-mapping's. It sits under its own heading because `input/tests/` reads §18's table up to the next heading.

**Method** (`mpt_dump_lane --boot mp4 --e2`, `tests/dump_lane/README.md` "E2"; rows, injection and verdict in `tests/dump_lane/e2.h`):
- **Test start.** `Mp4Runtime::setTestStart` (MptMp4BootConfig `test_start`, v1.5 test only; Party Board patch 0038, `runtimes/gc_wii/mp4/src/test_start.c`) boots straight into one minigame: bootDll's one-time setup, four human players, `GWSystem.mg_next`, then instDll, which plays it as a board does (no instruction screen on a fresh card). In seated categories (1-vs-3, 2-vs-2, Bowser, Fortune, Item) slot i plays on pad (i + focus) % 4, so every port can take slot 0. It is a determinism input in the state hash and changes nothing on a normal boot: the M1 trail is unchanged (seed 1 `2523e333cd8cc6cc`, seed 2 `5677a93bacfda417` at frame 10,000, checked after every module change on this branch).
- **Runs**, per row and port, from a save state on the minigame's first frame: idle twice (the trails must be equal), then each listed control on the port. Controls come in bursts, 24 frames of every 32 (buttons pulse 4 on, 4 off; the stick holds right, up, left or down; analog pulls hold raw 200, clamped 150), idle for the other 8.
- **The check.** The state hash at the last frame of each idle gap, compared with the baseline's at the same frame while both runs are in the minigame. The gap matters: the hash covers the game's pad arrays, which copy the pad every frame. At the end of the gap they are idle again, so a difference is game state the input changed. Deterministic, no wall clock: frames and hashes only.
- **Null controls.** Some minigames copy the pad into their own objects and stop refreshing the copy in some states, so an input can stay in game memory with no reaction behind it. Each listed control has a null of its input class that no row reads: Z for buttons, the C-stick for the stick, the unlisted trigger for an analog trigger (the C-stick when a row lists both). The null is played on the control's own baseline, and the control counts only where it diverges before its null. In this run no C-stick null ever left its baseline. One trigger null did: in Reversal of Fortune the L-analog null for R left its context baseline at t1375, after R's t799, so R's verdict stands. Z left its baseline in Avalanche (t863, after every proof) and in the two rows below where any extra button changes the game.
- **Context sets** for controls the game only acts on together or after setup (in the cells below): each control in the set is proven leave-one-out, the whole set against the set without it, and its null is played inside the same set.
- **Seats.** In seated categories, controls still unproven with the port in slot 0 are retried with the port in slot 3 (1-vs-3, Bowser, Fortune, Item) or slot 1 (2-vs-2).
- **End.** Every run must end the minigame with no halt or fault within 36,000 frames and run 300 frames more. After it, instDll's history goes to resultDll, which has no party to show here and returns inside its first frame. A minigame that waits for input passes when an injected run ends it. Bowser's Bigger Blast also needs R=128 and R=255 to differ.

**Run.** Every row except five: branch `m4-07-e2-game-side` at `1545b13`, 2026-11-20, Windows 11, `windows-clang-release`, null GX backend, seed 1, Party Board `98ac07f` with patches 0001-0039 (before patch 0040), musyx `86851a3`, aurora `5143394`. Makin' Waves, rerun with the L click: branch `m4-07-s18-input-facts` at `ec46173`, 2026-11-20, same platform, preset, backend and seed, patches 0001-0039. Kareening Koopa, rerun with patch 0040: branch `m4-07-kareening-koopa` at `18f8ea5`, 2026-11-20, same platform, preset, backend and seed, patches 0001-0040. Take a Breather, rerun with the L and R clicks: branch `e2-ruling-c-acceptance` at `bda4902`, 2026-11-20, same platform, preset, backend and seed, patches 0001-0040. Bowser Wrestling and Panels of Doom (added by P1-17): branch `p1-17-party-eligible` at `e7dde31`, 2026-11-25, same platform, preset, backend and seed, patches 0001-0040. Dates are local (UTC-8) dates in ISO 8601 throughout §18.1. `mpt_dump_lane --boot mp4 --e2 --e2-rows <list>` in four shards, Kareening Koopa alone under `--supervise`. "tN" is the first divergence in frames from the minigame's first frame (earliest to latest port where they differ).

**Synthetic checks (CI, no dump).** `tests/unit/test_dump_lane_e2.cpp`: the E2 rows are this table's rows with the controls each "Inputs read" cell lists, injection is idle on every checkpoint (both sticks, both triggers, every button), trigger values reach the thresholds after PADClamp, context plans differ in exactly one control, every null is of its control's class and never an input the row lists, and the verdict counts only divergences inside both minigame windows. `runtimes/gc_wii/sdk/tests/test_pad_vi.cpp`: PADClamp's trigger vector over raw 0..255 and where the game's analog-L/R bit turns on. `runtimes/gc_wii/mp4/tests/test_abi.c`: create refuses an invalid `test_start`.

**What a divergence changes** (`--e2-items`, the state hash's parts at the first divergence; Bob-omb Breakers, port 0, where every control first diverges at t607): each of stick, A, B, X and Y changes both the game's named globals (the `symbols` part) and its heap objects (`memory`), not a pad copy alone, while the C-stick and Z nulls never leave idle. The shared t607 is the first checkpoint after the minigame starts taking input, so every control acts on its first chance. The items are the hash's coarse parts; `--e2-items` names them per run.

**Result: 43 of 51 rows proven** on all four ports with every listed control; **8 are not fully proven**, each with its reason in the table: Mario Medley (L-click, R-click), Long Claw of the Law (A), Makin' Waves (L-click), Pair-a-sailing (B), Dungeon Duos (B, X, Y), Balloon of Doom (L-click), Bowser Wrestling (L-click, R-click) and Goomba's Chip Flip (L-click). The table has 51 rows since P1-17 added Bowser Wrestling and Panels of Doom (below). Against the first sweep (`a5cb11b`, Z as the only null): Bowser Bop is now proven (patch 0039), Kareening Koopa is now proven (patch 0040), Take a Breather is now proven with the inputs it reads (the L and R clicks), and Mario Medley's L/R and Balloon of Doom's L lost their proofs to the button null. **Under M4-E (Option C):** of the 46 party-eligible rows, 39 are fully proven and Mario Medley, Long Claw of the Law, Makin' Waves, Pair-a-sailing, Dungeon Duos, Balloon of Doom and Bowser Wrestling are accepted with documented reasons (below), so all 46 are signed off game-side. Of the 5 Item rows (not party-eligible), 4 are fully proven and Goomba's Chip Flip stays partly proven.

**Accepted under M4-E (Option C).** The ruling (`docs/phases/phase-1.md` §10, M4-E amendment 2026-11-20) accepts a party-eligible row with a documented reason when at least one listed control is proven game-side, the device proof passes and the controls not proven game-side are named here. Each of these seven rows meets it. The unproven controls are the Phase 2 entry item "E2 targeted reruns" (`docs/phases/phase-1.md` §11), which aims scripted reruns at the states named below. Goomba's Chip Flip is an Item row, not party-eligible, so the rule does not apply to it and its row stands as it is.
- **Mario Medley.** Proven: A, B (t639, in the prompt context, ports 0-3). Not proven game-side: L-click, R-click. Reason: the minigame ORs every pressed button into its input word and also counts a press that misses the prompt, so the Z null diverges with L and R at t1311 and no button can stand as a null; a proof has to compare a matching prompt against a non-matching button. Phase 2: §11 E2 targeted reruns.
- **Long Claw of the Law.** Proven: stick, L-analog, R-analog (t511, ports 0-3). Not proven game-side: A. Reason: A grabs only while the claw is idle over a free target, which the turning stick never lines up ({stick, A} context tried). Phase 2: §11 E2 targeted reruns.
- **Makin' Waves.** Proven: stick, A (t447, ports 0-3; A with the port in its second seat). Not proven game-side: L-click. Reason: the L click acts only in mid-air, after A has started a jump (`m417Dll/player.c`). A press of A sets the jump buffer `unk_A8` (:649-650), state 0 starts the jump from it (:659-667), state 2 sets `unk_34` only once the airborne timer `unk_AC` passes 0.06 on the way up (:682-691), and only then is A or the L click taken (:698). The stick plays no part. The {stick, L click} context tried on 2026-11-20 therefore could not reach that state. Phase 2 rerun context: A to jump, then the L click alone while airborne. Phase 2: §11 E2 targeted reruns.
- **Pair-a-sailing.** Proven: stick, A (t479, ports 0-3). Not proven game-side: B. Reason: B lowers the gear `unk_34` (`m430Dll/player.c`), which starts at 1 (:390, :848) and is clamped to 1..3 (:921-930), and B is read only while A is up (an else branch). So B alone does nothing until A has raised the gear; the {stick, B} context tried could not show it. Phase 2 rerun context: {A, B}, A to raise the gear, then B to lower it. Phase 2: §11 E2 targeted reruns.
- **Dungeon Duos.** Proven: stick, A (t351), L-analog, R-analog (t18335), ports 0-3. Not proven game-side: B, X, Y. Reason: they act only in states the team's movement sets up, which the bursts do not reach in either team seat ({stick, B, X, Y} context tried). Phase 2: §11 E2 targeted reruns.
- **Balloon of Doom.** Proven: A (t383, ports 0-3; the minigame waits for input and an A run ends it). Not proven game-side: L-click. Reason: L+A leaves the A trajectory at the same checkpoint and in the same state-hash items as Z+A, so any second button pressed with A changes the result, and L's own effect (an alternative to A in one state) is not separated. Phase 2: §11 E2 targeted reruns.
- **Bowser Wrestling.** Proven: A (t447, ports 0-3; the minigame waits for input and an A run ends it at t1580). Not proven game-side: L-click, R-click. Reason: the minigame takes the L and R clicks in turn (`m457Dll/main.c:192-194`, `prevTrig` at :230-234 and :271-272) only once A has brought the player to its grapple states (`gameState` 1008 and player state 7 to 14, :271). In the {A, L click, R click} context, Z added to A changes the game at the same checkpoint (t447) as L or R does, so any second button pressed with A changes the result and the clicks' own effect is not separated. Phase 2: §11 E2 targeted reruns, with A to reach the grapple, then the L and R clicks in turn without A.

| ID | Game side (M4-07) |
|---|---|
| `mp4.minigame.manta_rings` | **proven, ports 0-3.** stick t447, A t447; ends idle at t3726. |
| `mp4.minigame.slime_time` | **proven, ports 0-3.** A t831; ends idle at t3000. |
| `mp4.minigame.booksquirm` | **proven, ports 0-3.** stick t447; ends idle at t1207. |
| `mp4.minigame.trace_race` | **proven, ports 0-3.** stick t447; ends idle at t2920. |
| `mp4.minigame.mario_medley` | **partly proven.** A, B proven on ports 0-3 (t639) in the prompt context; ends idle at t18907. **L-click, R-click not proven:** they leave the context trajectory at t1311, but so does Z (the button null) at the same checkpoint. The minigame ORs every pressed button into its input word and a press that does not match the prompt counts too, so no button is a null here and L/R cannot be told apart from "any extra button". |
| `mp4.minigame.avalanche` | **proven, ports 0-3.** stick t447, A t447; ends idle at t1450. |
| `mp4.minigame.domination` | **proven, ports 0-3.** A t447; ends idle at t1508. |
| `mp4.minigame.paratrooper_plunge` | **proven, ports 0-3.** stick t927; ends idle at t2880. |
| `mp4.minigame.toads_quick_draw` | **proven, ports 0-3.** stick t639, A t1023; ends idle at t13672. |
| `mp4.minigame.three_throw` | **proven, ports 0-3.** A t511, B t511; ends idle at t2661. |
| `mp4.minigame.photo_finish` | **proven, ports 0-3.** stick t671, A t4255, L-analog t671, R-analog t671; ends idle at t4621. |
| `mp4.minigame.mr_blizzards_brigade` | **proven, ports 0-3.** stick t703; ends idle at t1580. |
| `mp4.minigame.bob_omb_breakers` | **proven, ports 0-3.** stick t607, A t607, B t607, X t607, Y t607; ends idle at t5680. |
| `mp4.minigame.long_claw_of_the_law` | **partly proven.** stick, L-analog, R-analog proven on ports 0-3 (stick t511, L-analog t511, R-analog t511); ends idle at t4581. **A not proven:** A grabs only while the claw is idle over a free target, which the turning stick never lines up (context {stick, A} tried). Needs a scripted approach to a target. |
| `mp4.minigame.stamp_out` | **proven, ports 0-3.** stick t415, A t415; ends idle at t2705. |
| `mp4.minigame.candlelight_flight` | **proven, ports 0-3.** stick t543, A t575; ends idle at t3092 (some controls proven with the port in its second seat). |
| `mp4.minigame.makin_waves` | **partly proven.** stick, A proven on ports 0-3 (t447; A with the port in its second seat); ends idle at t1253. **L-click not proven:** the module's live L read is the digital L click as an alternative to A in one player state (`HuPadBtnDown & 0x140` when `unk_34` is set), and an L click on its own never left idle on any port in either seat (rerun 2026-11-20). **Correction:** PR #91 listed L as proven by an analog pull ("L-analog t863"). That was a false positive. The pull reaches only the `L > 80` read, whose flag nothing reads: its run changed heap memory alone (no game global, no SDK state, `--e2-items`), where every real reaction here (stick, A) changes all three, and the R-analog null, which never sets the flag, never diverged. |
| `mp4.minigame.hide_and_go_boom` | **proven, ports 0-3.** A t511, B t511, X t511, Y t511; ends idle at t1625 (some controls proven with the port in its second seat). |
| `mp4.minigame.tree_stomp` | **proven, ports 0-3.** stick t735, A t735, B t735; ends idle at t3990 (some controls proven with the port in its second seat). |
| `mp4.minigame.fish_n_drips` | **proven, ports 0-3.** A t543, B t543, X t543, Y t543, L-click t543, R-click t543; ends idle at t4645. |
| `mp4.minigame.hop_or_pop` | **proven, ports 0-3.** stick t479, A t479; ends idle at t3661. |
| `mp4.minigame.money_belts` | **proven, ports 0-3.** stick t447; ends idle at t2665. |
| `mp4.minigame.goooooooal` | **proven, ports 0-3.** stick t543, A t511; ends idle at t2708. |
| `mp4.minigame.blame_it_on_the_crane` | **proven, ports 0-3.** A t447, stick t447; ends idle at t4542 (some controls proven with the port in its second seat). |
| `mp4.minigame.the_great_deflate` | **proven, ports 0-3.** A t383; ends idle at t2570. |
| `mp4.minigame.revers_a_bomb` | **proven, ports 0-3.** stick t447, A t479; ends idle at t1788. |
| `mp4.minigame.right_oar_left` | **proven, ports 0-3.** A t575, B t575; ends idle at t18978. |
| `mp4.minigame.cliffhangers` | **proven, ports 0-3.** stick t479, A t479; ends idle at t18926. |
| `mp4.minigame.team_treasure_trek` | **proven, ports 0-3.** stick t575, X t575, Y t575; ends idle at t4575. |
| `mp4.minigame.pair_a_sailing` | **partly proven.** stick, A proven on ports 0-3 (stick t479, A t479); ends idle at t3617. **B not proven** in either seat of the team (slots 0 and 1), alone or with the stick: the module reads B only when A is up (an else branch) and only in a state the bursts do not reach. |
| `mp4.minigame.order_up` | **proven, ports 0-3.** stick t415, A t927; ends idle at t9029. |
| `mp4.minigame.dungeon_duos` | **partly proven.** stick, A, L-analog, R-analog proven on ports 0-3 (stick t351, A t351, L-analog t18335, R-analog t18335); ends idle at t18736. **B, X, Y not proven** in either team seat, alone or with the stick (context {stick, B, X, Y}): they act only in states the bursts do not reach. |
| `mp4.minigame.cheep_cheep_sweep` | **proven, ports 0-3.** stick t479, A t479, B t479; ends idle at t4521. |
| `mp4.minigame.darts_of_doom` | **proven, ports 0-3.** A t383; waits for input (no idle end); an injected run ends it. |
| `mp4.minigame.fruits_of_doom` | **proven, ports 0-3.** stick t415, A t383; waits for input (no idle end); an injected run ends it (waits for A; leave-one-out over {stick, A}). |
| `mp4.minigame.balloon_of_doom` | **partly proven.** A proven on ports 0-3 (t383); the minigame waits for input and an A run ends it. **L-click not proven:** L+A leaves the A trajectory at t383, but Z+A does too, at the same checkpoint and in the same items (game globals): a second button pressed with A changes the result whatever it is, so L's own effect (an alternative to A in one state) is not separated. |
| `mp4.minigame.chain_chomp_fever` | **proven, ports 0-3.** stick t479; ends idle at t1493. |
| `mp4.minigame.paths_of_peril` | **proven, ports 0-3.** stick t447; ends idle at t4492. |
| `mp4.minigame.bowsers_bigger_blast` | **proven, ports 0-3.** R-128 t575-2751, R-click t575-2751, R-255-vs-R-128 t575-2751; ends idle at t15097 (R=128 and R=255 differ (t575 to t2751 by port)). |
| `mp4.minigame.butterfly_blitz` | **proven, ports 0-3.** stick t511, A t511, B t511; ends idle at t4774. |
| `mp4.minigame.mario_speedwagons` | **proven, ports 0-3.** A t735, R-analog t415; ends idle at t9851 (A shifts gear once R has the car past a speed; leave-one-out over {A, R}). |
| `mp4.minigame.reversal_of_fortune` | **proven, ports 0-3.** stick t895, A t255, R-analog t799; waits for input (no idle end); an injected run ends it (plunger: leave-one-out over {stick, A, R}). |
| `mp4.minigame.bowser_bop` | **proven, ports 0-3.** stick t511, A t511; ends idle at t4537 (ends cleanly with patch 0039: the mole process starts over, as on the console). |
| `mp4.minigame.mystic_match_em` | **proven, ports 0-3.** A t1055, stick t1055; ends idle at t23238. |
| `mp4.minigame.archaeologuess` | **proven, ports 0-3.** stick t639, A t5855; ends idle at t7272 (A digs where the stick moved the player; leave-one-out over {stick, A}). |
| `mp4.minigame.goombas_chip_flip` | **partly proven.** stick, A proven on ports 0-3 (stick t1247, A t1247); ends idle at t19612. **L-click not proven:** the module accepts the L click (`HuPadBtnDown & 0x140`: A or the SDK's digital L click, 0x40) only in one state, as an alternative to A, which the bursts did not reach without A. (Its pad-0 L/R reads drive a debug camera.) |
| `mp4.minigame.kareening_koopa` | **proven, ports 0-3.** stick t415; ends idle at t18839 (with patch 0040, which fixes the load fault below; rerun 2026-11-20). |
| `mp4.minigame.rumble_fishing` | **proven, ports 0-3.** stick t415, A t415; ends idle at t2690 (the stick reels once A has hooked a fish; leave-one-out over {stick, A}). |
| `mp4.minigame.take_a_breather` | **proven, ports 0-3.** L-click t415, R-click t415; ends idle at t1170, and each click's run at t1391. Its inputs are the L and R clicks pressed in turn: `m456Dll/main.c` tests `unk_0A & 0x60` (0x40 the L click, 0x20 the R click) against `lbl_1_data_184` = {0x40, 0x20}, flips the expected click after each accepted one, and counts them in `unk_30`. The stick (`unk_06`, `unk_07`) and the held buttons (`unk_08`) are copied every frame but never read, so the row no longer lists the stick and A. Each click alone is a valid first press, so no context set is needed; Z, the null for both, never left idle. Rerun on branch `e2-ruling-c-acceptance` at `bda4902` (2026-11-20, `windows-clang-release`, null GX backend, seed 1, patches 0001-0040). |
| `mp4.minigame.bowser_wrestling` | **partly proven (accepted under Option C, above).** A proven on ports 0-3 (t447); the minigame waits for input, and an A run ends it at t1580, then resultDll with no fault. **L-click, R-click not proven:** in the {A, L click, R click} context the Z null leaves the context baseline at the same checkpoint as L and R (t447), so the clicks' own effect is not separated. Seated as board 5 seats it: the port's player in group 0 against Bowser, a COM, in group 1 (one human seat, so no second seat). Run: branch `p1-17-party-eligible` at `e7dde31`, 2026-11-25, same platform, preset, backend and seed, patches 0001-0040. |
| `mp4.minigame.panels_of_doom` | **proven, ports 0-3.** stick t543, A t511, in the {stick, A} context (the minigame waits for A, and the stick acts only in its later phase); the minigame waits for input, and the context runs end it at t3209, then resultDll with no fault. The C-stick and Z nulls never left the context baseline. Seated as board 5 seats it: the port's player in group 0 against Bowser, a COM, in group 1 (one human seat). Run: branch `p1-17-party-eligible` at `e7dde31`, 2026-11-25, same platform, preset, backend and seed, patches 0001-0040. |

**Facts for the input columns (input-mapping's to change).** Checked against the module's code and the SDK:
- PADClamp maps raw trigger 0..255 to 0 at or below 30, raw - 30 above it and 150 from 180 up (`test_pad_vi.cpp` pins it). Every "/255" threshold in this section is a value the game compares the **clamped 0..150** trigger against, not raw 0..255.
- `HuPadRead` sets its own analog-L/R button bits, `PAD_BUTTON_TRIGGER_L` 0x4000 and `PAD_BUTTON_TRIGGER_R` 0x2000 (`include/game/pad.h`), when `trigger & 0xC0` is nonzero after PADClamp: **clamped 64 and up, raw 94 and up**. "192/255" is wrong: the clamped trigger never reaches 192 (its maximum is 150). **No m4xx module tests a pad word against 0x4000 or 0x2000** (m423 uses those values as collision flags, and the free-play menu reads the bits), so no party minigame reacts to that bit. The 0x40 and 0x20 that minigames test are the digital L and R clicks, which our SDK sets only for a key press or a trigger at 255 (`runtimes/gc_wii/sdk/src/vi_pad.cpp`).
- Makin' Waves: its `L > 80` read sets a flag nothing reads. **L acts through the digital L click**, not the analog trigger and not the analog-L bit: `HuPadBtnDown & 0x140` (A or the L click) in one player state. Goomba's Chip Flip's and Balloon of Doom's L are the same click as an alternative to A, each in one state. (PR #91 said "the analog-L bit" here; that was wrong.)
- Trigger rows audited against their modules (2026-11-20): Photo Finish, Long Claw of the Law, Dungeon Duos, Mario Speedwagons, Reversal of Fortune and Bowser's Bigger Blast read the analog trigger value (`HuPadTrigL`/`HuPadTrigR`), so their analog controls are right; Makin' Waves was the only row whose listed analog L is not what the game reads. Dungeon Duos also tests `0x140` (A or the L click) and `0x20` (the R click) in some states, which its row does not list.
- Reversal of Fortune reacts to L, which its row does not list: an analog-L pull on top of stick, A and R changes game state (t1375 on every port, the L-analog null run). Which L read that is was not traced.

**Which minigames a party can launch (P1-17, confirmed).** Searched in a pristine export of Party Board `98ac07f`: every board file under `src/game/board/` and the board RELs `w01Dll`-`w06Dll`, for each place that sets `GWSystem.mg_next` or launches a minigame overlay.
- **Roulette** (`board/mg_setup.c`): lists the `mgInfoTbl` entries with flag bit 0 clear (:318-322) whose `type` is the turn's `mgType` (:330, ovl 0x29 excluded); `mgType` comes from `GetMGType` or `GetMGTypeTeam` (:446, :546, chosen at :718-720), which return only 0, 1 or 2. 4-player, 1-vs-3 and 2-vs-2 are reachable.
- **Battle space** (`board/battle.c`): its list holds `type == 4` only (:169-170), then `mg_next` (:291) and instDll (:314). Battle is reachable.
- **Bowser space** (`board/bowser.c`): picks one of the three Bowser rows from `mgTbl` (:410), m435-m437, then `mg_next` (:459) and instDll (:461). Bowser is reachable.
- **Fortune space** (`board/fortune.c`): `mg_next = 43` and `DLL_m444dll` (:175-177). Fortune is reachable.
- **Bowser's Gnarly Party** (board 5, `w06Dll/bowser.c`): its own Bowser-space event branches on the landing player (:205-228); a Mega-size player (`BoardPlayerSizeGet == 2`, :224-225) gets `fn_1_A408`, which seats that player as group 0 and the rest as group 1 (:601-611), picks m457 (`mg_next` 0x38) or m458 (0x39) at 50/50 (:577-584) and launches it (`BoardNextOvlSet`, :634). The board's in-progress flag (1, 0), set in `BoardObjectSetup` (`board/main.c:98-100`) and cleared only on leaving the board (:226) or in `BoardSaveInit` (:285), stays set, so both minigames put Bowser, a COM, in the group-1 seat (`m457Dll/main.c:1662`, `m458Dll/main.c:1514`). **Bowser Wrestling and Panels of Doom are reachable** (ruling M4-U3, 2026-11-25, `docs/phases/phase-1.md` §10), though §12 gives them the game's Story type (8).
- **Nothing else:** `board/main.c` only clears `mg_next` (:306, :632) and runs the roulette (:552); `BoardMGCreate` (:1348, from `space.c:671`) starts a board-local event object, not a minigame overlay; `last5.c`, `item.c`, `mushroom.c` and `lottery.c`, the RELs `w01Dll`-`w05Dll` and `w06Dll`'s `main.c`, `mg_coin.c`, `mg_item.c`, `bridge.c` and `fire.c` set no minigame. The tutorial board (`w10Dll`) and the Extra Room board modes (`w20Dll`, `w21Dll`; Mega Board Mayhem and Mini Board Mad Dash) are not regular parties and are out of scope.
- **So:** a regular party can launch the 4-player, 1-vs-3, 2-vs-2, Battle, Bowser and Fortune rows and Bowser Wrestling and Panels of Doom: **46 minigames**. No party path reaches m445-m449 (the Item category: Bowser Bop, Mystic Match 'Em, Archaeologuess, Goomba's Chip Flip, Kareening Koopa), which come only from Mini-game mode's free play (`mgmodedll/free_play.c`) and Story's board clear (`mstoryDll/board_clear.c`, slot 1 the CPU host character), nor The Final Battle or any Extra Room row. The adapter holds the 46 as an explicit list (`mpt::mp4::party_eligible`, `adapters/mp4/src/catalog.cpp`), pinned by `tests/unit/test_mp4_host.cpp`.

**Kareening Koopa's load fault, found and fixed in the port (patch 0040).** Every test start faulted in the minigame's first frame on a null HSF root in `Hu3DModelObjMtxGet`. The test start's seating is not the cause: it seats the two racers as free play does (racer 0 is the group-0 slot, racer 1 the group-1 slot; free play gives slots groups 0 to 3), and Story's board clear only changes slot 1 to a host character. The cause is in the module: it keeps its two racer objects in `(&lbl_1_bss_54)[0]` and `[1]`, the word at 0x54 and the one after it, which on the console is `lbl_1_bss_58` (the decomp names globals by address, and nothing else uses `lbl_1_bss_58`). Our compiler lays these globals out in declaration order, so `[1]` landed on `lbl_1_bss_50`, the course object. The second racer's store overwrote it, and the racer setup read the course's models from a racer (model ids -1). Patch 0040 routes every `(&lbl_1_bss_54)[i]` through `MPT_M449_RACER(i)` on MPT_PC, which picks `lbl_1_bss_54` or `lbl_1_bss_58` by index, as the console's layout does. It is code only. A first version declared a two-element array instead and moved the M1 trail (the state hash names the module's data by symbol, unloaded RELs included), so it was replaced. With 0040 M1 is unchanged (seed 1 `2523e333cd8cc6cc`, seed 2 `5677a93bacfda417`), and the minigame runs to its end on all four ports. It is a port fault in Mini-game mode and Story, not a game bug: no other REL uses the `(&var)[i]` pattern.

**Bowser Bop's fault, found and fixed in the port (patch 0039).** Every run faulted near the 60 s end with "a process coroutine returned". The mole process (`fn_1_1C64`) leaves its loop with `break` once `lbl_1_bss_6DC >= 4` and falls off the end of the function. That happens in every mode that plays Bowser Bop, the console's included, and is harmless there: a process starts through `gclongjmp` with LR set to its entry, so a function that returns re-enters at its start, clears its mole's flags and waits for a pop-up that never comes. Only our coroutines fault on it (`src/libco.c` panics when one returns). On MPT_PC the function now jumps back to its start, as the console's return does. So it is a port fault in Mini-game mode and Story (the test start seats Bowser Bop as free play does), not a game bug. No party, replay or golden plays this code; M1 is unchanged.

**Follow-ups (runtime-gc-wii).**
- The partly proven rows need scripted inputs aimed at their states (a claw over a target, B with A up, Dungeon Duos' carry states, the L-as-A states, Bowser Wrestling's grapple states). For Makin' Waves the state is a jump, not the stick: `player.c:698` takes the L click only once `unk_34` is set, which happens only in mid-air after A has started a jump (:649-691). The {stick, L click} context tried on 2026-11-20 never left the stick-only trajectory on any port, because it never jumps. Its proof needs A to jump, then the L click alone while airborne. For Pair-a-sailing, B lowers a gear that A must first raise (`m430Dll/player.c:921-930`), so its proof needs {A, B}. For Mario Medley's L/R and Balloon of Doom's L, a proof has to compare a matching prompt or state against a non-matching button, since any extra button changes those games.

---

## 19. The game module (P1-15, `runtimes/gc_wii/mp4/`)

**Decomp progress and pins (2026-10-19).** `marioparty4` is fully matching for `GMPE01_00` (245 objects `Matching`, 238 `MatchingFor(USA, PAL)`, none non-matching; `main` at `147b165`). The module is built from **Party Board `98ac07fd1e8f7ab938bbb6fe75789fad99419010`** (tree `a3471c744d132b4625a4869da4e72cf85c5a1d9a`, CC0 1.0), **musyx `86851a3`** and **aurora `5143394`**, all in `decomp.lock` and fetched by `mpt_fetch_sources` into the source cache. Party Board's `extern/aurora` submodule is satisfied by the separate aurora pin. libco is **not** used (see below), so it has no pin.

**Build.** `MPT_BUILD_MP4_GAME_MODULE=ON` (off by default) adds `mp4/` to the Clang project. The pins are copied into the build tree and our patches are applied with `git apply` (Party Board patches `patches/partyboard/0001`–`0014`, aurora patches `gx/patches/aurora/0001`–`0016`). Game code builds with `TARGET_PC MPT_PC NON_MATCHING OPTIMIZED_TEXTURE_LOADING BYTESWAPPING TARGET_DOL VERSION=0`. `VERSION=0` is `GMPE01_00`; Party Board itself builds `VERSION=1`. `MPT_PC` is our target macro, used wherever Party Board's `TARGET_PC` code assumes its own port layer. The output is `mp4_game.dll` (`.so` on Linux), which exports **only** `mpt_mp4_get_api`. Patch 0014 turns off Party Board's `SHARED_SYM` exports. Without a dump everything compiles and links, so this is the compile-only build for CI.

**Recipe** (`mp4/recipe/`, host side, `mpt::mp4::make_recipe`). It pins the three sources, extracts the disc, checks for clang-cl (clang on Linux), cmake and ninja, and records the Clang version and target in the build key. It then configures `runtimes/gc_wii` as a Clang project with the resolved checkouts, builds `mp4_game`, and installs it as `<build>/module/mp4_game.dll`, with the extracted disc in `<build>/disc/`. It refuses any revision other than `GMPE01_00`. The shell registration and the GX backend/Dawn arguments belong to P1-16.

**Required pieces.**
1. **REL symbol isolation** (`tools/MptMp4Rel.cmake`, `tools/isolate_rel.cmake`). Each of the 91 RELs is its own object library. Its defined external symbols are renamed to `mpt_rel_<REL>__<sym>` with `llvm-objcopy --redefine-syms` and archived. An info record gives the REL's `ObjectSetup` and data range. Every REL is linked into the one module.
2. **Pristine `.data` restore.** A forced-include header puts each REL's writable data in its own section: `.data$mptrel$<REL>$b` between `$a`/`$c` markers on COFF, and `mptrel_<REL>` with `__start_`/`__stop_` on ELF. `-fno-zero-initialized-in-bss` keeps `.bss` inside that range. `rel_loader.c` copies every range at init and restores it on each `omDLLLink` (patch 0003 replaces the per-REL DLL loader). That gives fresh `.data` and zeroed `.bss`, as `OSLink` does. The game core (DOL, msm, port files) gets one range of its own (`.data$mptgame$b`). The REL ranges and the game-core range are registered as SDK state regions. **Unit check:** `mp4_rel_isolation` uses two synthetic RELs with the same names and checks independent resolution, intra-REL references, restore and zeroing.
3. **DOL data reader** (`src/port.c`). It uses Party Board's `dolassets` address table, reduced to the `GMPE01_00` column, and maps each blob through `main.dol`'s section table instead of Party Board's fixed shift. Attach fails if any blob falls outside a section. The disc-error screens use explicit indices (patch 0006), because Party Board's `dolassets.h` maps `retryerror_en` to the French entry and has no `wrongdisc_en`. The boot logo is still read from `dll/bootDll.rel` through the DVD layer.
4. **64-bit HSF conversion** is Party Board's `byteswap.cpp`, unchanged. **Pointer/integer cast review:** a scan of every game object with clang's cast warnings (`-Wpointer-to-int-cast`, `-Wint-to-pointer-cast` and the void variants) found that `msm` did pointer arithmetic through `u32` (patch 0013 changes it to `uintptr_t`). It also found that `THPSimple.c` passed AI DMA buffers as `u32`, because aurora declares 32-bit addresses (patch 0008 adds bridges to the SDK's `uintptr_t` calls). The remaining casts are harmless: 32-bit offset arithmetic in `msmmem.c`, an ARAM address round trip in `msmsys.c`, and an int kept in `user_data` in `w04Dll`.
5. **Constant heap-header caller address.** Party Board already passes `0` (`HuMemDirectMalloc`/`Free` under `TARGET_PC`) or `FAKE_RETADDR` (`process.c`). No other `HuMemMemoryAlloc` caller exists.
6. **`ztardll` low-memory read.** `*(u32*)0x800000F8` (the bus clock) becomes `OS_BUS_CLOCK` (patch 0012). The read is in a `!VERSION_ENG` branch, so `GMPE01_00` never runs it.
7. **C ABI** (`include/mpt/mp4/game_abi.h`, **v1.0**: architect approved the v1 draft, and the seven required changes from the ADR 0008 ABI review are applied). One export returns a versioned function table:
   - create/destroy, step, frame, fault and frame rate;
   - state hash and save/load state;
   - save media and its generation;
   - video (RGBA8), audio (s16 stereo), rumble and OSReport text;
   - raw party reads (overlay, board, turn, max turns, and per player the character, CPU flag, coins, stars and space).

   Every struct carries `struct_size`, checked against named v1.0 sizes. Fields past the caller's size default, and frozen structs are size-asserted. The input frame mirrors `mpt::InputFrame` byte for byte, and sticks fold -32768 to -32767. The module owns all memory it returns, which stays valid until the next `step`, `load_state` or `destroy`; `audio()` returns the last step's samples on every call.

   **Gameplay boot flags** (skip boot sequence, unlock minigames, unlock Bowser's Gnarly Party) are determinism inputs. They are hashed and saved with the state. **The fixed Phase 1 set is `MPT_MP4_PHASE1_GAMEPLAY_FLAGS` = none (the console's behaviour), which `Mp4Runtime` always passes.** They are never user settings; later the unlocks become ruleset rules. Debug and presentation settings (copy trap, video backend, scale, audio output, cache dir, arena base) never reach the hash.

   **Test coverage (qa #40):** CI checks the ABI's shape (versioning, sizes, argument handling) through `mp4_game_abi`. The **stick fold** (-32768 to -32767) and the **gameplay flags in the state hash** only run when a game boots, so they are exercised **on the dump lane only**.

   **Save states:** the SDK state (arena, card image and generation, registered regions), then a trailer with the full build ID, the gameplay flags, the soft-reset frame and generation bases, the module status and fault text, the GX object IDs, and a layout version (`state_format()`, 2). `load_state` rejects another build or layout. A state is valid for the same build and platform at the same arena and module base.

   **`OSResetSystem` is a deterministic soft reset inside the module.** The game core's data returns to its load-time image, MusyX and the renderer reset, and a new SDK boots on the same card from the same seed. Frame numbers and the card generation continue. Halts and faults are terminal module status, kept in the state, for ADR 0011's `status()`.

   **Party reads** (`MptMp4Party::flags`, raw for the adapter's `PartyPhase`). `PARTY_MODE`, `IN_SETUP` and `IN_PROGRESS` are **confirmed on the dump lane** (P1-17, M3 run below). `TURNS_OVER` and `RESULTS_FINAL` need a full party and are still provisional:
   - `PARTY_MODE` is `GWSystem.party`.
   - `IN_SETUP`: party mode, and the running overlay is `mentDll`, the party menu that calls `BoardPartyConfigSet`.
   - `IN_PROGRESS`: party mode, and a board overlay (`wNN`) is in the overlay history (`omOvlHisGet`). That covers the board's turns and the minigames it calls.
   - `TURNS_OVER`: party mode, and `GWSystem.turn > GWSystem.max_turn` (`BoardTurnNext` ends the party there).
   - `RESULTS_FINAL`: turns over, and the board has left the overlay history.

   `resultDll` is the minigame results screen (`instDll` switches to it), not the party's. `party()` only reads.

   `mp4_game_abi` loads the module and checks versioning and argument handling.

   **Build ID** (`mp4/build_id.cmake`, ADR 0009 amendment after P1-16). One fingerprint, `mp4-` plus 16 hex digits of a SHA-256. It is used as `MPT_MP4_BUILD_ID` (the save-state trailer, and the replay and trail identity) and as the recipe's `runtime_build_version` (the cache key). It covers, in groups:
   - `tree`: the module build's files in `runtimes/gc_wii`, by content. For `sdk`, `gx`, `musyx` and `mp4` that is the `CMakeLists.txt`, every top-level `*.cmake`, the locks, `src`, `include` and `mp4/tools`. The top-level `*.cmake` include musyx's link-check script, which only tests run; that can only make the id change more often. Test directories, docs and the host side are left out.
   - `core`: `core/include`, by content.
   - `patches`: the Party Board, aurora and musyx sets, in apply order.
   - `sources`: every lock entry (name, commit, tree) with its state. A pin is `pinned` or `unpinned:<fingerprint>` (from asset-import's resolver, passed by the recipe as `MPT_MP4_SOURCE_STATES`). Dawn's entries are `locked`. A developer build outside the recipe records `unverified`.
   - `toolchain`: compiler ID, version and target (from `clang --version`), build type, CMake version, and the ADR 0008 §4 flags (`sdk/fp_flags.cmake`, shared with the SDK).
   - `constants`: the GX backend, the recipe's module-build arguments, the main-RAM size, the fixed bases and their spacing, and the Phase 1 gameplay flags.
     - The arguments go in by a path-independent identity: whether a Dawn prefix is given (Dawn's pin is `gx/dawn.lock`), and for vcpkg's tree the triplet plus `vcpkg.json` and `vcpkg-configuration.json`, which hold the packages and the registry baseline.
     - The absolute paths are recorded only in the host's `mp4_recipe_build_id.txt`, so two developers building the same commit get the same id (#55 review).
   - `layout`: `kStateFormat` and the ABI version.

   Every input is a configure dependency, so a content change re-runs the computation. The host build computes the same id from the same inputs, with every pin taken as pinned. The recipe's install step fails a pinned build whose module reports another id, so the module, the cache and save states always agree. An unpinned build keeps its own id: its fingerprints are in the module's id and the cache key, not the host's. The module build writes `mp4_game_build_id.txt`, which lists every component with its hash, the repository commit and whether the tree was dirty (those two are not in the id). The recipe installs it next to the module. `mp4_build_id_groups` (host tests) copies the input set, checks that the copy gives the tree's id, and checks that an edit in each group changes it.

**Warnings and link checks (qa #40):**
- Party Board's sources build with warnings off. Our own module code (`src/`, the generated REL records and table, `tests/`) builds with the GX layer's warning set as errors, and only Party Board's and aurora's headers are SYSTEM includes.
- `mp4_no_host_math` (the SDK's `check_no_host_math.cmake`) and `mp4_no_host_services` (musyx's `check_no_host_services.cmake`) run over every DOL object and isolated REL. The object list is passed through a file, because it is too long for a Windows command line.
- The first run of `mp4_no_host_math` found the game's C code calling the host's double `sin`, `cos` and `atan2` (in about 110 objects) and `asinf`/`atanf`.
  - Game C code is now force-included with `mpt_gc/libm_redirect.h`, as MusyX already was.
  - The SDK gained `mpt_atan2` (double), `mpt_asinf` and `mpt_atanf`, with accuracy checks and a golden.
- `mp4_rel_isolation` and `mp4_copy_trap` (`tests/standalone`) need no Party Board checkout, so every Clang build with tests runs them, including public CI.

**Also in this work.**
- **msm byte order** (patch 0015, `src/msm_swap.c`). Every structure msm reads raw from its `.msm` and `.pdt` files is converted to host order right after its read:
  - the header, the info block, the aux parameters and the group info;
  - each group's data head, at every load;
  - the music and SE tables;
  - the stream header and the stream pack list;
  - each pack, converted once even when streams share it, with the second stream entry only for stereo packs;
  - the ADPCM coefficients.

  The project, pool, sample directory, songs and samples stay big-endian, because MusyX's PC loader reads them as big-endian. The layouts are asserted. `mp4_msm_swap` checks every field against synthetic big-endian records written from the structure definitions. **msmSysInit now gets valid sizes** for everything it reads (header, info, group info, aux parameters, music and SE tables, stream tables). Nothing has run on real files yet.
- **Port layer** (`src/port.c`, `src/sdk_bridge.c`, patches 0010–0011). Party Board's settings and version queries return fixed `GMPE01_00` answers. `main.c` is the console loop, entered as `mpt_mp4_game_main` with no aurora frame calls, SDL events, UI or frame limiter. `card.c` calls the console `CARDInit`.
- **Coroutines.** libco's API on the SDK's own context switch (`mpt_gc/coro.h`, `src/libco.c`). Process stacks come from the process heap, as on the console (patch 0002, twice the console size). Each stack has a record and a 256-byte canary at its low end. A smashed canary or a returning process is an `OSPanic`.
- **SDK additions:**
  - `OSStopwatch*`, which read `OSGetTime` and so cost one quantum each, as on the console;
  - `OSDumpHeap`, `OSGetResetButtonState`, and `OSCachedToUncached`/`OSUncachedToCached` (identity);
  - `__OSCurrHeap`, the public SDK name, replacing `__OSCurrentHeap`.
- **Layout contract** (`sdk/include/mpt_gc/layout_contract.h`). A record-layout comparison against the aurora headers found two real mismatches. aurora's `PADStatus` is 16 bytes (`extButton`), so `PADRead` wrote pads 2–4 at the wrong stride. aurora's `ARQRequest` has 32-bit `source`/`dest`, so `ARQPostRequest` wrote 16 bytes past a 40-byte request. Both are fixed in the SDK. Every size and offset of `PADStatus`, `DVDCommandBlock`, `DVDFileInfo`, `CARDFileInfo`, `CARDStat`, the `ARQRequest` prefix, `OSCalendarTime`, `OSStopwatch` and `OSThreadQueue` is now asserted on both sides: `sdk/src/layout.cpp` against our headers, and `mp4/src/layout_check.c` against the game's. A signature comparison of the 236 functions declared by both found only `CARDInit` (aurora has two arguments; bridged) and the AI DMA calls above with ABI-relevant differences. The rest are `const`, `BOOL`/`int` and typedef spellings.
- **Copy-destination trap** (`src/copy_trap.cpp`, aurora patch 0016, boot flag `MPT_MP4_BOOT_DEBUG_COPY_TRAP`):
  - While a frame step runs, whole pages inside each `GXCopyTex` destination are protected.
  - A CPU read faults the step with the overlay's name, unless the overlay is on the allow-list. The allow-list is `m415Dll`: Stamp Out! memcpys its shadow map into a texture, and on PC it copies zeros. Party Board does not branch this code.
  - A CPU write releases the range, because the heap or the game reused it.
  - Pages are unprotected between steps, so save states and hashes never fault.
  - `mp4_copy_trap` drives the real `GXCopyTex` on the null renderer.

**Merge condition (a): met at build level.** `msm` compiles and links against musyx `86851a3` with `MUSY_VERSION` 2.0.0 (patches 0001, 0007, 0009). It needed:
- the msm headers' declarations;
- `SND_HOOKS` with a `size_t` malloc;
- `msmSysLoadGroup` without the console's ignored third argument;
- `msmSysCheckInit` returning `sndIsInstalled()`, the r3 value `sreset.c` tests.

**`MPT_MUSYX_VERSION` stays 2.0.0.** msm links against 2.0.0 and has no `MUSY_VERSION` checks. Runtime correctness is open; see below.

**Determinism notes.**
- `m404` has a Party Board TODO, "do we need to byteswap this?", at a 16-bit read next to its `DebugBreak`. Patch 0004 removes the `DebugBreak` but does not answer the question. The dump lane must check the value (Trace Race).
- `sreset.c`'s reset path spins on `VIGetRetraceCount` with a 1,349,800-iteration cap before `OSResetSystem`. The cap bounds it, and the SDK faults on reset.
- `HuTHP3DCreateVol` returned an undefined value on an early exit (patch 0008 returns -1). Nothing calls it.

**Open before M1 (boots headless on the dump lane).** These are listed in the P1-15 report and carried by P1-16:
- ~~msm's file structures are big-endian~~: converted (patch 0015, above).
- Process coroutine stacks are game-sized (4–32 KiB). SDK interrupts that fall due at a call boundary inside a process, including the MusyX render on the AI tick, run on that stack. The stack high-water mark must be measured on the dump lane. If it is too high, the SDK should run due interrupts on its interrupt coroutine from a boundary instead.
- The GX front end's state (`g_gxState`) is not in the save state. Load restores only the object ID counters and resets the renderer caches. The front end's game-visible state outside it (the draw-sync callback and token) is now saved and hashed (P1-17, below).
- MP4's I/O callback mode (P1-16 audit), the fixed module and arena bases, and the hash manifest. v1 covers the SDK, MusyX, the `GW*` blocks, `rnd_seed` and the current overlay.
- **Linux:** the module has not been built on Linux here, and the ELF section markers and `__start_`/`__stop_` bounds are untested.
- A soft reset re-creates the SDK. With `arena_base` 0 its arena can move, and states saved before the reset then no longer load; P1-16's fixed arena base removes this. The GX front end's globals are not reset at a soft reset. The game's own `GXInit` at boot re-initializes them; the dump lane should confirm nothing stale is left.
- The party flags above came from the decomp's code paths; three of them are now confirmed on a real party (P1-17).

---

## 20. The host runtime (P1-16, `runtimes/gc_wii/mp4/host/`, `adapters/mp4/`)

**Mp4Runtime** is host side, built with the tree's compiler, and drives the module only through `game_abi.h`.
- `boot()` loads `<build_dir>/module/mp4_game.dll` (`.so`) and creates the game on `<build_dir>/disc`. It uses the fixed arena base and the Phase 1 gameplay flags (none).
- `status()` (ADR 0011) is the module's step result and fault text, which the module keeps in its save state.
- **ABI minor versions** (append-only, `game_abi.h`):
  - v1.1 appends `status`. The loader requires it (`mpt_mp4_api_has_v1_1`), and `mp4_game_abi` checks that a 160-byte v1.0 table is rejected.
  - v1.2 appends `debug_perturb`, a test-only hook for the hash coverage proof (accepted in PR #48). It acts only on a game created with the unhashed boot flag `MPT_MP4_BOOT_DEBUG_HOOKS`; without it, it returns `MPT_MP4_ERR_ARGUMENT` and changes nothing. `Mp4Runtime::setDebugHooks` sets the flag (the dump lane's `--perturb` does), and `debugPerturb` uses the entry only when the table is at least 176 bytes. A dump-lane run without `--perturb` checks on the real game that the module refuses.
  - v1.3 appends `debug_deep_hash`, the deep hash of ADR 0008 §9: P1-16's raw-memory hash, kept for bisecting desyncs on one machine and build and as the E4 coverage oracle (amendment after P1-16 hash coverage). Same flag and refusal as `debug_perturb`: without `MPT_MP4_BOOT_DEBUG_HOOKS` it returns `MPT_MP4_ERR_ARGUMENT` and computes nothing. `Mp4Runtime::debugDeepHash` uses it only when the table is at least 192 bytes. v1.3 also appends `debug_hash_items`, which lists every part of `state_hash` with its digest (below), and the perturbation target `MPT_MP4_PERTURB_SAMPLE`. The hooks-off dump-lane check covers both entries.
  - v1.4 appends `memory_use` (M4-01), which reports the arena use between steps: OSAlloc's allocated blocks, each HuMem heap's size and allocated bytes, and ARAM in use. Like `party`, it only reads, and it needs no debug hooks. `Mp4Runtime::memoryUse` uses it only when the table is at least 200 bytes. The dump lane's `--party` samples it at every checkpoint for the peak arena use.
- `SaveState::format_version` is the module's `state_format()`.
- If the module was built with the null GX backend, `create` returns `MPT_MP4_ERR_BACKEND` for a GPU request and `boot()` retries with null video. The host keys on that status code, never on the error text (#49 review).

**Mp4Adapter** (`adapters/mp4`, P1-17) accepts only an empty ruleset, and maps the raw party read to a `PartySnapshot` (`snapshot_of`, unit-tested without a dump):
- **Catalog.** §12, from `src/catalog.cpp`: 8 boards, 61 minigames and 8 characters, with their display names.
- **Board.** `GWSystem.board` (`BOARD_ID_*`: 0–5 the party boards, 6 the tutorial, which is not catalogued, 7 and 8 the Extra Room boards). It is set once a board has run, so not during setup, when the field already holds a value.
- **Characters.** `GWPlayerCfg.character` in the game's order: Mario, Luigi, Peach, Yoshi, Wario, Donkey Kong, Daisy, Waluigi.
- **Phase.** `phase_of` is the raw classifier, which calls any `m4NN` overlay a minigame. In the minigame phase, `current_minigame` is the running overlay's catalog entry. An `m4NN` overlay with no row (m452, m454) counts as the board phase, so `current_minigame` is set exactly in the minigame phase (ADR 0005). This replaces the earlier board-phase demotion.
- **Turn.** The game moves `GWSystem.turn` one past `max_turn` when the last turn ends. The snapshot keeps it at `max_turns`.

The shell registration (`mpt_register_mp4`) gives `--dump` the asset-import recipe, using this tree's `runtimes/gc_wii`, Clang, Ninja, GX backend and packages.

**Fixed bases** (ADR 0008 §11; `mp4/layout.cmake`, the same on both OSes):

| What | Address | How |
|---|---|---|
| Game module image base | `0x5B979E3C0000` | **Windows:** `/BASE` with `/DYNAMICBASE:NO /FIXED`, so the image has no relocations and loads there or not at all (checked: `IMAGE_FILE_RELOCS_STRIPPED`). **Linux:** lld `--image-base`; glibc asks the kernel for that address. Both: no identical-code folding and no incremental link (`/OPT:NOICF /INCREMENTAL:NO`, `--icf=none`), so every function keeps its own start. The base (ADR 0008, "Module words: a safe rule must not depend on layout", 2026-11-07) replaced `0x500000000000`: its upper half `0x5B97` and its lower half `0x9E3C0000` make an integer pair that reads as a module address improbable. The image must end at or below the next 4 GiB boundary, `0x5B9800000000`. |
| SDK arena | `0x400000000000` | Reserved by the SDK at `create`, which fails if the address is unavailable. |

The loader refuses a module that landed anywhere else, or whose image (`SizeOfImage` on Windows, the highest `PT_LOAD` segment on Linux, read from the loaded headers) ends above the 4 GiB boundary after its base, and unloads it again on that path, as on a missing export or a pre-v1.1 table. `mp4_module_loader` checks both with stubs: one linked at the old base and one linked 64 KiB below a 4 GiB boundary with a 256 KiB image. `mp4_game_abi` checks the base. `mpt_mp4_fixed_base_link(<target> [BASE <address>])` in `layout.cmake` applies these link flags to any shared library, so a stub module can test the loader without game sources.

**Mandatory ASLR.** A `/DYNAMICBASE:NO /FIXED` image has no relocations, so it cannot load when Windows Exploit protection forces randomization for images ("Mandatory ASLR"), system-wide or for the executable. The loader then reports that the fixed base is in use "or Mandatory ASLR is forced"; the setting must be off for the shell and the dump lane. **Linux has not been run here.** Fresh-process save/load is a Phase 2 item (M4-D, M4-I); the in-process E5 check is in §11, "E5 in process (M4-16)".

**I/O callback mode: before return** (`SdkConfig::io_callbacks_before_return = true`). The audit:

| Code | Wait | SDK call in the loop? |
|---|---|---|
| `game/dvd.c` `HuDvdDataReadWait` | `while(!CallBackStatus) HuDvdErrorWatch();` | yes (`DVDGetDriveStatus`) |
| `game/data.c` `HuDataDVDdirDirectRead` | `while(DVDGetCommandBlockStatus(...))` | yes |
| `game/data.c`, `game/armem.c` (four sites) | `while (HuARDMACheck());` straight after `ARQPostRequest` | **no**: it only reads `arqCnt` |
| `game/card.c` | `do CARDProbeEx(...) while (BUSY)` | yes |
| `msm/msmstream.c` | no waits; completion chains through `msmStreamDvdCallback` | n/a |

The `armem.c` loops would never see their callback under the default mode, so MP4 runs callbacks before return. Ordering was checked:
- Every caller sets its counters and busy/status fields (`arqCnt++`, `CallBackStatus = 0`, `readBusyF`, `status`) **before** the call.
- The stream callbacks' nested read (`msmStreamDvdCallback` case 4) touches nothing that `msmStreamDvdCallback2` reads.
- Stereo streams complete left then right, as on the console.

**Process stacks.**
- **Size** (Party Board patch 0002). Twice the console's size for 64-bit frames, plus a fixed 16 KiB for the host code under game calls (GX on aurora, the C runtime). That host depth does not scale with the console's stack. The patch hash is in the build ID, so the changed heap layout gets a new one.
- **High water.** `src/libco.c` paints each stack at creation. It measures the stack when the process ends, and also samples it when the process yields (1 switch in 256), which covers long-lived processes. OSReport prints each new peak with the process's entry address, which is stable because the module sits at a fixed base. The peak and the switch counter are host-side (`src/libco_stats.c`, outside the game data range), so they are neither saved nor hashed: the peak depends on how much host stack the code under game calls uses, which differs by compiler and platform (#49 review).
- **What drove the peak.** Before the headroom, `HuWinProc` (window.c; 0x1000 on the console, 8 KiB here) peaked at 7816 of 7904 bytes (98.8%). Moving interrupts off the stack (below) did not change that, so the depth comes from its own calls into GX and the CRT. With the headroom it peaks at 7720 of 24,288 bytes (31.7%). The largest other peak in the boot window is 15.4% of a 48 KiB stack.
- **Interrupts.** Due interrupts at an SDK call boundary used to run on whatever stack was current, which could be a game process's coroutine stack. They now run on the SDK's interrupt coroutine and switch straight back (`runDueOffStack`). The same events run in the same order, and a handler still sees the interrupted thread as current. A synthetic SDK test checks both.
- **Deleted stacks are scrubbed.** `co_delete` repaints a stack before process.c frees it. Host frames leave per-process values on a stack (stack cookies, host heap pointers), and the game may hand the memory out again.

**Dump lane.** `mpt_dump_lane --boot mp4 [--frames N] [--every N] [--seed S] [--perturb]` is the M1 check: it boots headless with no input, prints the state hash every N frames and the game's OSReport text, and fails if the runtime halts or faults. It also prints the mean wall time of a `stateHash()` call. That timing is a harness measurement; nothing in the runtime reads a clock.
- `--perturb` runs the coverage checks below at every checkpoint, and fails on any miss.
- `--press button@frame[+frames][,...]` holds buttons on player 1 (connected whenever a script is given; 6 frames by default). `--capture DIR [--capture-every N]` writes checkpoint frames as PNGs. Captures are the user's own game output: they go only to the directory given, never into the repository.
- The dump comes from `MPT_DUMP_DIR`. It is identified and built through asset-import as `--dump` does, into `MPT_BUILD_CACHE` (default the shell's build cache, `<user dir>/cache`; never inside `MPT_DUMP_DIR`).
- `MPT_SOURCE_CACHE` reuses pinned checkouts instead of fetching them.
- **Dev-environment quirk.** Processes started from the Claude desktop app see a virtualised `%APPDATA%` (under `%LOCALAPPDATA%\Packages\<app>\LocalCache\Roaming`), so caches built there are not the ones the real launcher uses. The first launch outside it rebuilds into the real `%APPDATA%` cache. No code change is needed.

**Memory (ADR 0008 amendment, PRs #44 and #46): 112 MiB of emulated main RAM.**
- **What the space holds.** Party Board's `TARGET_PC` multiplies the console's game heaps by 4 for 64-bit pointers and the converted HSF structures (`malloc.c`). With HEAP_MUSIC grown by 16 MB (Party Board patch 0018, below), the heaps are 9 + 21 + 42 + 22 MiB. `HuMemInitAll` gives what OSAlloc's arena has left to its last heap. That is about 16 MiB at 112 MiB, and the console's 24 MiB arena left about 4.5 MiB the same way.
- **What it is not.** `main_ram_bytes` is a build constant in `layout.cmake`, part of the build ID, and identical on every platform. It is never a setting. ARAM (16 MB) and the stack region keep their fixed sizes.
- **Save states.** They record the arena layout (main RAM, ARAM, total), and `load_state` rejects a mismatch.
- **Hardware getters.** `OSGetPhysicalMemSize` and `OSGetConsoleSimulatedMemSize` still answer 24 MiB. `init.c` only prints them; its `meminfo.bin` path is not on the disc.
- **Failures.** An OSAlloc failure is a deterministic fault (`SdkConfig::fault_on_alloc_failure`).
- **Measured (M4-01).** The sizing rule is the smallest multiple of 8 MiB that covers the heaps plus the SDK's fixed allocations, with at least 10% margin. The seed 1 party in §21 (250,800 frames, null backend, music on) measured, at checkpoints every 60 frames (ABI v1.4 `memory_use`):
  - OSAlloc: 110.82 of 112 MiB allocated from frame 60 on. `HuMemInitAll` hands the five heaps everything, and the SDK's own blocks are 1.00 MiB.
  - Peak use inside each heap: SYSTEM 2.36 of 9 MiB (26%), MUSIC 19.75 of 21 MiB (94%, from frame 60), DATA 19.09 of 42 MiB (45%), DVD 4.99 of 22 MiB (23%), MISC 0 of 15.82 MiB (the remainder heap is never used).
  - ARAM in use: 0.02 MiB (MusyX keeps its samples in msm's heap on PC, patch 0018).
  - The heaps' peaks add up to 46.2 MiB. Every heap except MUSIC has at least half its size free, and MUSIC's 94% is msm's fixed heap, allocated at boot. So the sizes stand; any reduction would shrink DATA, DVD or MISC, which needs a Party Board change to `malloc.c`, and is left for the M5 performance work.
  - Save and load at the end of the party: a 148,974,061-byte state, saved in 178–303 ms and loaded in 13–32 ms (two runs).

**MusyX's PC sample store (Party Board patch 0018).** MusyX's PC renderer keeps every resident sample in msm's heap (through `salMalloc`, `aramStoreData`), where the console kept them in the 16 MB ARAM. It also keeps decoded copies of each pushed group's project, pool and 64-bit sample directory there. The first boots failed `sndPushGroup` with "pool out of memory". Patch 0018 grows HEAP_MUSIC by 16 MB and makes msm's heap three times the console's size plus 16 MB.

**MusyX patch 0001** (`runtimes/gc_wii/musyx/patches/`, applied to a build-tree copy of the pin, CC0, ADR 0009 section 5; approved in PR #46):
- **Purpose.** A group push succeeds when the group names samples that neither its own sample directory nor a resident group has.
- **Console behaviour it restores.** On Dolphin, `sndPushGroup` → `InsertSamples` → `ScanIDList` ignores `dataAddSampleReference` returning FALSE, so such samples are skipped. The PC push refused the whole group. MP4 relies on the console behaviour: the first clean-memory boot found group 93 naming 51 such samples, which made `GroupSet` fail with -32.
- **What it does not change.** Only that case is skipped. A sample that is present but fails to upload still fails the push.
- **Test.** The synthetic test in `musyx/tests` pushes a group naming a missing sample and checks that the group pushes and its present sample plays. It also checks that a failed upload still fails the push. Without the patch the first check fails.
- **Build.** The patch hashes are in the module's build ID. Configure fails if a patch does not apply, as it does for aurora's and Party Board's.
- **Upstream.** To be offered to AxioDL/musyx.

**State hash (ADR 0008 §8 and its amendment after P1-16 hash coverage).** `state_hash` is the cross-platform hash: trails, replays, the conformance suite, E3 and netplay use it. P1-16's raw-memory hash is kept as the **deep hash** (ABI v1.3 `debug_deep_hash`, debug hooks only), valid for one build on one platform. `state_hash` is a `StateHasher` over the digests of four parts, in this order:
- **`fields`.** The gameplay flags, the frame and generation bases, resets and status.
- **`sdk`.** The SDK state by value: scheduler, events, queues, alarms, mutexes, the heap lists and the attached sound engine. Pointers into the arena are offsets. Pointers into the module and function pointers are **stable IDs** from the module's symbol table, through `SdkConfig::pointer_id`. This replaces the presence sentinel, as ADR 0008's P1-13 amendment point 5 asked. A pointer the table does not know keeps the sentinel.
- **No raw `globals` part.** GWPlayerCfg, GWPlayer, GWSystem and GWGameStat are bitfield-heavy, and the MSVC and Itanium ABIs may lay out and pad bitfields differently. The symbol pass covers them as globals, and the deep hash still hashes them raw (#55 review). If Linux shows a layout difference there, they get typed entries.
- **`symbols`: globals by symbol.**
  - **Where the symbols come from.** The module reads its own symbol table from its image file at the first `create` (`src/module_symbols.cpp`, the small library `mpt_mp4_module_symbols`, which `mp4_module_symbols` tests on its own test image in every Clang build, public CI included): on Windows the COFF table that lld-link writes with `/DEBUG:SYMTAB`, on Linux `.symtab`. Both list static symbols as well as external ones, under their source names. The module also links with `/OPT:NOICF`, so no two functions share an address.
  - **Which symbols.** Every symbol inside the game core's data range or a REL's, under the name `<range>:<name>`, where the range is `core` or the REL's name.
  - **Order.** The symbols are visited in name order, so the section layout doesn't matter.
  - **Extent.** A symbol's bytes run to the next symbol. They are canonicalized as words from the symbol's start (see below), and trailing zero bytes are dropped. Alignment padding is therefore never counted: it is always zero, because these ranges are restored from their load-time image. Bytes of a range before its first symbol count as `<range>:<lead>`.
  - **Duplicates.** Static symbols with the same name in the same range are combined without regard to order, because the two linkers need not order archive members the same way.
  - **Host objects.** `create` fails if a C++-mangled symbol sits in a game data range (the `unordered_set` lesson). None does today.
- **`memory`: heap blocks and RAM.** This part walks the same ranges as the deep hash: each allocated OSAlloc block, a HuMem heap's allocated blocks (headers as offsets, `retaddr` left out, stacks by record), RAM outside the OS heaps, and ARAM in use. Every range is canonicalized.
  - **Order.** Blocks are visited in address order. For two builds that make the same allocations with the same sizes, that is the same permutation of allocation order. A build whose sizes differ already splits on every pointer's arena offset, so allocation order would add nothing.
  - **Typed entries.** No allocation site has needed a registered type yet. Typed entries are added only where Windows and Linux are shown to differ (padding, bitfields), never as byte exceptions.

**Canonical words** (`Canon` in `src/abi.cpp`, applied through `memoryDigestWords` in `sdk/include/mpt/gc/digest.h`). Every 64-bit word of untyped memory is replaced before digesting, as follows:
- **Arena pointers.** A word inside the SDK arena becomes a tagged arena offset.
- **Module pointers** (ADR 0008, "Module words: a safe rule must not depend on layout"). By section:
  - **Code:** only an **exact function start** is canonicalized, to its function's name. The readers take the function type from ELF `STT_FUNC` and from COFF's complex type `IMAGE_SYM_DTYPE_FUNCTION`; section symbols, labels, untyped and data symbols and the layout probe's symbols are never starts. Every other code word (inside a function, padding, below a section's first function) is **hashed raw** and counted as `module_code_interior`. Game state stores only function starts (callbacks, process entries, coroutine records by entry), so such a word is most likely an integer pair.
  - **No section** (the PE or ELF headers, gaps between sections): hashed raw, counted as `module_raw`.
  - **Data and read-only data:** the symbol at or below the word, by name, plus the offset; a word with no symbol at or below it becomes one sentinel (`module_unknown`).

  A **C++ symbol** (a C++ function start, or a C++ data symbol) becomes one shared value per section kind instead, because MSVC and Itanium mangle names differently. `count module_cxx` in `debug_hash_items` shows whether any game state does, and a `note cxx <where> -> <symbol>` line says where and to what. The one case on the title screen was legitimate: msm's `sys` global keeps the previous AI DMA callback, which is the SDK's engine callback. That callback now has a C name, `mpt_gc_ai_engine_callback` (`sdk/src/ai.cpp`), so it hashes by symbol on both platforms, and `module_cxx` is 0. Any later hit gets the same treatment. If the word points to a string literal in read-only data, it becomes the literal's text, up to the NUL and at most 64 bytes. ELF has no symbols for literals, and the MSVC ABI's `??_C@` symbols are therefore treated as anonymous on Windows too.
- **NaNs (ADR 0008's rule for untyped memory, accepted by architect in PR #56).** Only the default NaNs are merged:
  - an f32 half `0xFFC00000` becomes `0x7FC00000`;
  - an aligned 64-bit word holding the f64 default NaN, `0x7FF8000000000000` or `0xFFF8000000000000`, hashes as one value (`0x7FF8000000000000`).

  Every other bit pattern stays exact.
  - **Why not every NaN.** The amendment says "an aligned word with an f32 or f64 NaN bit pattern becomes the canonical NaN". In untyped memory that would also merge every s32 from -1 to -8,388,607 (`0xFF800001`–`0xFFFFFFFF` are f32 NaN patterns), and every word whose high half is -1. Small negative integers are common in game state, so that would hide real divergence.
  - **What the narrower rule covers.** It covers the case P1-14 found: the sign of a default NaN chosen by operand order. Typed fields (`update_f32`/`update_f64`) still merge every NaN.
  - **What is left open.** A payload difference in memory, beyond the default NaN's sign, is not merged. Any NaN difference the Linux comparison finds goes to architect as a typed-entry candidate (the `debug_hash_items` diff names its symbol or range). The untyped rule is never widened.
- **The merge-only rule holds.** Each rule can merge two values but never split one.
- **Cost of the check.** A cheap three-compare test covers four words at a time. The arena and module bases (`0x4000'0000'0000`, `0x5B97'9E3C'0000`) share a window, upper halves `[0x4000, 0x6000)`, that one compare covers; the module's upper half is `0x5B97` throughout, because the host refuses an image that crosses a 4 GiB boundary. Only a block containing a special word pays for the exact rules.

**Cost** (Windows, Release, title screen and boot window, mean of 100 calls per run):
- `state_hash`: 5.8–6.0 ms per call.
- Deep hash: 3.95 ms per call. P1-16's hash measured 3.5–3.8 ms on the same screens.
- **Where the time goes.** About 1 ms is the symbol pass (5,832 symbol groups, about 320 KB of nonzero data). The canonical memory walk is most of the rest: roughly 115 MiB of HuMem heaps, of which only the allocated blocks are read. The first version cost 9.6 ms, with the special-word test applied word by word.
- **Budget.** At checkpoints every 60 frames this is about 0.6% of run time. A checkpoint frame spends about a third of its 16.7 ms budget on hashing.

**Debug entries (ABI v1.3, `MPT_MP4_BOOT_DEBUG_HOOKS` only).**
- `debug_deep_hash`: the raw-memory hash.
- `debug_hash_items`: every part above with its size and digest, in hash order, then (outside the hash) how many words each canonical rule changed: `count arena`, `module_symbol`, `module_text`, `module_cxx`, `module_unknown`, `nan32`, `nan64`, and the module words hashed raw: `module_code_interior` (a code word that is not a function start), `module_raw` (a module word in no section) and `module_misaligned` (a module address at 4 mod 8, which no rule sees). Each C++, unknown, raw, misaligned or code-interior hit also gets a `note` line naming where it was found and the symbol it points into, with the word itself for a raw or unknown hit. With `MPT_MP4_BOOT_DEBUG_NOTE_MODULE_HITS` (`mpt_dump_lane --note-module-hits`) every module hit gets one, symbol and text hits included. The list may grow; readers match names. That means one `sym <range>:<name>` per symbol group, one `<kind> <arena offset>` per RAM range, then `aram` and the part totals. `mpt_dump_lane --hash-items FILE` writes them at the last checkpoint. With `--party` it also sums each count over every checkpoint (printed at the end) and writes every distinct note, with the number of checkpoints it appeared at, to `FILE.notes`. **Diffing a Windows file against a Linux one names the symbol or range that differs**, which is the first step after a Linux run (amendment point 3). The file names the game's symbols, so it stays in the directory given and is never committed.
- `MPT_MP4_PERTURB_SAMPLE | (k << 8)`: flips a byte chosen from `k` among everything the deep hash digests as memory. The next call with the same target restores it.

**Coverage proof (E4), with the deep hash as the oracle** (`mpt_dump_lane --perturb [--perturb-samples N]`). At every checkpoint:
- **Fixed targets.** Each of the six flips must be restored, and both hashes must come back.
  - Game data, a HuMem block, ARAM in use and GWSystem must change the deep hash and `state_hash`. A flip that changes the deep hash but not `state_hash` fails as "manifest misses an area".
  - A free HuMem block and a process stack must change neither.
- **Sampled bytes.** N sampled bytes (default 32) go through the same oracle. They are spread over game data, main RAM, ARAM and all of them.
- **Result.** 10,000 frames with checkpoints every 1,000 and 64 samples: all 60 fixed checks and 640 of 640 sampled bytes pass.

**Layout independence on Windows (the stand-in for Linux).** `MPT_MP4_RECIPE_LAYOUT_PROBE=ON` or `rodata`, test only, builds the module with `src/layout_probe.c` first in the link. Architect kept it as a **standing regression check on the dump lane** (PR #56). It is now the CTest `dump_lane_layout_perturbation` (steps in `tests/dump_lane/README.md`).
- **What the probe does.** ON inserts 8 KiB of code, which moves every function, and 4 KiB + 24 bytes at the start of the game core's data range and of its read-only data. `rodata` inserts only the read-only padding, so the sections after it move by whole pages. The probe's own symbols delimit their neighbours but are never hashed, and it has no `mpt_rw_anchor` of its own.
- **Layout bug found after #64 (2026-11-07).** Party Board patches 0023 and 0024 moved the data sections by one page, and the trail moved with them.
  - **Cause.** aurora's PC `GXTexObj` and `GXTlutObj` are `u32` arrays, so they are only 4-aligned, but aurora stores pointers in them. `AnimTexData` puts a `GXTexObj` after two `bool`s, so every second texture object held its data pointer at 4 mod 8. The canonical rules see aligned words only, so those addresses (the DOL texture blobs in `s_blob_storage`) were hashed raw.
  - **Fix.** aurora patch 0017 gives both objects 8-byte alignment.
  - **Check.** The pre-0023 and post-0024 modules give identical trails to 20,000 frames with the fix (seed 1, `--party`). Without it they differ from frame 1,000.
- **Probe runs had stopped matching** before that: the probe object brought one more `core:mpt_rw_anchor`, so its trail differed from frame 0. Nobody had rerun the probe since the anchor came in (P1-17).
- **Result before the new base** (`--party`, 20,000 frames, a checkpoint every 1,000, seeds 1–5): **`rodata`** was identical to the normal module at all 20 checkpoints on every seed. **ON** differed from frame 3,000 on seed 1, because of the integer pair below.
- **Closed: an integer pair that looks like a code address** (ADR 0008, "Module words: a safe rule must not depend on layout"). A game struct held an ID and a flag word side by side (`0x216e`, `0x5000`). At the old base `0x5000'0000'0000` that read as the module base + `0x216e`, inside `.text`, and the rule mapped it to a function and an offset, which moved whenever code did. architect's ruling moved the base to `0x5B97'9E3C'0000` and narrowed the code rule to exact function starts. The pair no longer falls in the module, and the ON trail now matches to frame 81,000 (below).
- **Results at the new base** (2026-11-08, Windows, `mp4-code-word-canonicalization`):
  - **`rodata` against OFF**, `--party`, 20,000 frames, seeds 1–5: identical at all 20 checkpoints on every seed.
  - **ON against OFF, the full party (the gate), seed 1**: the trails are identical up to frame 81,000 and **differ from frame 82,000 on**, during Candlelight Flight (`m416Dll`). The gate does not pass yet (open, below).
  - **Every module hit noted** (`--party --hash-items --note-module-hits`, OFF, seed 1, the full party to `finished` at frame 250,789, 4,180 checkpoints): `module_symbol` 8,492,384, `module_text` 1,801,654, `module_cxx` 1, `module_unknown` 0, `module_raw` 0, `module_misaligned` 0 and `module_code_interior` 3,315 (summed over checkpoints, at most 2 at one). The 3,403 distinct symbol notes are function starts (callbacks such as particle hooks and object functions) and data symbols by offset, and the 389 text notes are string literals: all real pointers. The one `cxx` hit is a pointer to the RTTI descriptor of `std::exception` in a HuMem block at one checkpoint, a host object's remnant (open, below).
  - `--perturb` (10,000 frames, 64 samples) and `--conformance` pass.
- **Closed by zeroed heaps: stale heap bytes** (ADR 0008, "Stale heap bytes: game heaps return zeroed blocks on PC", §11). The split at frame 82,000 was one heap word holding the low 32 bits of `UpdateEffect`'s address (`chrman.c`'s particle hook) with `0x00000001` above it, in a particle record's `animTime` and `blendMode`/`attr`/`unk_2E` slot. An earlier record 0x40 lower had its `hook` at exactly that address, and the new record never wrote the lower half. 15 of the 16 distinct `module_code_interior` words (`m417Dll`, the item pick, the multi-view camera, such as `0x5B979E6F0000` and `0x5B979E410000`) were the same kind: stale pointers partly overwritten by a 16-bit field. With every game heap zeroing its blocks (patches 0026–0028, the SDK's `OSAllocFromHeap` and `ARAlloc`), the probe trail now matches past frame 82,000, and those 15 words and the `std::exception` RTTI hit (`module_cxx`) are gone: they were stale.
- **Results with zeroed heaps** (2026-11-08, Windows, seed 1 unless noted):
  - **rodata against OFF**, `--party`, 20,000 frames, seeds 1–5: identical at all 20 checkpoints on every seed.
  - **Every module hit noted**, OFF, the full party to `finished` at frame 250,789 (4,180 checkpoints): `module_cxx` 0, `module_unknown` 0, `module_raw` 0, `module_misaligned` 0, and `module_code_interior` 58 (one word, at most 1 per checkpoint; closed below). 3,629 distinct notes: the symbol notes are function starts and data symbols by offset, and the text notes are string literals, all real pointers.
  - **ON against OFF, the full party**: identical to frame 104,000, **different from frame 105,000** (closed below).
  - `--perturb` (10,000 frames, 64 samples): 0 failed. `--conformance`: 0 problems, 70 of 70 coverage targets.
  - **Cost of zeroing** (ADR 0008, "Stale heap bytes", point 5). The same machine, `mpt_dump_lane --boot mp4 --party --seed 1 --frame-times`, N = 5 runs of each build, alternating, one at a time: the build before zeroing (this branch at `a1bd65a` with the two zeroing commits reverted) against the zeroing build. Exempt frames come from game-visible events only: boot until the first `bootDll` frame, overlay changes, and frames in which the SDK completes a DVD read or an ARAM transfer (1,229 of 250,800 frames in every run of both builds). Medians:

    | | before | zeroing | change | budget |
    |---|---|---|---|---|
    | total step time, full party | 381.4 s | 376.5 s | −1.3% | under +2% |
    | process wall time | 471.3 s | 464.8 s | −1.4% | |
    | p99 over 249,571 steady frames | 2.468 ms | 2.441 ms | −0.03 ms | under +0.25 ms |

    Both are within budget; the difference is below the run-to-run noise (one run of each build was 15–45% slower than the rest, from other load on the machine, and the medians leave those out). No high-water mark is needed.
- **Closed: two layout-dependent words in an HSF model block** (ADR 0008, "Gate status after the implementation", decision point 1). At frame 105,000 (Toad's Midway Madness, turn 9, on the board) the ON and OFF heap dumps differed in exactly two words, 40 bytes apart, in a HuMem block holding a loaded HSF model (`HSFV037`), in its matrix section (matrix 14, at its first and its 11th float): `0x00005B979E745100`, a code address inside `m430Dll`'s code that is not a function start (the one `module_code_interior` word), and `0x0000727D9E727524`, the low 32 bits of another code address with `0x0000727D` above it. Under ON both moved by the same `0x2814`.
  - **How it was found** (the dump lane's test-only arena watches, `tests/dump_lane/watch.h`; OFF, seed 1, from a state saved at frame 104,000). `--find-word` put the first word at arena offset `0x3170910` (the other at `0x31708e8`). `--watch-word` showed both written in frame **104,584**, when the Boo house loads its model; before that the bytes belonged to an earlier block. `--watch-write`, a hardware data breakpoint on both words, caught the last stores in `C_MTXConcat`.
  - **The writer, for both words: `SetRevMtx` reads an uninitialised local.**
    - Call path: `ExecBooHouse` (`board/boo_house.c:184`) → `BoardModelCreateParam` (`board/model.c:248`) → `CreateInstance` (`board/model.c:184`) → `Hu3DModelCreate` (`hsfman.c:388`, calls `LoadHSF` at line 405) → `LoadHSF` (`hsfload.c:658`, calls `InitEnvelope` at line 693) → `InitEnvelope` (`EnvelopeExec.c:25`, calls `SetRevMtx` at line 81; inlined) → `SetRevMtx` (`EnvelopeExec.c:430`) → `MTXConcat` (aurora's `C_MTXConcat`). Lines are in the pinned Party Board source, `98ac07fd1e8f7ab938bbb6fe75789fad99419010`, which no patch before 0029 touches in `EnvelopeExec.c`.
    - Cause, class "reading uninitialised stack memory". `EnvelopeExec.c:443` inverts each object's matrix into the local `Mtx sp38`, and line 444 concatenates `sp38` into the model's reverse matrices (`MtxTop`, the matrix section). `MTXInverse` returns 0 and writes nothing for a singular matrix (on the console and in aurora alike). For this model the first matrix inverted is singular (its determinant is 0, as for an object whose matrix `SetMtx` never sets, or one with a zero scale), so line 444 reads `sp38` before anything has written it.
    - Why the value is code-derived. On PC `sp38` lies in stack memory that earlier host calls used, and their frames hold return addresses and saved pointers. `MTXConcat`'s float arithmetic carried those bytes through to within rounding: the first word is the return address after a call to `HuDataSelHeapReadNum` in `m430Dll`'s code, `0x5B979E7450FE`, two units off, and the second holds the low half of an `m427Dll` address. They move with the code, which is the split. On the console `sp38` held other stale stack bytes: the same read, with a value that only happens to be layout-independent there. Further evidence: under the hardware watch, whose handler runs on the game's stack, the second word came out 0 instead, because the handler's own frames had changed the stale stack.
    - Is it read: yes. The renderer and `EnvelopeProc` read the reverse matrices, so a typed entry was ruled out (decision point 1).
  - **Fix: Party Board patch 0029.** `SetRevMtx` clears `sp38` before its loop, on `MPT_PC`. A failed inverse before any successful one now gives a zero matrix on every platform; after a successful inverse the loop still reuses the last one, as the game always did. `mp4_envelope` builds `EnvelopeExec.c` from the patched pin and runs `InitEnvelope` on a hand-made two-object model with a singular first matrix over a stack filled with a pattern: the reverse matrix comes out zero with the patch, and the test fails without it.
  - msm is not the writer, so its 64-bit header overrun stays for its own PR (ruling, finding 1).
- **Closed: the seed 2 split (same procedure).** With patch 0029 the first seed 2 gate run differed from frame 71,000, in Hide and Go BOOM! (`m427Dll`). `debug_hash_items` named HEAP_SYSTEM's HuMem heap; a canonical diff of the two lanes' memory at frame 71,000 (module words mapped to their symbols, process stacks set aside) left two words in a 0xF40-byte block (`num` 0x10000000) of 60-byte records: `0x9EA0533F`, the low 32 bits of the address of `seqPunctTbl` (a module data symbol), each in a float slot where the neighbouring records hold 0.0. `--watch-word` put the first write at frame 70,499 and `--watch-write` caught it in `fn_1_15F10`.
  - **The writer: three hit-box Vecs with an unset y.** Call path: `omMain` → `fn_1_7F38` (`REL/m427Dll/map.c:1467`, calls `fn_1_8768` at lines 1537, 1552 and 1565) → `fn_1_8768` (`map.c:1618`) → `fn_1_15F10` (`REL/m427Dll/player.c:2003`), which copies its two Vec arguments into a record of `lbl_1_bss_57C` (a HuMem block). `fn_1_8768` sets only `x` and `z` of `sp2C` and `sp20` (lines 1680–1688) before the call at line 1689. `fn_1_7C34` (`map.c:1406`, call at 1438) and `fn_1_7F38` (call at 1570) do the same with `sp14` for `fn_1_15FD0` (`player.c:2027`). Class: reading uninitialised stack memory, on the console as well.
  - **Why it is code-derived.** The `y` slots took whatever the stack held there, on PC the low half of a pointer to `seqPunctTbl` left by an earlier host frame; it moves with the data layout (by `0x4020` under ON).
  - **Read list** (ADR 0008, "Stale heap bytes", point 6): the records' `y` (`unk_1C.y`, `unk_28.y`) is never read. The hit tests in `player.c` (lines 2050–2140) and the debug draw `fn_1_13FBC` (`player.c:1654`) read only `x`, `z` and `unk_34`; nothing else takes the records. So it changes no behaviour, but a patch at the source is still simpler than a typed entry.
  - **Fix: Party Board patch 0031** sets the unused `y` to 0.0 at the three sites, on `MPT_PC`. `m427Dll` cannot be built without the rest of the REL, so the regression check is the seed-2 dump-lane run (accepted in ADR 0008, "Gate status after the writer fixes", point 4): with the patch the two lanes' trails are identical from frame 71,000, where the split had been, to frame 111,000, and every module count is 0 there; the full seed-2 gate in M4-03 repeats it to `finished`.
  - **Every caller in the pin** (`98ac07f`, whole `src/` and `include/`; the names are `m427Dll`-local, and `include/REL/m427Dll.h:31–32` only declares them):

    | Caller | Callee | Vec argument(s) | Components set before the call |
    |---|---|---|---|
    | `fn_1_7C34`, `map.c:1438` | `fn_1_15FD0` | `sp14` | `x`, `z` (lines 1436–1437): patched site |
    | `fn_1_7F38`, `map.c:1570` | `fn_1_15FD0` | `sp14` | `x`, `z` (lines 1568–1569): patched site |
    | `fn_1_8768`, `map.c:1689` | `fn_1_15F10` | `sp2C`, `sp20` | `x`, `z` (lines 1680–1688): patched site |

    There is no other caller, so every call either sets all three components or is one of the three patched sites.
- **Results with the writers fixed** (2026-11-09, Windows, `mp4-layout-gate-writer`: patches 0029–0031 and the `HuARMalloc` ARAM coverage of §11, which was in place for every run below):
  - **Every module hit noted**, OFF, seed 1, the full party to `finished` at frame 250,789 (4,180 checkpoints): `module_code_interior` 0, `module_cxx` 0, `module_unknown` 0, `module_raw` 0, `module_misaligned` 0. 3,628 distinct notes, 3,239 symbol notes (function starts and data symbols by offset) and 389 text notes (string literals), all real pointers. Seed 2 to frame 111,000 (see below): all five counts 0 as well, 3,177 distinct notes.
  - **The layout gate, ON against OFF, seed 1:** the full party, both lanes exit 0 with the Director at `finished` at frame 250,789, and the trails are identical at all 250 checkpoints (last `frame 250000 hash b6f0e8560e2f0da5`). **Met for seed 1 on Windows.**
  - **Seed 2: not met in these runs, blocked by a crash that is not layout-dependent (fixed in M4-03 and met there on Windows: "Results with the M4-03 fixes" below).** Both lanes give identical trails up to frame 111,000 and then crash identically at frame 111,514 (turn 11, Toad's Midway Madness): an access violation reading `0x00004000B6F43440` in `byteswap_animbankdata`, from `HuSprAnimRead` ← `Hu3DAnimCreate` ← `RoundItemInit` (`REL/w01Dll/mg_item.c:116`, the round item's `itemAnim`, data `w01` 28). `main` at `8a1431d` (before this branch's patches) crashes at the same frame and address, so the branch did not cause it. It is a port crash on a seed nobody had played to the end, for the board and seed sweep (M4-03) and the crash reports (M4-04); the seed 2 gate is rerun once it is fixed. **Ruling (ADR 0008, "Gate status after the writer fixes", point 1): the seed-2 gate is open, blocked by this crash, which is an M4-03 item.** M4-03 records and fixes the crash, then reruns the seed-2 gate over the full party to `finished` (both lanes exit 0, identical trails) with every heap module count 0 (point 2). The layout gate is met for seed 1 only, on Windows.
  - **rodata against OFF**, `--party`, 20,000 frames, seeds 1–5: identical at all 20 checkpoints on every seed.
  - `--perturb` (10,000 frames, 64 samples): 0 failed, now 8 fixed targets per checkpoint (80 checks) with the `HuARMalloc` block and free ARAM, and 640 of 640 sampled bytes. `--conformance`: 0 problems, 72 of 72 coverage targets.
  - **Re-recorded** (the fixes change heap contents): the M1 hashes below; the M4-01 autopilot replay (§21) now ends at `43198e9219fd8f37` (250,800 frames, 45,967 input runs, 4,180 checkpoints, 6,168,073 bytes, `finished` at 250,789), which supersedes `963f80f9096ee682`. The replay stays local.
- **Results with the M4-03 fixes** (2026-11-10, Windows, `m4-03-board-sweep`: patches 0032–0034 and the autopilot's stall policy, §21 "M4-03"). Each lane ran `--party --every 10 --hash-items` in its own process, the full 20-turn party. **Ruling (ADR 0008, "Gate status after M4-03"): the layout gate is met on Windows for seeds 1 and 2.** Every run below is on Windows; none says anything about Linux, where the module has not run (M4-U2). E3c is still not run for acceptance: the msm header overrun (§11; M4-15, whose PR reruns this gate) and the Linux lane come first.
  - **The layout gate, ON against OFF, seed 1: met on Windows.** Both lanes exit 0 with the Director at `finished` at frame 250,789. The trails are identical at all 250 printed checkpoints (last `frame 250000 hash dc276cab9c3bc0b2`), and the final line is `final frame 250790 hash df121039386e9397` on both. The counts are summed over every checkpoint and printed in the log at the end of the run (`hash items at 25079 checkpoints`, then each count's total and largest value). Those lines are the same on both lanes: `module_code_interior`, `module_cxx`, `module_misaligned`, `module_raw` and `module_unknown` are total 0 and max 0, `module_symbol` is 50,784,322 (max 2,127) and `module_text` 10,809,488 (max 432). The `--hash-items` file itself holds only the last checkpoint's list; the two lanes' files are byte-identical.
  - **The layout gate, ON against OFF, seed 2: met on Windows.** Both lanes exit 0 with the Director at `finished` at frame 272,239 (the party passes turn 11, where it crashed before patch 0032, and the two lottery stalls of §21 "M4-03"). The trails are identical at all 272 printed checkpoints (last `frame 272000 hash 1a63526f2445af3b`), and the final line is `final frame 272240 hash 036db6e228961154` on both. The log's count lines (`hash items at 27224 checkpoints`) are the same on both lanes: the same five module counts are total 0 and max 0, `module_symbol` is 55,165,625 (max 2,341) and `module_text` 11,733,934 (max 432). The two `--hash-items` files (the last checkpoint's list) are byte-identical. This is the rerun that ADR 0008, "Gate status after the writer fixes", point 2 asks for.
  - **Notes for every module hit, seed 2** (ADR 0008, "Gate status after M4-03", point 4). The gate runs above did not pass `--note-module-hits`, so they counted the hits but wrote no notes (both `.notes` files are empty). `architect` ran seed 2 once more on the OFF build with a note for every module hit and a checkpoint every 60 frames: exit 0, `finished` at frame 272,239. Its 45 printed checkpoints (every 6,000 frames) carry the gate trail's hashes at those frames. Over 4,538 checkpoints the five counts are total 0 and max 0, with `module_symbol` 9,196,002 and `module_text` 1,955,944. There are 3,843 distinct notes: 3,454 named symbols and 389 string literals' text, all real pointers. runtime-gc-wii checked these numbers against that run's log and notes file and did not rerun it.
  - **How the two lanes' logs differ**, on both seeds, in diagnostic lines only:
    - libco's stack high-water lines print a host entry address, 0x2010 higher under the ON probe (its 8 KiB of code);
    - the timings (`stateHash` microseconds per call, the end-of-run save and load milliseconds);
    - the end-of-run save state's size: 148,978,189 bytes under the ON probe against 148,974,061, which is 4,128 bytes larger, with `hash unchanged` by the round trip on both. A save state carries the game core's data range, and the ON probe puts a 4,120-byte array in it (`src/layout_probe.c`). `llvm-nm` on the two modules shows the data behind the array 4,128 bytes further on (the next symbol is 16-byte aligned). The state hash leaves the probe's symbols out.
  - **rodata against OFF**, `--party`, 20,000 frames, seeds 1–5: identical at all 20 checkpoints on every seed (last hashes `d8547d30afd54858`, `bfeebdf9edac05be`, `1d248d5d527963ec`, `9ad86c05b4f7380a`, `a76063ba54fb5b1d`).
  - `--perturb` (10,000 frames, 64 samples): 0 failed, 8 fixed targets per checkpoint. `--conformance`: 0 problems, 72 of 72 coverage targets.
  - **Re-recorded:** the M1 hashes below, and the M4-01 autopilot replay (§21), which now ends at `7c843ad641d303eb` and supersedes `43198e9219fd8f37`. The seed-1 gate's last hash moved from `b6f0e8560e2f0da5` to `dc276cab9c3bc0b2`. **Patch 0032 moved them** (by what it writes and by eliminating the other two, not by a run with patch 0032 alone): it leaves the conversion mark and the converted `ANIMDATA`'s address in the header of every anim file it converts, which is game heap, from the first sprite read at boot on. Measured on the M1 seed-1 run: the hash at frame 10,000 is the same with and without patch 0034, and the same with patch 0033 taken out of the build (`6a4787b9d62c797b` each time), so neither moves it. Patch 0033 changes game memory only where an inverse fails on a matrix that was not set first, which those 10,000 frames do not reach; whether a full party reaches one was not measured. The replay stays local.
- **Results with the M4-15 fix** (2026-11-12, Windows, `m4-15-msm-header`: Party Board patch 0035, msm's PC block header, §11). The rerun that ADR 0008, "Gate status after the implementation" and "Gate status after M4-03", point 2, ask of this fix. Each lane ran `--supervise --crash-report FILE --party --every 10 --hash-items FILE` in its own process, the full 20-turn party, on the OFF and ON builds; seed 1 also passed `--note-module-hits`. No run wrote a crash report.
  - **Seed 1, ON against OFF.** Both lanes exit 0 with the Director at `finished` at frame 250,789 (the same frame as before the fix). The trails are identical at all 250 printed checkpoints (last `frame 250000 hash b73556aba1575ad4`), and the final line is `final frame 250790 hash f66f1b7ea402c5f9` on both. The log's count lines are the same on both lanes (`hash items at 25079 checkpoints`): `module_code_interior`, `module_cxx`, `module_misaligned`, `module_raw` and `module_unknown` total 0 and max 0; `module_symbol` 50,784,322 (max 2,127); `module_text` 10,809,488 (max 432); `nan32` 1,379,142 (max 2,390); `nan64` 0. These are the M4-03 counts. The `--hash-items` files (the last checkpoint's list) are byte-identical, and so are the two `.notes` files: 3,672 distinct notes, 3,283 named symbols and 389 string literals' text.
  - **Seed 2, ON against OFF.** Both lanes exit 0 with the Director at `finished` at frame 272,239 (as before). The trails are identical at all 272 printed checkpoints (last `frame 272000 hash fe190bfe19f6f33f`), and the final line is `final frame 272240 hash e2173441c813ba99` on both. Count lines, the same on both lanes (`hash items at 27224 checkpoints`): the five module counts total 0 and max 0; `module_symbol` 55,165,625 (max 2,341); `module_text` 11,733,934 (max 432); `nan32` 1,432,738 (max 3,529); `nan64` 0, again the M4-03 counts. The `--hash-items` files are byte-identical; the `.notes` files are empty (no `--note-module-hits`).
  - **Seed 2 with a note for every module hit** (qa-reviewer's run in the #82 review, OFF build, `--party --seed 2 --every 10 --hash-items FILE --note-module-hits`): exit 0, `finished` at frame 272,239, `final frame 272240 hash e2173441c813ba99` (the gate's). Over 27,224 checkpoints the five module counts total 0. There are 3,887 distinct notes: 3,498 named symbols and 389 string literals' text, all real pointers. runtime-gc-wii records qa's numbers here and did not rerun it.
  - **The logs differ** only as in M4-03: libco's entry addresses, the timings (about 2,000 to 2,400 s per lane, four lanes at once), and the save state's size, 148,978,189 bytes under ON against 148,974,061, with `hash unchanged` on all four.
  - **rodata against OFF**, `--party`, 20,000 frames, seeds 1–5: identical at all 20 checkpoints on every seed (last hashes `b011611a04400dfc`, `d8f4b3d5c373b269`, `b4ae409823eb6614`, `47144ed191490541`, `d14ba3b2005faff8`).
  - `--perturb` (10,000 frames, 64 samples): 0 failed; at each of the 10 checkpoints the 8 fixed targets and 64 of 64 samples are `ok`. `--conformance`: 0 problems, 72 of 72 coverage targets.
  - **Re-recorded:** the M1 hashes below and the M4-01 autopilot replay (§21), which now ends at `1be7b9fb2eccad06` and supersedes `7c843ad641d303eb`. The gate's final hashes moved from `df121039386e9397` (seed 1) and `036db6e228961154` (seed 2). **Patch 0035 moved them, and only it:** the branch's other changes are a comment in patch 0032, the dump lane's option check, an SDK header comment and tests. Measured: a build of this branch with patch 0035 taken out gives `final frame 3000 hash 74118b890e88ecf6` on seed 1, M4-03's hash at frame 3,000; with the patch, both lanes give `7bd4ac7fd3f311b9` there. The trails differ from M4-03's from the first printed checkpoint, frame 1,000, on. The parties' course is unchanged: every finish frame is M4-03's.
  - **Board spot-check** under `--supervise --crash-report`, OFF build: `mp4.board.shy_guys_jungle_jam` (`--party-board 2`) seed 1 and `mp4.board.bowsers_gnarly_party` (`--party-board 5 --party-unlock-bowser`) seed 2 both exit 0 at turn 20/20, finishing at frames 197,498 and 214,576 (the sweep's frames, §21), final `f98ed2176e6d0d77` and `ac3a33a462309a0d`; no crash report.
- **Results with the stream refill fix** (2026-11-16, Windows, `dawn-preview`: Party Board patch 0036, msm's stream refill, §23). The same gate as M4-15, on the null backend: each lane ran `--supervise --crash-report FILE --party --every 10 --hash-items FILE` in its own process, the full 20-turn party, on the OFF and ON builds; seed 1 also passed `--note-module-hits`. No run wrote a crash report.
  - **Seed 1, ON against OFF.** Both lanes exit 0 with the Director at `finished` at frame 250,789. The trails are identical at all 250 printed checkpoints (last `frame 250000 hash 4d4c759783d29ea5`), and the final line is `final frame 250790 hash 3214cbcfc8a748ff` on both. The count lines are identical (`hash items at 25079 checkpoints`): the five module counts total 0 and max 0; `module_symbol` 50,784,322 (max 2,127); `module_text` 10,809,488 (max 432); `nan32` 1,379,142 (max 2,390); `nan64` 0, the M4-15 counts. The `--hash-items` files are byte-identical, and so are the `.notes` files (3,672 lines).
  - **Seed 2, ON against OFF.** Both exit 0 at `finished` at frame 272,239. All 272 printed checkpoints are identical (last `frame 272000 hash a022e0699ea2a674`), final `final frame 272240 hash 817434cdf8fb335d` on both. The five module counts total 0 and max 0; `module_symbol` 55,165,625 (max 2,341); `module_text` 11,733,934 (max 432); `nan32` 1,432,738 (max 3,529); `nan64` 0. The items files are byte-identical.
  - The save states are 148,974,061 bytes under OFF and 148,978,189 under ON, with `hash unchanged` on all four lanes.
  - **rodata against OFF**, `--party`, 20,000 frames, seeds 1–5: identical at all 20 checkpoints on every seed (last hashes `0fe785da008a6c51`, `3d0675bbb6668e7b`, `d3b4b20c3bcf7e68`, `9c88ebc9e4e405d7`, `79f250f9340cd460`).
  - `--perturb` (10,000 frames, 64 samples): 0 failed. `--conformance`: 0 problems, 72 of 72 coverage targets.
  - **Re-recorded:** the M1 hashes below and the M4-01 replay (§21), which now ends at `53b119efe3f28194` and supersedes `1be7b9fb2eccad06`. The gate's final hashes moved from `f66f1b7ea402c5f9` (seed 1) and `e2173441c813ba99` (seed 2). **Patch 0036 moved them, and only it:** with the patch taken out, the same branch gives the M4-15 M1 trail exactly (`df135cfd7b5d551d` at frame 10,000); with it, the trail differs from frame 800 on, when the title screen's stream starts. The parties' course is unchanged: every finish frame is M4-15's.

**History.** P1-16's raw-memory proof found two real bugs:
- Party Board's `byteswap.cpp` had a file-scope `std::unordered_set` in the game data range. It now builds outside the game section.
- Coroutine stacks carried host frames. They are now hashed by record only.

The #49 review moved libco's stack high-water statics out of the range as well.

**msm diagnostics (Party Board patch 0017, `src/msm_diag.c`).** msm only reports `MSM_ERR_GRP_FAILPUSH`. Group pushes now go through `mpt_msm_push_group`, which reports why MusyX refused a group through OSReport: the validation error, msm's heap headroom, and the macros, curves, keymaps, layers or samples that do not resolve.

**M1 (boots headless), Windows, rerun 2026-11-05** on the libm redirects (qa #40), the memory hash manifest and Party Board patch 0019.
- **Boot.** `mpt_dump_lane --boot mp4` on the user's GMPE01_00 dump runs clean: no halt or fault. msm's one diagnostic line is unchanged since the first boot. With no input the game stays in bootDll, the boot and title sequence.
- **Build.** The module builds from the dump in about 70 s. Each 10,000-frame run takes about 12 s.
- **Determinism.** 10,000 frames, checkpoints every 100 frames, in two separate processes:
  - seed 1: `c8f8da4267f8d431` at frame 10,000 in both, identical at all 100 checkpoints;
  - seed 2: `a619f53f9b4f56d5`, different at all 100.
- **Rerun after the #49 review (2026-11-05).** The libco high-water statics left the game data range (`libco_stats.c`), which changes the hashed and saved bytes, so the trail moved. Same check, 10,000 frames, checkpoints every 100, separate processes:
  - seed 1: `8f557606d1449fcd` at frame 10,000 in both, identical at all 100 checkpoints;
  - seed 2: `b8c08c09683489b8`, different at all 100.
  - `stateHash()` cost 3.9 to 5.4 ms per call (means of 100).
- **Rerun with the final cross-platform `state_hash` (2026-11-06).** This is after the #55 review: no raw GW part, C++ pointers as one value, and the AI callback's C name. Same check, 10,000 frames, checkpoints every 100, separate processes:
  - seed 1: `e98de7b50ea4c3cc` at frame 10,000 in both, identical at all 100 checkpoints;
  - seed 2: `55a1fe7e09ecbe5e`, different at all 100;
  - `stateHash()` 6.4 to 6.6 ms per call. No halt or fault.
  - The first cross-platform run (`8eefae0667aae794` / `a93d7072ebb73373`) is superseded.
- **Rerun with zeroed heaps (2026-11-08).** Game heaps now return zeroed blocks (ADR 0008, "Stale heap bytes", §11), which changes heap contents, and so `state_hash`, from the first reused block on. Same check, 10,000 frames, checkpoints every 100, separate processes:
  - seed 1: `c577ef4cd85201d6` at frame 10,000 in both, identical at all 100 checkpoints;
  - seed 2: `297d3cb3468b630b`, different at all 100;
  - `stateHash()` 7.4 to 8.0 ms per call. No halt or fault.
- **Rerun with the gate fixes and ARAM coverage (2026-11-09).** `HuARMalloc`'s blocks are now hashed and start at `HU_AMEM_BASE` (§11), and patches 0029–0031 remove stale stack bytes (§20, layout gate). Same check, 10,000 frames, checkpoints every 100, separate processes:
  - seed 1: `7722e010cc2f09b1` at frame 10,000 in both, identical at all 100 checkpoints;
  - seed 2: `d4d83ecc05f5225a`, different at all 100;
  - `stateHash()` 8.1 to 9.5 ms per call. No halt or fault; the `memory_use` guard check passes (a 160-byte v1.4 caller gets 160 bytes, and the I/O counts have counted 204 DVD reads and 5 ARAM transfers by frame 10,000).
- **Rerun with the M4-03 patches (2026-11-10,** patches 0032–0034; §20, layout gate, "Results with the M4-03 fixes"). Same check, 10,000 frames, checkpoints every 100, separate processes:
  - seed 1: `6a4787b9d62c797b` at frame 10,000 in both, identical at all 100 checkpoints;
  - seed 2: `bb4208d7c03c9aae`, different at all 100;
  - `stateHash()` 13.8 ms per call, measured while other lanes were running, so not comparable with the figures above. No halt or fault; the `memory_use` guard check passes (204 DVD reads and 5 ARAM transfers by frame 10,000).
- **Rerun with the msm header fix (2026-11-12,** patch 0035; §11, §20 "Results with the M4-15 fix"). Same check, 10,000 frames, checkpoints every 100, separate processes:
  - seed 1: `df135cfd7b5d551d` at frame 10,000 in both, identical at all 100 checkpoints;
  - seed 2: `49d98f6c5cd37806`, different at all 100;
  - `stateHash()` 7.9 to 8.3 ms per call, measured while other lanes were running. No halt or fault; the `memory_use` guard check passes (204 DVD reads and 5 ARAM transfers by frame 10,000).
- **Rerun with the stream refill fix (2026-11-16,** patch 0036; §23, §20 "Results with the stream refill fix"). Same check, 10,000 frames, checkpoints every 100, separate processes:
  - seed 1: `2523e333cd8cc6cc` at frame 10,000 in both, identical at all 100 checkpoints. The trail equals the M4-15 trail up to frame 700 and differs from frame 800 on, when the title screen's stream starts;
  - seed 2: `5677a93bacfda417`, different at all 100;
  - the same branch with patch 0036 taken out gives the M4-15 trail exactly, ending at `df135cfd7b5d551d`;
  - `stateHash()` 5.5 to 6.8 ms per call, measured while other lanes were running. No halt or fault; the `memory_use` guard check passes (204 DVD reads and 5 ARAM transfers by frame 10,000).
- **Earlier trail.** The first M1 (2026-10-21) used the coarse hash (SDK state and named globals only). Its trail did not move with the libm change, because it did not cover game memory.
- **Not yet.** The Linux runs. E5 is narrowed to in process (`docs/phases/phase-1.md`, M4-D) and met on Windows with the module kept loaded for the process (§11, "E5 in process (M4-16)").

**M2 (Dawn backend, title screen, menus), Windows, 2026-11-05.**
- **Backend.** The host build with `MPT_GX_BACKEND=dawn` passes the backend and Dawn prefix to the recipe, and they are part of the cached module's key. The recipe installs the module's runtime DLLs (`webgpu_dawn.dll`) next to it. The GX layer names vcpkg's static zlib (`zs.lib`), since the Clang build has no vcpkg toolchain. Video is 640x480 RGBA through the readback path.
- **Boot.** The Nintendo and Hudson logos render from frame 60 to about 540.
- **Title and menus.** A script of `start@900,start@1200,a@1500,a@1800,a@2100,a@2400` reaches, in order:
  - the title screen (about frame 960);
  - the file-select screen with the memory card slots;
  - the three file slots;
  - the new-file opening on the blank card.
- **Fixed on the way.**
  - **Exit crash.** The Dawn lane ended in a fail-fast (0xC0000409), because the module's static teardown ran at process exit after the other threads were gone. The last `Mp4Runtime` then unloaded the module (`release_module`) while the process ran. Since M4-16 the module stays loaded for the process and is unloaded by the title's shutdown after `run()` returns (§11, "E5 in process (M4-16)").
  - **Boot audio.** Party Board patch 0019 restores the boot sound group and the title music (TODO PC upstream).
- **Opening movie: open.** Party Board skips the movie on PC, so with no input bootDll shows a black screen until START or A. Enabling it showed that `THPSimpleOpen` compares the big-endian THP header (`mVersion`, offsets) on a little-endian host, and fails every frame. The movie needs a THPSimple patch that reads the file and frame headers in host byte order; until then it stays off.
- **Pipeline cache.** aurora reported that its SQLite pipeline cache could not open its database: it never creates `cachePath`, and `<build>/gx-cache` did not exist. Fixed in the Dawn preview (§23): the renderer creates the folder, and both caches open.

**Still open in P1-16:**
- **Linux: built, run and diffed (E3c).** The module has never been built on Linux, and this machine has neither Linux nor WSL. No MP4 golden trail is committed until Windows and Linux match on the full-party replay (ADR 0008 amendment, point 3). Two ways to get there, both of them without the dump ever going on CI infrastructure (D4):
  - **A CI job, without a dump.** A Linux job that configures `MPT_BUILD_MP4_GAME_MODULE=ON` from the pinned sources (compile-only, as on Windows) proves that the module builds and links on Linux. It also shows that the ELF section markers (`__start_`/`__stop_`), lld `--image-base` and `.symtab` are in place, and it runs `mp4_rel_isolation` and the host tests. It excludes `mp4_game_abi`, which loads the module: ADR 0009 allows no CI test that runs the module (the M4-12 job builds the module and leaves that test out). It cannot run the game.
  - **A Linux machine with the user's own dump.** For example a native Linux install, or WSL2 if the user enables it. Clang, lld, CMake and Ninja are needed, and `mpt_dump_lane` must be built from this branch. The runs to make:
    - `mpt_dump_lane --boot mp4 --frames 10000 --every 100 --seed 1 --hash-items <dir>/items_linux.txt`, plus `--perturb`;
    - the same run on Windows, then a diff of the two trails and the two item files.
    - Patch 0032 puts a host pointer in hashed game memory: the converted `ANIMDATA`'s address, at offset 8 of each converted anim file's header in `HEAP_DATA`. The hash's canonical walk turns that word into an arena offset, so the trail compares only if the `ANIMDATA` lies at the same arena offset on both hosts, as every other heap pointer must. The raw word is the host address, so a save state carrying it is valid only at the fixed arena base (§11).

    The first differing item names the area that needs a typed entry. The full-party replay follows once P1-18's replay exists.
- The rest of the GX front-end state (`g_gxState`) in the hash manifest (E4). The game-visible draw-sync state is done (P1-17).
- E5, narrowed to in process (M4-D): met on Windows, see §11, "E5 in process (M4-16)".
- The conformance suite on the dump lane.

---

## 21. M3: a board turn and a minigame (P1-17, `p1-17-mp4-adapter-m3`)

**Adapter.** See §20: the catalog and `snapshot_of`, with `current_minigame` from the running overlay.

**Party-eligible list (P1-17, confirmed 2026-11-25).** The minigames a regular party can launch are confirmed from the board code (§18.1, "Which minigames a party can launch", with file and line for every path): 46 minigames, the 44 rows §18 marked plus Bowser Wrestling and Panels of Doom, which Bowser's Gnarly Party launches for a Mega-size player. The adapter answers `mpt::mp4::party_eligible(id)` from that explicit list, not from the category, and `tests/unit/test_mp4_host.cpp` pins the 46 IDs and the 15 exclusions (the five Item rows, The Final Battle and the nine Extra Room rows). No `core/` type changed.

**The adapter's other calls in Phase 1 (P1-17).** `applyRuleset` accepts only an empty ruleset (the seed is always set by the host) and fails with a reason for any rule. `writeParty` is unused in Phase 1: the Director is a read-only mirror (ADR 0005), so the call does nothing. `launchMinigame` fails with a reason and `pollMinigameResult` reports nothing; both wait for Phase 5 (ADR 0002). `tests/unit/test_mp4_host.cpp` pins all four on an unbooted runtime: every rule field is rejected, and `writeParty` changes neither the party read nor the state hash. No `core/` type changed.

**Full-party event test (P1-17, 2026-11-26, Windows, Release, null backend).** `mpt_dump_lane --boot mp4 --party-events` (`dump_lane_mp4_party_events`, label `dump`; `tests/dump_lane/README.md`, "Party events") plays the M4-01 autopilot replay on a fresh `Session` and checks its `PartyEvent` sequence and final `PartySnapshot` against structural rules (`check_full_party`, `dump_lane.h`), not against a recorded golden. The replay was recorded locally into the build cache and is never committed (M4-F). Its recording run gave the M4-01 numbers below unchanged: finished at frame 250,789, 250,800 frames, 45,967 input runs, 4,180 checkpoints, 6,168,073 bytes, last hash `53b119efe3f28194`.
- **Two separate verifying processes.** Each played 250,800 of 250,800 frames and matched 4,180 of 4,180 checkpoints, with last hash `53b119efe3f28194` and the Director at `finished`. Each saw the same 124 events: 25 minigames over 20 turns, `results` at frame 246,053 and `finished` at 250,788 (the step after which the Director saw it). Each printed `party events: PASS`.
- **Final snapshot.** Turn 20/20 on `mp4.board.toads_midway_madness`:

  | Seat | Character | Seat type | Stars | Coins |
  |---|---|---|---|---|
  | 1 | `mp4.character.mario` | human | 1 | 67 |
  | 2 | `mp4.character.luigi` | CPU | 2 | 70 |
  | 3 | `mp4.character.peach` | CPU | 0 | 116 |
  | 4 | `mp4.character.yoshi` | CPU | 2 | 160 |

- **The minigames, as evidence only** (the test does not pin them). Frames are when each started; all 25 are party-eligible.
  - Turns 1–3: `paratrooper_plunge` (11,181), `long_claw_of_the_law` (18,591), `fruits_of_doom` (22,787), and on turn 3 also `team_treasure_trek` (35,431).
  - Turns 4–9: `cliffhangers` (46,697), `fish_n_drips` (60,238), `money_belts` (72,323), `candlelight_flight` (81,200), `hide_and_go_boom` (93,181), `the_great_deflate` (108,389).
  - Turn 10: `reversal_of_fortune` (111,808), then `pair_a_sailing` (122,268).
  - Turns 11–13: `right_oar_left` (133,193), `blame_it_on_the_crane` (144,994), `mario_medley` (154,935).
  - Turn 14: `reversal_of_fortune` twice (160,928 and 166,079), then `team_treasure_trek` (170,853).
  - Turns 15–16: `toads_quick_draw` (180,071), `hop_or_pop` (195,778).
  - Turn 17: `bowsers_bigger_blast` (203,185), then `candlelight_flight` (215,255).
  - Turns 18–20: `take_a_breather` (224,503), `goooooooal` (229,786), `cliffhangers` (235,477).
- **Event sequence details.**
  - Phases: `setup` at 3,914, `board` at 4,306, and `results` at 246,053, 356 frames after the last minigame returned to the board (245,697).
  - Turn 1 starts in `setup`: mentDll's party read already holds turn 1/20.
- **Found on the way: a second party session in one process diverged (fixed 2026-11-26).** The first version of the test verified the replay in the process that had just recorded it. Its fresh `Session` differed from the replay's first checkpoint, at frame 60 (hash `72644e94f129beb9`), while a separate process matched every checkpoint. So the test records in one run, exits as skipped, and verifies in the next.
  - **Cause** (found by qa-reviewer in the review of P1-17). The module stays loaded for the process (ADR 0008, "Game module kept loaded for the process"), and `boot()` restored only the game core's writable data. A REL's data and bss were restored only when that REL was linked. Every REL's range is hashed and saved, so a runtime booted on the kept module after a party started from the previous runtime's REL state.
  - **Fixed** on `mp4-rel-reset-at-boot`: every boot now restores every REL (§11, "Every REL restored at boot"). There, `--reboot-dirty --reboot-dirty-party 252000` (a whole party, then four minigames, then a second clean runtime) matches the first runtime from frame 0, and `dump_lane_mp4_reboot_dirty` guards it.
  - **Possible now, not done:** `--party-events` could verify in the process that recorded the replay again. The test keeps its separate-process verify until that is tried.

**Scripted party on the dump lane.** `mpt_dump_lane --boot mp4` with a `--press` script runs this sequence with A presses (START at the title and on the minigame's instruction screen):
- title → file select on the blank card → mode select (the card picker; the first card is party mode);
- party setup (`mentDll`);
- Toad's Midway Madness with Mario and three CPU players;
- the first turn, then the minigame roulette, the instruction screen (`instDll`) and the minigame.

The lane prints the module's party read whenever it changes. Seed 1, frames from boot:

| Frame | Overlay | Flags | Phase | Notes |
|---|---|---|---|---|
| 934 | `modeseldll` | 0x00 | none | file select and mode select |
| 2450 | `mentDll` | 0x00 | none | party setup, before party mode is set |
| 6158 | `mentDll` | 0x03 | setup | `PARTY_MODE` and `IN_SETUP` confirmed |
| 6551 | `w01Dll` | 0x05 | board | `IN_PROGRESS` confirmed; `GWSystem.board` 0 → `mp4.board.toads_midway_madness`; turn 1/20 |
| 12454 | `instDll` | 0x05 | board | the minigame's instruction screen |
| 12807 | `m401Dll` | 0x05 | minigame | `current_minigame` = `mp4.minigame.manta_rings` |

**One board turn and one minigame, end to end** (null backend; seeds pick the minigame through the roulette). With START timed to each seed's instruction screen, all five seeds play their minigame to the end, and each one returns to the board on turn 2/20 (rerun after patches 0021 and 0022, 22,000 frames each, no halt, fault or fail-fast):

| Seed | Minigame (`current_minigame`) | Minigame frames | Results (`resultDll`) | Back on the board, turn 2 |
|---|---|---|---|---|
| 1 | `mp4.minigame.manta_rings` (m401) | 12847–16574 | 16575 | 17072 |
| 2 | `mp4.minigame.blame_it_on_the_crane` (m424) | 13906–17963 | 17964 | 18451 |
| 3 | `mp4.minigame.team_treasure_trek` (m429) | 14606–19182 | 19183 | 19551 |
| 4 | `mp4.minigame.three_throw` (m410) | 14006–16668 | 16669 | 17231 |
| 5 | `mp4.minigame.slime_time` (m402) | 13607–16587 | 16588 | 17112 |

The phase is `board` on the instruction and results screens, and `minigame` with the right ID while the minigame runs. Seed 4 was also captured with the Dawn backend: frames of the party start, the first turn, Three Throw and its results. The captures stay in the developer's scratch directory, never in the repository.

**Found and fixed on the way:**
- **Mode select hang** (Party Board patch 0020). After a card is picked, `modesel.c` waits for the opening movie with `while (!HuTHPEndCheck())`. Party Board starts that movie only under `__MWERKS__`, and stubs `HuTHPEndCheck` to TRUE in its own port. We build the original `thpmain.c`, where it stays FALSE while no movie has frames, so every mode hung on a star field. With no movie process, `HuTHPEndCheck` now answers TRUE.
- **Game globals in a read-only section** (`src/game_section.h`, `tools/MptMp4Rel.cmake`):
  - **Cause.** LLVM gives an object's game data section the attributes of the first global it emits there. In `board/audio.c` that was `boardMusTbl`, a static the optimizer made constant because nothing writes it, so the object's `.data$mptgame$b` came out read-only. lld-link then linked those sections into a separate read-only `.data`, outside the game data range.
  - **Damage.**
    - `boardSeq`, `boardFX`, the board CPU tables, `FontCharFile` and the option REL's `execModeTbl` were neither saved nor hashed.
    - The first write, in `BoardAudSeqClear` when the board starts, crashed.
  - **Fix.**
    - An anchor (`mpt_rw_anchor`) now comes first in every game and REL section. It is never written; `used` and `volatile` keep it in the object and writable.
    - `const` data goes to `.rdata`.
    - The symbol reader refuses a module with a read-only game data section.
- **Draw-sync state outside the save state** (`gx/src/sdk_additions.cpp`). The game's draw-sync callback and token were file statics. MP4's performance meter registers its callback at init, so a runtime that loaded a state had none, its stopwatch stopped querying the clock, and the resumed run diverged within 60 frames. The callback and token are now an SDK state region, part of `state_hash` and the deep hash, and cleared at boot. `mpt_dump_lane --save-load-diff N` found it by diffing `debug_hash_items` between the straight and the resumed run.
- **Build ID inputs.** Adding a file (patch 0020) did not reconfigure the host, so the cached module was reused. The build ID's globs now use `CONFIGURE_DEPENDS`.
- **Manta Rings crashed loading its model** (Party Board patch 0021). `MaterialLoad` (`hsfload.c`) writes each material's colour to `rgba[i]`, which has 100 entries and is never read. In our module `head` follows `rgba`, so the write for material 107 overwrote `head.material.count` and ended the loop. Materials 107 and up kept their file symbol index as the `attr` pointer, and `MakeDisplayList` faulted on the first face that used one (material 165, `attr` = 0xA5). On the console the extra writes land in a scratch buffer. The writes past `rgba` are now skipped. Four models on the disc have more than 100 materials: Manta Rings (255), Paratrooper Plunge (112), Toad's Quick Draw (122) and Bowser's Bigger Blast (206); all of them were affected.
- **Seed 5 failed fast in turn 2** (Party Board patch 0022). `CoasterPathUpdate` (`w01Dll`, Toad's Midway Madness) passes a three-entry `s16` list to `BoardSpaceLinkTargetListGet`, which clears and fills `BOARD_SPACE_LINKMAX` (4). The module is built with `/GS`, so the overrun hit the stack cookie and the return failed fast (0xC0000409, `FAST_FAIL_STACK_COOKIE_CHECK_FAILURE`). On the console the two bytes land in the frame. The list now has four entries, as the board's other caller already has. The other callers all leave room for four.

**Conformance on the dump lane.** `mpt_dump_lane --boot mp4 --conformance --frames 1200 --every 60 --press start@900` runs every check in `tests/conformance/conformance.h` against `Mp4Runtime` and `Mp4Adapter` on the real game. It then runs `check_hash_coverage` against the deep hash on a runtime with debug hooks: 6 fixed targets and 64 sampled bytes. **All checks pass** (Windows, null backend), including the save-state round trip after the draw-sync fix, with 70 of 70 coverage targets checked. Rerun after patches 0021 and 0022: 0 problems, 70 of 70.

### M4-01: a whole party on an autopilot (`m4-01-party-autopilot`)

**The autopilot.** `mpt_dump_lane --boot mp4 --party` plays a party through a `Session` (`tests/dump_lane/README.md`). Player 1's pad is a pure function of the frame, the Director's phase, the running overlay and the frames since the last change of phase, overlay or turn (`party_autopilot`, `dump_lane.h`, unit-tested):
- START pulses on bootDll, instDll and (once `finished`) the final ranking, A pulses everywhere else, and in minigames a 120-frame cycle of 40 frames with the stick held down, then mashed A. A alone soft-locks a human seat in Reversal of Fortune (m444): its plunger only fires after the stick pulls it back.
- **Seats.** mentDll needs at least one human player: player 0's slot starts human and the others COM (`fn_1_7304` and the player-count screen), so there is no all-COM party. Player 1 is human and driven by the autopilot; three COM players.
- **Board.** The default choices give Toad's Midway Madness and 20 turns. `--party-board N` moves mentDll's board wheel (position order 1, 2, 0, 3, 4, 5, starting at 0) inside a window measured in M4-03 (below), and the run fails if the party's board is not N.
- **Watchdog.** The run fails if phase, overlay and turn stay unchanged for 36,000 frames. It caught the Reversal of Fortune soft lock. After 18,000 such frames the autopilot's stall policy starts (M4-03, below).
- **Oracle.** `Session::director()`: the run exits 0 only once its phase is `finished`, then stops at the next checkpoint.

**The end-of-party flags (fixed).** `TURNS_OVER` and `RESULTS_FINAL` never fired, because a party board never moves `GWSystem.turn` past `max_turn`:
- On the last turn, `mg_setup.c` sets flag (0,10) before the minigame. When the board comes back from it, `MainFunc` sees the flag and kills the board at once, without `BoardTurnNext`. The board returns to mstory3Dll, which mentDll put in its own history slot (`omOvlHisChg`) when it called the board.
- mstory3Dll event 0 is the end-of-party board result (the bonus stars). It then switches to event 1, the final ranking, which saves and returns to mode select.
- The raw reads, seed 1 (turn 20/20 throughout):

| Frame | Overlay | Raw flags before | Raw flags after the fix | Director phase |
|---|---|---|---|---|
| 235478 | `m428Dll` | 0x05 | 0x05 | minigame (`cliffhangers`, the last one) |
| 245699 | `resultDll` | 0x05 | 0x05 | board |
| 246051 | `w01Dll` (3 frames) | 0x05 | 0x05 | board |
| 246054 | none (switching) | 0x01 | 0x09 | none before, **results** after |
| 246055 | `mstory3Dll` event 0 | 0x01 | 0x09 | results (bonus stars) |
| 250789 | none (switching) | 0x01 | 0x19 | **finished** |
| 250790 | `mstory3Dll` event 1 | 0x01 | 0x19 | finished (final ranking) |

- **The fix** (`game_reads.c`, through the pure `mpt_mp4_party_flags` in `include/mpt_mp4_party_flags.h`, all from game memory, no latch in the adapter):
  - The turns are over when `turn > max_turn` (the Extra Room boards' `BoardTurnNext`), or when `turn == max_turn` and no board is in the overlay history, outside mentDll.
  - The results are final once the turns are over, no board is in the history, and the game is not on mstory3Dll's event 0.
  - Between two overlays (`omcurovl` is `DLL_NONE`), the overlay the switch is heading to (`omnextovl`, `omnextovlevtno`) stands in for the running one.
  - `IN_SETUP` still keys on the running overlay. A new party's mentDll, before `BoardSaveInit` resets the turn, is setup, not finished.
- **Known limit.** Quitting a party from the pause menu during its last turn would also read as turns over: no game state tells it apart from a finished party once the board has left the history.
- **Tests.** `test_mp4_host.cpp` checks `mpt_mp4_party_flags` on these facts and `snapshot_of` on the raw sequence above, with synthetic structs.

**Found and fixed on the way:**
- **Goooooooal failed fast in turn 19** (Party Board patch 0023). The game module failed fast with 0xC0000409, which skips the crash report; a minimal local debugger (not in the tree) gave the stack, `__report_gsfailure` ← `m423Dll fn_1_137C4`. Its six hit cases end with `VECAdd((Vec *)&arg3, &sp10, (Vec *)&arg3)`, which adds to the bytes of the pointer `arg3`, not to `*arg3`. On the console those 12 bytes land on a dead 4-byte slot and its neighbours. In the 64-bit module the slot is 8 bytes, so /GS catches the overrun. The call is left out, which keeps the console's behaviour.
  - It did not fail in every process. One run passed turn 19, and three runs of a later host build failed at the same frame.
- **The same bug in six minigames** (patch 0024). m402, m405, m429, m432, m441 and m450 share a movement helper (`fn_1_1FC4`) that ends with the same `VECAdd((Vec *)&arg0, ...)`. None has failed yet; the call is left out the same way.
- **The final ranking crashed about 20,000 frames in** (patch 0025). Party Board left mstory3Dll's `fn_1_16924` without a return ("TODO PC return value"). Its caller leaves the ranking only when it returns non-zero: START from the winner, or 300 frames for a COM winner. In the module it returned 0, so the ranking cycled through the records view, and each round left a window open. A host-side count of `winData` showed one more window in use about every 630 frames. At 32, `HuWinCreate` returned -1 and `HuWinBGTPLvlSet` read a null sprite member (0xC0000005). The function now returns its exit flag.

**Evidence, seed 1, Windows, null backend** (the final module, patches up to 0025; Party Board `98ac07fd1e8f7ab938bbb6fe75789fad99419010`, the decomp is fully matching for GMPE01_00):
- `mpt_dump_lane --boot mp4 --party --seed 1 --replay <file>` exits 0: **the Director reports `finished` at frame 250,789**, and the replay has 250,800 frames, 45,967 input runs, 4,180 checkpoints, 6,168,073 bytes and blank media. Last state hash `800fb3b3d91d29b4`.
- `--verify-replay` on that file in two more separate processes: 250,800 of 250,800 frames and 4,180 of 4,180 checkpoints (state hash and party digest) match, the last hash is `800fb3b3d91d29b4`, and the Director phase at the end is `finished`, in both.
- After merging main (PRs #60–#62), the party and both `--verify-replay` processes were rerun: the replay is byte-identical, the Director again reports `finished` at frame 250,789, and both verifications pass with last hash `800fb3b3d91d29b4`.
- **Re-recorded with zeroed heaps (2026-11-09,** ADR 0008, "Stale heap bytes", §11). Zeroing changes heap contents and so every `state_hash` from the first reused block on. The same autopilot party exits 0 with the Director at `finished` at frame 250,789. The replay has the same 250,800 frames, 45,967 input runs, 4,180 checkpoints and 6,168,073 bytes, and its last hash was `963f80f9096ee682` (superseded below). Two more separate `--verify-replay` processes each match 250,800 of 250,800 frames and 4,180 of 4,180 checkpoints and end at `finished`. The earlier hash `800fb3b3d91d29b4` is superseded. The replay file stays local and is never committed.
- **Re-recorded with the writer fixes (2026-11-09,** ADR 0008, "Gate status after the implementation" and "Gate status after the writer fixes"; patches 0029–0031 and the `HuARMalloc` ARAM hash coverage, §11 and §20). The fixes change heap contents and the hash now covers `HuARMalloc`'s blocks. The same autopilot party exits 0 with the Director at `finished` at frame 250,789; the replay has the same 250,800 frames, 45,967 input runs, 4,180 checkpoints and 6,168,073 bytes, and its last hash is now **`43198e9219fd8f37`**. Two more separate `--verify-replay` processes each match 250,800 of 250,800 frames and 4,180 of 4,180 checkpoints, end at `finished` and report the same last hash. `963f80f9096ee682` is superseded. The replay stays local.
- **Re-recorded with the M4-03 patches (2026-11-10,** patches 0032–0034; §20, layout gate, "Results with the M4-03 fixes"). The same autopilot party exits 0 with the Director at `finished` at frame 250,789; the replay has the same 250,800 frames, 45,967 input runs, 4,180 checkpoints and 6,168,073 bytes, and its last hash is now **`7c843ad641d303eb`**. Two more separate `--verify-replay` processes each match 250,800 of 250,800 frames and 4,180 of 4,180 checkpoints, end at `finished` and report the same last hash. `43198e9219fd8f37` is superseded. Seed 1 on the default board never stalls for 18,000 frames, so the stall policy never acts in it. The replay stays local.
- **Re-recorded with the msm header fix (2026-11-12,** patch 0035; §11, §20 "Results with the M4-15 fix"). The same autopilot party exits 0 with the Director at `finished` at frame 250,789; the replay has the same 250,800 frames, 45,967 input runs, 4,180 checkpoints and 6,168,073 bytes, and its last hash is now **`1be7b9fb2eccad06`**. Two more separate `--verify-replay` processes each match 250,800 of 250,800 frames and 4,180 of 4,180 checkpoints, end at `finished` and report the same last hash. `7c843ad641d303eb` is superseded. The replay stays local.
- **Re-recorded with the stream refill fix (2026-11-16,** patch 0036; §23, §20 "Results with the stream refill fix"). The same autopilot party exits 0 with the Director at `finished` at frame 250,789; the replay has the same 250,800 frames, 45,967 input runs, 4,180 checkpoints and 6,168,073 bytes, and its last hash is now **`53b119efe3f28194`**. Two more separate `--verify-replay` processes each match 250,800 of 250,800 frames and 4,180 of 4,180 checkpoints, end at `finished` and report the same last hash. `1be7b9fb2eccad06` is superseded. The replay stays local.
- The run before patch 0025, with a byte-identical replay, also passed two separate `--verify-replay` processes. Patch 0025 changes only the final ranking, after the replay's last frame.
- The party: 20 turns and 25 minigames, 21 of them different. Player 1 (human) ends with 1 star and 67 coins; the COM players with 2/70, 0/116 and 2/160.

| Turn | Starts (frame) | Minigames, `mp4.minigame.*` (start frame) |
|---|---|---|
| 1 | 3,914 (setup) | `paratrooper_plunge` 11,181 |
| 2 | 14,515 | `long_claw_of_the_law` 18,591 |
| 3 | 21,151 | `fruits_of_doom` 22,787, `team_treasure_trek` 35,431 |
| 4 | 40,009 | `cliffhangers` 46,697 |
| 5 | 53,934 | `fish_n_drips` 60,238 |
| 6 | 64,318 | `money_belts` 72,323 |
| 7 | 75,495 | `candlelight_flight` 81,200 |
| 8 | 83,545 | `hide_and_go_boom` 93,181 |
| 9 | 97,621 | `the_great_deflate` 108,389 |
| 10 | 111,323 | `reversal_of_fortune` 111,808, `pair_a_sailing` 122,268 |
| 11 | 126,279 | `right_oar_left` 133,193 |
| 12 | 139,109 | `blame_it_on_the_crane` 144,994 |
| 13 | 148,598 | `mario_medley` 154,935 |
| 14 | 159,841 | `reversal_of_fortune` 160,928 and 166,079, `team_treasure_trek` 170,853 |
| 15 | 175,576 | `toads_quick_draw` 180,071 |
| 16 | 186,368 | `hop_or_pop` 195,778 |
| 17 | 199,803 | `bowsers_bigger_blast` 203,185, `candlelight_flight` 215,255 |
| 18 | 218,745 | `take_a_breather` 224,503 |
| 19 | 226,450 | `goooooooal` 229,786 |
| 20 | 232,627 | `cliffhangers` 235,477; results 246,053, finished 250,788 (event frames) |

- **Cost.** About 9 minutes of wall time for the party on this machine; `stateHash()` 6.3–10.3 ms per call (means of 4,180, two runs).
- Peak arena use and save/load times: §20, Memory.
- **Past `finished`** (diagnosis, resumed with `--party-resume` from a state saved at frame 250,000 on the final module; not evidence):
  - The ranking waits for the winner's pad. A on it opens the records view, which only B leaves, so the autopilot now pulses START on mstory3Dll once the phase is `finished`. That change acts only after the replay's last frame.
  - The ranking ends at frame 251,623, and mode select follows at 251,624, still `finished`. The autopilot then starts a new party: mentDll at 252,125 reads as setup (turn still 20/20), `BoardSaveInit` resets the turn to 1/20 at 254,264, the board starts at 254,657, and its first minigame (m419) runs and ends.
  - The window count stays at one through the ranking. Before patch 0025 it grew by one about every 630 frames.
- **Determinism across builds.** Patches 0023 and 0024 change only code in minigames that load later, yet the state-hash trail moved from the first printed checkpoint (frame 6,000) on, while the Director's events stayed identical. Patch 0025 did not move it. So a trail belongs to one module build, which a fixture's build pin already requires; why a code-only REL change moves early hashes is still to find (qa, architect).

**Open:**
- E1 still needs 4 human seats through the launcher UI, and E3c the Linux run of this replay.
- **Crash reports (M4-04, done).** `mpt_dump_lane --supervise [--crash-report FILE]` reports every crash, `/GS` fail-fasts included: the status and fail-fast code, mode, seed, board, frame, phase, turn, overlay, the last checkpoint's frame and hash, and the stack as `module+offset symbol+offset` (`mp4_game.dll` named from its COFF symbol table), with the `culprit` frame for a /GS failure. No game text or paths. Run the M4-03 sweep under it. Without `--supervise` the in-process hooks still cover exceptions, `abort`, `std::terminate` and invalid parameters. The Linux path (`__stack_chk_fail`) builds but has not run yet (M4-U2). See `tests/dump_lane/README.md`.

### M4-03: the seed 2 crash, the board sweep and the audits (`m4-03-board-sweep`)

From `main` at `4069d6a` (PR #77 and #76 merged; ADR 0008, "Gate status after the writer fixes", points 2 and 3).

**The seed 2 crash (fixed, Party Board patch 0032).**
- **Report** (`--supervise --crash-report`, `main` at `4069d6a`): `EXCEPTION_ACCESS_VIOLATION` reading `0x00004000b6f43440` at `byteswap_animbankdata+0x0`, stack `HuSprAnimRead+0x92` ← `Hu3DAnimCreate+0x250` ← `mpt_rel_w01Dll__RoundItemInit+0x38e` ← `RoundItemMain+0x13c`, seed 2, turn 11/20 on Toad's Midway Madness. The frame being stepped was 112,316; 111,514 is the turn's start (the party read's last change), which is the frame §20 quoted.
- **Call path in the pin** (`98ac07f`): `RoundItemEvent` → `RoundItemMain` (`REL/w01Dll/main.c:465`, the item carousel) → `RoundItemInit` (`REL/w01Dll/mg_item.c:116`), which loads `w01` data 28 once into `itemAnim` (line 128) and calls `Hu3DAnimCreate(itemAnim, …, "item01")` for each of its four item models (line 182) → `Hu3DAnimCreate` (`game/hsfanim.c:42`) → `HuSprAnimRead` (`game/sprman.c:218`) → `byteswap_animbankdata` (`port/byteswap.cpp:929`).
- **Cause: a second conversion of one anim file.** On the console `HuSprAnimRead` relocates the file in place, and a second read finds it relocated and shares it (`useNum++`). Party Board's `BYTESWAPPING` path converts into a new `ANIMDATA` and swaps the file's header, banks, patterns, bitmaps, frames and layers in place, with no mark. The second `Hu3DAnimCreate` therefore swapped the header back to big-endian and read the banks from the byteswapped offset 0x14000000 past the file. Class: a port bug (a non-idempotent in-place conversion), not uninitialised memory and not layout-dependent; both lanes and `main` crashed at the same address. Even without the crash, the frames and layers, which the first `ANIMDATA` points into, would have been swapped back under it.
- **Fix.** Patch 0032: the first read leaves `MPT_ANIM_FILE_CONVERTED` and the converted `ANIMDATA`'s address in the file's header (dead after conversion), and a later read of the same file shares that `ANIMDATA` with `useNum++`, as the console shares the relocated file. The mark's first half is the file's `bankNum` and `patNum`, never negative on disc. `HuDataSelHeapReadNum` reads into a fresh block every time, so a re-loaded file converts on its own. The round item's kill path then frees the shared `ANIMDATA` on its third `Hu3DAnimKill` and reports one `HuMem>memory free error` on the fourth, as the console's shared, refcounted file does.
- **The mark outlives the freed `ANIMDATA` (qa, #79; M4-15).** The last `Hu3DAnimKill` or `HuSprAnimKill` frees the shared `ANIMDATA` but leaves the mark and the pointer in the file's header, since the kill paths see only the `ANIMDATA`. A later read of that same buffer would share freed memory. Traced, it cannot happen in code that works on the console: there the `ANIMDATA` is the file, and the last kill frees the file itself (`HuMemDirectFree(anim)`), so reading the buffer again is already a use after free. Every `HuData` read (`HuDataRead`, `HuDataReadNum`, `HuDataSelHeapReadNum`, `HuDataReadNumHeapShortForce`) decodes into a newly allocated buffer and overwrites its header, so a new read of the file never finds a mark; the callers in the pin (`RoundItemInit`, `filesel.c`'s stars) read the file again each time. No code change; the reason is in the patch's comment.
- **Test.** `mp4_sprman_anim` builds `sprman.c` and `byteswap.cpp` from the patched pin and reads a hand-made big-endian anim file (one bank of two frames, one pattern, one bitmap) four times: every read returns the same `ANIMDATA` with `useNum` 0 to 3 and host-order banks, frames and layers, and a second copy converts on its own. Without the patch the test dies on the second read with an access violation (exit 0xC0000005, before it prints anything; rerun in M4-15 by reverse-applying the patch in a module build).

**The fixed-array audit (the patch 0022 pattern).** A callee that writes a fixed number of entries into a caller's array overruns a smaller array; with `/GS` the overrun fails the stack cookie. Searched in the pin: every `memset` on a pointer parameter and every write through an array parameter in `src/game` and `src/REL`, then every caller of each callee whose count does not come from the caller.

| Callee | Writes | Callers and their arrays | Result |
|---|---|---|---|
| `BoardSpaceLinkTargetListGet` (`game/board/space.c:254`) | clears and fills `BOARD_SPACE_LINKMAX` (4) | `w01Dll/main.c:746` `linkTbl` (patch 0022: 4), `:2460` `linkTbl[4]`; `w04Dll/boo_event.c:672`, `:802` `sp10[4]`, `:879`, `:1033` `sp8[4]`; `w05Dll/dolphin.c:275` `sp14[4]`; `game/board/player.c:1358` `&spAC[n]`, `spAC` 10 entries with n ≤ 4 | all fit |
| `BoardSpaceLinkTypeListGet` (`space.c:236`) | at most `BOARD_SPACE_LINKMAX` | no caller in the pin | — |
| `CreateBallMdl` (`game/board/boo.c:542`) | 2 | `BooEventWork.unk04[2]` | fits |
| `SetCharWheelChars` (`game/board/char_wheel.c:432`) | 4 (reads 4) | `wheelChars[4]` | fits |
| `fn_1_10FE8` (`m427Dll/player.c:991`) | 7 | `map.c:568` `sp8[8]`, `:1724` `spC[8]` | fit |
| `fn_1_8250` (`m453Dll/map.c:882`) | up to 6 | `sp8[6]` twice | fit |
| `fn_1_8274` (`m403Dll/scene.c:662`) | up to 6 | `sp8[6]` twice | fit |
| `fn_1_BB74` (`w03Dll/mg_item.c:933`) | `ARRAY_COUNT(unk3)` = 3 | `lbl_1_bss_194[3]` | fits |
| the shuffles `fn_1_AC08` (m409), `fn_1_62D0` (m424), `fn_1_7DD8` (m438), `fn_1_6638` (m431) | the caller's count | `sp8[8]` with at most 4 players, `sp8[10]` with 8, `sp8[48]` with 10, `spA[10]` with at most 10 | fit |

The other writes through array parameters are matrices, vectors and spline tables sized by the caller's own count argument. No overrun found besides patch 0022's, which was already fixed.

**The MTXInverse audit (the patch 0029 pattern).** `MTXInverse` and `MTXInvXpose` (aurora `lib/dolphin/mtx/mtx.c`, as on the console) return 0 and leave the output untouched when the matrix is singular, which a model or a bone scaled to 0 makes it. A site is real when the output can be uninitialised there and then reaches game memory. The pin has 90 calls outside the SDK: 4 are `kerent.c` jump-table entries, and the other 86 are listed here by class (line numbers in the pin with patches 0001–0031).
- **Patched, Party Board patch 0033** (zeroed at function entry on `MPT_PC`, so a path that set them first behaves as before):
  - `game/hsfdraw.c:242` and `:253`, `objMesh`'s billboard paths: `mtx` is set first only on the non-envelope path that applies the transform, and `MTXConcat` stores the result in `DrawObjData[].matrix` and the object's constant data.
  - `game/EnvelopeExec.c:364`, `SetEnvelop`'s multi-weight loop: `sp170` is set first only by the single-weight loop, and the normals go through it into the envelope's normal buffer (`sp110` at `:327` is cleared too, as a precaution).
  - `REL/m442Dll/main.c:1740`, `fn_1_5EDC` (a model hook): `spC` is concatenated into the hook's matrix, which is `DrawObjData[].matrix`.
- **Patched before:** `game/EnvelopeExec.c:451`, `SetRevMtx` (patch 0029).
- **Output set first or persistent** (a failure keeps a defined value): `EnvelopeExec.c:170` (into `MtxTop`, a zeroed heap block), `:242` and `:325` (in place after `MTXConcat`), `:244` and `:327` (inputs of unit scale), `:454` (`sp8` set by `MTXCopy`, unused after); `hsfex.c:445` and `m438Dll/map.c:305` (in place); `mapspace.c:925` (the caller's matrix); `m417Dll/player.c:290`, `:1075` (a work field); `m417Dll/water.c:580` (`spC8` set by `MTXScale` first); `hsfman.c:230`, `:237` (`Hu3DCameraMtxXPose`, a global).
- **Camera matrices** (a view matrix is rigid and never singular): `board/item.c:1045` (from `Hu3DCameraSet`), `hsfdraw.c:2324`, `:2335`, `:2464`, `:2475`; `instDll/main.c:1093`, `:1162`; `m405Dll/main.c:954`, `:960`, `:1047`; `m410Dll/player.c:1104`; `m415Dll/map.c:141`; `m417Dll/water.c:779`, `:787`; `m421Dll/player.c:1772`, `:1777`; `m423Dll/main.c:4572`; `m427Dll/map.c:2417`; `m430Dll/water.c:1221`, `:1228`, `:1334`, `:1455`; `m434Dll/map.c:283`, `:290`, `:426`; `m438Dll/fire.c:152`; `m455Dll/stage.c:493`, `:557`; `m456Dll/stage.c:494`, `:558`; `resultDll/battle.c:656`; `ztardll/main.c:1528`.
- **The board map's matrix** (`MapMT`, the board's own transform at scale 1): `mapspace.c:99`, `:156`, `:760`.
- **GX only** (the output goes to `GXLoadNrmMtxImm`, a texture matrix or vertices built on the stack for one draw: render state, not game state): `hsfdraw.c:2321`, `:2367`, `:2460`, `:2507`; `instDll/main.c:1111`; `m405Dll/main.c:941`; `m406Dll/map.c:1107`; `m408Dll/stage.c:845`, `:909`; `m409Dll/cursor.c:181`, `:242`; `m415Dll/map.c:211`, `:275`; `m417Dll/water.c:767`; `m423Dll/main.c:848`, `:1238`; `m425Dll/main.c:1538`; `m425Dll/thwomp.c:2087`; `m427Dll/map.c:2366`; `m427Dll/player.c:1673`; `m428Dll/player.c:2168`; `m430Dll/player.c:1605`; `m430Dll/water.c:1200`; `m438Dll/fire.c:225`, `:298`; `m440Dll/main.c:1874`, `:2078`; `m442Dll/main.c:1536`; `m455Dll/stage.c:550`; `m456Dll/stage.c:551`.
- **Not in a party:** `modeltestDll/main.c:598`, `:800` (a model viewer REL).

**The board wheel (`--party-board`).** mentDll's wheel reads the stick (|x| of 50 or more) or the d-pad bits of `HuPadBtn`, but Party Board's `pad.c` masks the d-pad out of `HuPadBtn`, so only the stick moves it. It takes a direction once it has been idle for 21 frames and its board models have settled, then waits again. Measured on seeds 1 and 2 with the autopilot's A pulses before it: the wheel first takes the stick 1,448 frames after mentDll starts (`kBoardWheelFrom`). The autopilot gives one 4-frame stick pulse every 60 frames, one per move (30 frames is too short), presses no A in the window, and A confirms 30 frames after the last pulse. `--party-wheel FROM:TO` overrides the window. The lane fails a run whose party read names another board than `--party-board`, so a run that exits 0 played its board. Bowser's Gnarly Party is locked in the Phase 1 flag set: the sweep reaches it with `--party-unlock-bowser` (`MPT_MP4_BOOT_UNLOCK_BOWSERS_GNARLY_PARTY` through `Mp4Runtime::setTestGameplayFlags`, test only), which changes the boot flags and so the state hash, and no replay records it.

**The soft locks: the lottery waits for a human seat's stick.** The first sweep (A pulses only on the board) soft-locked 6 of the 18 parties, all in phase `board` with the turn unchanged for 36,000 frames, and three of them locked a second time once the first lock was cleared:

| Board, seed | First lock (turn, since frame) | Second lock (turn, since frame) |
|---|---|---|
| `toads_midway_madness`, 2 | 14, 142,937: ball game | 20, 237,425: scratch ticket |
| `goombas_greedy_gala`, 2 | 3, 24,689: ball game | 18, 196,915: scratch ticket |
| `boos_haunted_bash`, 1 | 15, 146,895: scratch ticket | — |
| `koopas_seaside_soiree`, 1 | 5, 47,852: ball game | 6, 75,611: scratch ticket |
| `bowsers_gnarly_party`, 1 | 8, 85,802: scratch ticket | — |
| `bowsers_gnarly_party`, 3 | 16, 146,537: scratch ticket | — |

- **How they were found.** `--dump-arena FRAME FILE` at a frame inside the lock, then a scan of the coroutine stacks in the dump for return addresses, named with `llvm-nm -n` on the installed `mp4_game.dll` of the same build. Every scanned lock has the turn's process suspended under `BoardPlayerTurnExec` → `BoardPlayerTurnMoveExec` → `BoardSpaceWalkExec` → `BoardLotteryExec`, inside `ExecLottery` (`game/board/lottery.c`). The ball-game locks were confirmed with a local debug build that printed the handle's state: the player was the human seat and the handle's speed 0. Scanned or printed: both `goombas_greedy_gala` locks, the `toads_midway_madness` turn-14 lock and the `koopas_seaside_soiree` and `bowsers_gnarly_party` seed-1 ticket locks. The other four are classed by which stick pattern cleared them.
- **Cause: the game, not the port.** The lottery is shared board code, and the locks were on five of the six boards. It plays one of two games, and both wait on a human player's stick with no time limit and no button exit:
  - the ball game (`ExecBallGame`) until the stick has cranked the handle one revolution. A sample counts only when an axis is past 57.6 of 72, it differs from the previous sample and the angle grew;
  - the scratch ticket (`ExecScratchTicket`, `TicketUpdate`) until the hand, which moves 8 pixels a frame toward the stick inside a 192 by 96 pixel box and clears a 17 by 17 block of the ticket's bitmap around itself, has cleared all but 300 of its pixels.

  A COM player plays both from input tables. The autopilot's seat is human and never moved the stick on the board, so it waited there forever, as a player who puts the pad down would on the console. No patch: the game code is right.
- **Fix: the autopilot's stall policy** (`party_autopilot`, `tests/dump_lane/dump_lane.h`, unit-tested). After 18,000 frames with no party change (`kPartyStallFrames`; the longest normal stretch is 10,480) the autopilot keeps its A pulses and adds, outside minigames:
  - a B pulse (4 frames every 60, between the A pulses), for a menu that only B leaves. It was added first, did not clear any lock above, and no run is known to need it;
  - the stick, in a 720-frame cycle: 264 frames turning clockwise through up, right, down and left, two frames each (the crank), 40 frames up and left (the hand to the corner of the box), then rows of 25 frames across and one frame down (the ticket; 13 rows cover it and 16 fit).

  The pad is still a pure function of the frame, phase, overlay and frames since the last party change, so a run stays reproducible. A run that never stalls for 18,000 frames gets the same pad as before; seed 1 on the default board is one (same `finished` frame, 250,789). A lottery the human seat lands on costs a run the 18,000 frames before the policy starts, and the watchdog (36,000) still catches a real lock.
- **Party Board patch 0034 (hardening, not a lock's cause).** Found by reading `ExecBallGame` while tracing the locks: the COM's crank index `comInputPos` runs past the `{ 0x7F, 0x7F }` terminator of `comInputBall` and reads whatever follows the table. On `MPT_PC` the index now stays on the terminator. No lock above came from it (they were all the human seat). No synthetic test: `ExecBallGame` is static and needs the whole board to run, so only the sweep exercises it.
- **`--party-resume` and `/GS`.** `--party-save-at` and `--party-resume` were used to probe the locks. A state saved in one process and resumed in another can fail fast with 0xC0000409 when a game function that was suspended at the save returns: its frame holds a stack cookie made with the saving process's `__security_cookie`, which Windows makes anew every time the module is loaded. Seen while probing; it is why `--party-resume` is diagnosis only. The same holds for any save state loaded in a fresh process (the original E5, a netplay resync), which is not solved here. **Ruling (ADR 0008, "Stack guard values in save states"; `docs/phases/phase-1.md`, M4-I):** fresh-process save states with stack guards are a Phase 2 item. Phase 1 has one check, M4-16: the in-process E5 round trip (save, destroy the runtime, create a fresh one, load, continue) at a frame where a guarded game function is suspended. `architect` read that destroying the last `Mp4Runtime` unloads the module and that Windows makes a new cookie on the reload, so that round trip may fail the same way. M4-16 ran it, and it did; the module now stays loaded for the process, and the round trip passes (§11, "E5 in process (M4-16)").

**The sweep: 6 boards, seeds 1 to 3, 20 turns each (2026-11-10).** Each party ran in its own process on the OFF lane as `--supervise --crash-report <file> --boot mp4 --party --seed S --every 1000 --party-board N` (with `--party-unlock-bowser` for board 5), null backend, Windows. "Result" is the lane's exit code; the lane exits 0 only when the Director reports `finished`, no halt, fault or watchdog stop occurred and the party read names the requested board. "Finished" is the frame at which the Director reported `finished`; the hash is the state hash at the run's last frame.

| Board | Seed | Result | Finished at frame | Last frame | Final hash |
|---|---|---|---|---|---|
| `mp4.board.toads_midway_madness` | 1 | exit 0 | 250,789 | 251,000 | `e29d9d9882006f0f` |
| `mp4.board.toads_midway_madness` | 2 | exit 0 | 272,239 | 273,000 | `a4d0e48009c45c62` |
| `mp4.board.toads_midway_madness` | 3 | exit 0 | 277,797 | 278,000 | `4e3163ec843e68d4` |
| `mp4.board.goombas_greedy_gala` | 1 | exit 0 | 261,567 | 262,000 | `7043809ab2f1d11c` |
| `mp4.board.goombas_greedy_gala` | 2 | exit 0 | 269,720 | 270,000 | `dcdbce28bc5ee46b` |
| `mp4.board.goombas_greedy_gala` | 3 | exit 0 | 257,779 | 258,000 | `f9dcf9327252eacf` |
| `mp4.board.shy_guys_jungle_jam` | 1 | exit 0 | 197,498 | 198,000 | `0a045d7f3f5f3220` |
| `mp4.board.shy_guys_jungle_jam` | 2 | exit 0 | 213,242 | 214,000 | `043b26cda8569ed6` |
| `mp4.board.shy_guys_jungle_jam` | 3 | exit 0 | 210,648 | 211,000 | `dc6dfa3c0c71efd7` |
| `mp4.board.boos_haunted_bash` | 1 | exit 0 | 236,183 | 237,000 | `3a80aaed2df16887` |
| `mp4.board.boos_haunted_bash` | 2 | exit 0 | 220,671 | 221,000 | `d36e81fa5d94bf61` |
| `mp4.board.boos_haunted_bash` | 3 | exit 0 | 251,099 | 252,000 | `038f515212040845` |
| `mp4.board.koopas_seaside_soiree` | 1 | exit 0 | 351,839 | 352,000 | `db72f880356c3121` |
| `mp4.board.koopas_seaside_soiree` | 2 | exit 0 | 278,204 | 279,000 | `0701a47d17db189d` |
| `mp4.board.koopas_seaside_soiree` | 3 | exit 0 | 256,597 | 257,000 | `12da01cb737247ee` |
| `mp4.board.bowsers_gnarly_party` | 1 | exit 0 | 275,834 | 276,000 | `dc3531d8d68ebbc5` |
| `mp4.board.bowsers_gnarly_party` | 2 | exit 0 | 214,576 | 215,000 | `915c2aec4b487a70` |
| `mp4.board.bowsers_gnarly_party` | 3 | exit 0 | 236,060 | 237,000 | `1dcc4c732182500a` |

- **18 of 18 parties finish.** No halt, fault, `/GS` fail-fast or watchdog stop, and no run wrote a crash report. Every log names its board in the party read and ends at turn 20/20.
- The six parties that soft-locked before the stall policy (table above) now finish. Each lottery stall costs a party at least 18,000 frames, and `koopas_seaside_soiree` seed 1, with two of them, is the longest. The twelve that never stalled finish at the same frame as in the first sweep.
- The hashes are from one process per party. The cross-process and cross-layout checks are the M1 runs, the replay verification and the layout gate (§20), on the default board with seeds 1 and 2; the gate's seed 1 and seed 2 parties finish at the same frames as this table's.
- **Not covered:** more than 3 seeds per board; a human seat that plays (the autopilot presses A and, on a stall, B and the stick); the Linux lane (WSL is not installed on this machine).

**The layout gate after these fixes** is recorded in §20 ("Results with the M4-03 fixes"): met on Windows for seeds 1 and 2 over the full party (ADR 0008, "Gate status after M4-03"). The msm header overrun is fixed in M4-15 (§11), whose gate rerun is §20 "Results with the M4-15 fix"; E3c acceptance still waits for the Linux lane.

## 22. E1 sign-off checklist (M4-10)

E1 is the full local party (Phase 1 plan §4). The user plays it and fills in this checklist: on Windows for M4-11, and later on Linux under WSL2 for M4-14 (on hold, ruling M4-U2). An agent then re-verifies the replay (section D). qa approves the checklist.

**Keep it public-safe.** Only the values asked for below go into this file. Never write a dump path, a disc ID, a user folder path, game text, screenshots or captures here. Keep those in your own local notes. The replay, its summary and the build cache stay on your computer and are never committed.

**How to fill it in.** Put `yes`, `no` or the value in the cell for your OS. A `no` stops the sign-off: write a short note in the Notes list at the end and tell the agents.

### A. Setup before you play

1. Build the shell with the GX backend that draws to the screen and the MP4 title registered. Follow "Building it" in `runtimes/gc_wii/gx/README.md`: fetch aurora, build Dawn with `runtimes/gc_wii/gx/tools/build_dawn.cmake`, then configure with a `windows-clang-*` preset and `-DMPT_GX_BACKEND=dawn`.
2. Make a new, empty folder for this run. This is `<user_dir>` below. Do not reuse an old user folder: a fresh one means no old save, no old input profile and no old build cache.
3. Start the launcher with that folder. Use the executable you built (`<exe>`):

   ```
   <exe> --user-dir <user_dir>
   ```

4. Connect the keyboard and exactly one gamepad. No other controllers.

### B. The checklist

| # | Check | Windows (M4-11) | Linux, WSL2 (M4-14, **on hold**) |
|---|---|---|---|
| **Environment** | | | |
| 1 | Date played | 2026-11-18 | on hold |
| 2 | OS name, version and build (Linux: the distribution, kernel and WSL version) | Windows 11 Pro 10.0.26200 (build 26200) | on hold |
| 3 | CPU, GPU and RAM | AMD Ryzen 5 3600, AMD Radeon RX 5700, 16 GB | on hold |
| 4 | GPU driver version | 32.0.21045.16010 | on hold |
| 5 | Gamepad model and how it is connected (USB or Bluetooth; Linux: forwarded with usbipd-win) | Logitech F710, wireless USB receiver, XInput mode | on hold |
| 6 | Repository commit the shell was built from (`git rev-parse --short HEAD`), and whether the tree was clean | `c89eea0`, clean | on hold |
| 7 | Shell version (`<exe> --version`) | `mario-party-together 0.0.0` | on hold |
| 8 | CMake preset and `MPT_GX_BACKEND` value | no preset: Ninja, clang-cl, RelWithDebInfo, triplet `x64-windows-static-md`; `MPT_GX_BACKEND=dawn` | on hold |
| **Import, through the launcher** | | | |
| 9 | `<user_dir>` was new and empty before the launcher started | yes | on hold |
| 10 | Library: "Add game dump", then your own `.iso` picked from the list, the file dialog or by dropping it on the window | yes | on hold |
| 11 | The dump matched the supported MP4 entry (record the disc ID and path in your own notes only) | yes | on hold |
| 12 | The build finished in the launcher, and the library lists the game | yes | on hold |
| 13 | Build ID: `runtime_build_version` in `<user_dir>/cache/builds/mp4/<key>/manifest.json` (it starts with `mp4-`) | `mp4-63a38f64aac1f8ea` | on hold |
| 14 | `unpinned` is `false` in that manifest. An unpinned build cannot sign off E1. | yes | on hold |
| 15 | The manifest's sources match the `decomp.lock` rows of section C (Party Board, musyx, aurora). Dawn is covered by the build ID only. | yes | on hold |
| **Seats and devices (ruling M4-U1)** | | | |
| 16 | Players screen: "Keyboard + controller, 2 computer players" chosen, then "Start the party" pressed | yes (the gamepad could not reach "Start the party"; fixed in #84) | on hold |
| 17 | Seat 1 is the keyboard and seat 2 is the gamepad. Both are set to human in the game's own party setup (only the first player starts as human there). | yes | on hold |
| 18 | Seats 3 and 4 are computer players, set in the game's own party setup | yes | on hold |
| **The party** | | | |
| 19 | Board played: a regular party board (any of the first six in §12, not an Extra Room board). Write its content ID. | `mp4.board.shy_guys_jungle_jam` | on hold |
| 20 | Number of turns chosen in the game's setup (at least 10) | 10 | on hold |
| 21 | Every turn was played, through the last minigame, the bonus stars and the final ranking | yes | on hold |
| 22 | The pause overlay showed "Turn x/y", and Resume worked | yes | on hold |
| 23 | Audio played through the whole party | yes | on hold |
| 24 | No crash, hang, freeze or soft-lock. If you had to stop, write where in the Notes. | yes | on hold |
| 25 | You did **not** quit the party before the final ranking ended, neither from the game's own pause menu nor with the launcher's "Quit to library" (see the caveat in section E) | yes | on hold |
| 26 | After the final ranking, the game went back to its own mode select. You then opened the launcher's pause overlay (Escape, or the gamepad's Guide/Home button) and chose "Quit to library", without starting a new party. Not "Exit Mario Party Together". | yes (opened with Escape; a gamepad way, Back+Start, came in #85) | on hold |
| 27 | The launcher then showed the end-of-party screen: the heading "Party finished" and the line "Turns played: N of N." | yes | on hold |
| 28 | That screen also showed "Replay saved in your user folder: replays/…" | yes | on hold |
| **The replay and summary (ruling M4-C)** | | | |
| 29 | `<user_dir>/replays/` holds `mp4-<seed>-<n>.mptr` and `mp4-<seed>-<n>.summary.txt`. Keep both. Do not move, rename or delete them, and keep `<user_dir>` as it is. | yes | on hold |
| 30 | Summary `end` is `ok` | `ok` | on hold |
| 31 | Summary `phase` is `finished` | `finished` | on hold |
| 32 | Summary `turn` is N/N (both numbers equal, N at least 10, same as row 20) | 10/10 | on hold |
| 33 | Summary `board` (same content ID as row 19) | `mp4.board.shy_guys_jungle_jam` | on hold |
| 34 | Summary `runtime_build` (same as row 13), `runtime_unpinned` (`false`), `seed`, `frames` and `events` | `mp4-63a38f64aac1f8ea`, `false`, seed 7618890121469772264, 145,228 frames, 58 events | on hold |
| **Agent re-verification (section D)** | | | |
| 35 | Process 1: exit code 0, `party: phase finished`, `replay OK` with the frame and checkpoint counts | exit 0, `party: phase finished, turn 10/10, 58 events`, `replay OK: 145228 frames, 2420 checkpoints` | on hold |
| 36 | Process 2: the same | the same | on hold |
| 37 | The two outputs are identical (yes/no). Write the last trail line (frame and hash). | yes; last trail line `145200 6929d7fa9f19ade2` | on hold |
| 38 | Agent and commit that ran the check | coordinator (Claude), shell built from `c89eea0` | on hold |
| **Sign-off** | | | |
| 39 | Tester sign-off (yes/no) | yes | on hold |
| 40 | qa-reviewer approval | yes (PR #86) | on hold |

Notes (one line each, no paths or game text):
- Windows: played 2026-11-18 with the Dawn preview build. Bugs found, none blocking E1: the gamepad could not reach the Players screen's lower buttons (fixed in #84); no gamepad way to the pause overlay (Back+Start added in #85); black boxes under characters in Money Belts, a black minigame description background, and a white light blob in Right Oar Left? (rendering, being fixed); the pad lost power after its Vibration toggle and came back after its batteries were reseated (the host sends no rumble).
- Linux:

### C. Pins to compare against (row 15)

These are the pins on `main` when this checklist was written. If the lock files have changed since, the lock files win: compare against `runtimes/gc_wii/mp4/decomp.lock` and `runtimes/gc_wii/gx/dawn.lock` at the commit in row 6.

| Source | Commit | Lock |
|---|---|---|
| Party Board | `98ac07fd1e8f7ab938bbb6fe75789fad99419010` | `runtimes/gc_wii/mp4/decomp.lock` |
| musyx | `86851a3a8d51788d166ea42d137b49526f570400` | same |
| aurora | `514339438178ef2bed1b14e5149d90ece0c6e0cc` | same |
| Dawn | `13abc3bc8ea2d3c2050f9e77a12d012108ceee24` | `runtimes/gc_wii/gx/dawn.lock` |
| Party Board patch set | every patch in `runtimes/gc_wii/mp4/patches/partyboard/` at the commit in row 6 (0001 to 0036 as of the Dawn preview, §23) | covered by the build ID |

The build manifest's `sources` list comes from `decomp.lock` only (Party Board, musyx, aurora). Dawn (`dawn.lock`) and aurora's GX pin (`runtimes/gc_wii/gx/aurora.lock`, the same aurora commit) are not in that list, but both are hashed into the build ID.

The build ID (row 13) already covers the pins, the patch sets, the toolchain and the module's own sources (`runtimes/gc_wii/mp4/build_id.cmake`). Rows 13 and 34 must agree.

### D. Agent re-verification: the replay headless in 2 separate processes

The agent runs this on the tester's computer, with the same executable, dump and user folder as the party. The tester gives the paths to the agent locally; they never go into the repository.

Why each option is needed (`core/shell/app.cpp`, `run_verify_replay`):
- `--verify-replay FILE` runs headless: no window, no audio and no UI.
- `--dump` is required for MP4. Without it the run fails with `--dump is required to run mp4`. The build cache in the user folder is reused, so nothing is rebuilt.
- `--user-dir` must be the same folder. It holds the build cache and the replay media store (`replays/media/`).
- `--verify-replay` cannot be combined with `--title` or `--replay`.

PowerShell, two separate processes:

```powershell
$exe  = "<exe>"
$user = "<user_dir>"
$dump = "<path to the dump>"
$rep  = "$user\replays\mp4-<seed>-<n>.mptr"
$out  = "<scratch folder outside the repository>"

& $exe --verify-replay $rep --dump $dump --user-dir $user > "$out\verify-1.txt"; "exit $LASTEXITCODE" | Tee-Object -FilePath "$out\verify-1.txt" -Append
& $exe --verify-replay $rep --dump $dump --user-dir $user > "$out\verify-2.txt"; "exit $LASTEXITCODE" | Tee-Object -FilePath "$out\verify-2.txt" -Append
Compare-Object (Get-Content "$out\verify-1.txt") (Get-Content "$out\verify-2.txt")
Get-Content "$out\verify-1.txt" -Tail 3
```

Linux (when M4-14 is no longer on hold):

```sh
out="<scratch folder outside the repository>"
"$exe" --verify-replay "$rep" --dump "$dump" --user-dir "$user" > "$out/verify-1.txt"; echo "exit $?" | tee -a "$out/verify-1.txt"
"$exe" --verify-replay "$rep" --dump "$dump" --user-dir "$user" > "$out/verify-2.txt"; echo "exit $?" | tee -a "$out/verify-2.txt"
cmp "$out/verify-1.txt" "$out/verify-2.txt" && tail -n 3 "$out/verify-1.txt"
```

`$out` is a scratch folder outside the repository, so the outputs never land in the tree. Each command also appends its exit code to its output file as the last line, `exit <code>`, so the file records it for row 35 and the comparison covers it. The run is not read-only: it opens the GX pipeline cache in the build cache under `<user_dir>` (the module build's `gx-cache` folder, §23) and may add pipelines to it. That does not change what the replay checks; the cache only saves compile time. What passes:
- Both runs exit 0 (`exit 0` is the last line of both files). Any checkpoint mismatch exits 1 and prints the error on stderr.
- Each output ends, before the `exit` line, with the hash trail (one `<frame> <state_hash>` line per checkpoint), then `party: phase finished, turn N/N, E events`, then `replay OK: F frames, C checkpoints`.
- `Compare-Object` prints nothing (`cmp` is silent): the two processes give the same output.
- The phase, turn and event count match the summary (rows 31, 32 and 34). The frame count matches the summary's `frames`.
- Passing means the replay matches the trail recorded live, because each checkpoint in the file is checked as it plays.

Record rows 35 to 38. Do not commit the replay, the summary or the outputs.

### E. Known caveat: play through to the results

Quitting a party from the game's own pause menu during its **last** turn also reads as `finished` (§21, M4-01, "Known limit"). No game state tells it apart from a finished party once the board has left the overlay history. So a `finished` phase alone does not prove that the party was played to the end. The tester must play through the last minigame, the bonus stars and the final ranking (rows 21 and 25).

The session does not stop by itself when the party is over. The game goes back to its mode select and the phase stays `finished`. "Quit to library" from the launcher's pause overlay then stops the session, writes the replay and the summary, and shows the end-of-party screen with the "Party finished" heading (rows 26 and 27). Do not start a new party first: its setup reads as `setup`, and the run then ends in the wrong phase.

"Quit to library" during the party stops the session at once. The summary then keeps the phase the party was in, and the launcher shows "The game session ended" instead of "Party finished". That run cannot sign off E1.

"Exit Mario Party Together" in the pause overlay closes the program without the end-of-party screen. Do not use it to end the run: use "Quit to library" (row 26).

## 23. Dawn preview: the launcher on screen (2026-11-16)

The first runs of the launcher with the Dawn backend, before M4-11 (E1 on Windows). Branch `dawn-preview`, Windows, `windows-clang-release` with `-DMPT_GX_BACKEND=dawn` and the Dawn prefix at the `gx/dawn.lock` pin.

- **What works.** `--version`; the launcher draws its library screen; "Add game dump" verifies and builds the dump in the launcher; "Keyboard + controller, 2 computer players" starts the party. The game draws through Dawn (D3D12) from the logos to the title, the menus, the board and minigames, and its audio plays. The tester played turns 1 to 7 of `mp4.board.koopas_seaside_soiree` (205,915 steps) with the keyboard and a gamepad. Their recorded session passes `--verify-replay` headless (3,431 of 3,431 checkpoints, phase `board`).
- **The module build outside a developer prompt** (commits 308ebcb, 75cfad3). The recipe baked the C compiler as the preset's bare `clang-cl`, which a shell started from Explorer cannot find ("clang-cl --version failed"). It now bakes an absolute path. clang-cl, lld-link and rc also need the MSVC developer environment (`INCLUDE`, `LIB`, `LIBPATH`, `PATH`). The recipe now bakes the environment the host build was configured in and passes it to every tool it runs (`RecipeConfig::env`). This is a developer build: it builds the module with the toolchain of the machine it was configured on. **The baked environment is not in the build ID** (decided in M4-16, qa's #83 review). The ID is a path-independent identity (#55 review): two machines that build the same commit with the same Clang version get the same ID, and `INCLUDE`, `LIB`, `LIBPATH` and `PATH` are paths. What the environment selects, the Windows SDK and the MSVC CRT the module links against, is therefore not in the ID either; only the Clang version is. That is acceptable for Phase 1, where every module is built and run on one machine and save states and replays only meet modules of the same build. It is a gap for any distributed or shared build (Phase 2 netplay between machines): such a build must pin the SDK and CRT versions and put them, not the paths, into the ID.
- **GX pipeline cache** (commit 89070e2). aurora opens `pipeline_cache.db` and `dawn_cache.db` under `<build>/gx-cache` but never creates the folder, so both failed to open (§20, M2) and every run compiled every pipeline again. The renderer now creates the folder.
- **Hitches of up to about a second** (the tester's report; measured with the dump lane's `--frame-log`). They are Dawn compiling render pipelines the first time a material is drawn. Autopilot party, seed 1, Dawn module, frames 2,000 to 60,000, first run on an empty cache against a second run on the cache that run left: 44 steps over 50 ms (12.7 s in total) against 14 (1.3 s). The steps at a minigame's start were 1,610 + 725 ms (m414), 622 ms (m408) and 737 ms (m436) cold, against 140 + 29, 31 and 62 ms warm. Spikes inside minigames with no I/O (150 to 320 ms) fall to 3 to 10 ms. What remains warm is overlay loads with DVD and ARAM traffic, such as the board's reload after a result screen (about 120 ms both times). Still open: pipelines compile once, the first time they are ever seen on a machine; and the cache lives in the module's build folder, so a new build ID starts cold. aurora already creates every cached pipeline at startup. Keeping the cache per user rather than per build is the cheap next step; compiling asynchronously would break frame-identical images (G-e) and needs a test-off switch.
- **The minigame-end jingle replayed earlier music** (Party Board patch 0036, the tester's report: "it comes to its conclusion but then either loops or plays again", after every minigame). Each minigame ends with stream 1 (winner, 3.58 s) or 4 (draw, 5.01 s), non-looping on the disc. msm streams through a ring of two halves (1.0 s each at 32 kHz). `msmStreamData` toggles `bufNo` after `DVDReadAsync`, and `msmStreamDvdCallback` uploads the half that `bufNo` names, which assumes the read completes later, as on the console. Our DVD layer runs the callback inside `DVDReadAsync`, so every upload went to the other half, one half late. While a stream plays, the late upload still lands just before the voice reaches that half. A non-looping stream's last half is never uploaded, so the voice played that half's contents from two halves earlier. Measured on the dump lane's `--audio-out` (local only): from 3.0 to 3.5 s into the jingle, the audio correlated with the audio exactly 2.0 s earlier at 0.71 to 0.96 (m408) and 0.65 to 0.93 (m414), and at 0.0 to 0.3 before 3.0 s. Silence then followed until the result screen's music, as in `ResultMain`. On PC the patch toggles `bufNo` before the read. After it, the same windows give 0.09 to 0.24 (m408) and 0.08 to 0.47 (m414). `mp4_msm_stream` plays a hand-made 340-block stream through `msmstream.c` with synchronous reads and a modelled MusyX stream voice, and checks every block in order, then silence. Without the patch blocks 300 to 339 play blocks 100 to 139 again (45 of 345 wrong). `mp4_msm_stream_async` (M4-16, qa's #83 review) plays the same stream with reads that complete later, as on the console (the command block stays busy, and the callback runs at the voice's next update): it passes with the patch too, since the patch changes when `bufNo` toggles, not which half the callback uploads once the read is done. The patch moves state hashes: §20 "Results with the stream refill fix" and the M1 rerun, §21.

## 24. Rendering bugs from the E1 party (2026-11-18, `mp4-gx-render-bugs`)

The tester's 10-turn party on the Dawn build (§23) showed three rendering bugs. Reproduced from their own replay: the dump lane plays it with `--verify-replay FILE --capture DIR --capture-from/-to/-every` (no checks), and the shell's `--replay` in its window. Captures stay local.

- **Instruction screens with a black background, and black boxes under the characters and the conveyor belts in Money Belts** (`mp4.minigame.money_belts`). One cause: the frame image carried the EFB's alpha, and the shell's presenter blends its RGBA texture over black. On the console the display copy writes colour only to the XFB, so the EFB's alpha never reaches the TV. MP4 leaves alpha 0 in much of the EFB (the shadow pass's `GXCopyTex` clears to `{0, 0, 0, 0}`, and the instruction screen's background reads alpha 0 after its sprite is drawn), and blob shadows blend (`GX_BL_ZERO`, `GX_BL_INVSRCCLR`) into it. The dump lane's captures looked right, because a PNG viewer ignores alpha; drawn over black like the presenter, they show exactly the reported boxes. On the tester's replay, 12,789 of 19,200 sampled pixels of a Money Belts instruction frame, and 3,372 of a Money Belts play frame, had alpha below 250. **Fix:** the Dawn renderer's readback returns an opaque picture (alpha 255); after it, 0 of 19,200 in both frames. Test: scene `efb_alpha_not_in_picture` (`gx_dawn_probes`). The renderer is presentation only: the M1 seed-1 trail is unchanged (`2523e333cd8cc6cc` at frame 10,000, identical at all 100 checkpoints in two processes, with both fixes in this section).
- **Right Oar Left's lamps** (`mp4.minigame.right_oar_left`). m427 lights the water with a 37.5 degree `GX_SP_SHARP` spot (`Hu3DLLightSpotSet(..., 37.5f, 4)`, `GX_AF_SPOT` on the water mesh). aurora's `GXInitLightSpot` left SHARP's `a0` undivided by `(1 - cr)^2` (and flipped RING1's `a0`), so this spot lit everything up to almost 90 degrees off its axis, saturated (about 22 times full light on the axis): the whole water shone cyan around a white blob. **Fix:** aurora patch 0018, the SDK's formulas; test `gx_frontend_sdk_conformance` (every spot function's shape and SHARP/RING1's coefficients). After it the water is dark outside the cones. **Still open:** the glow at each lamp is a smooth disc, where the tester remembers lit, rippling water. It is m427's additive light-pool quad (`fn_1_9B88`: the pool texture times the alpha of a caustic texture warped by an indirect stage). Next step: trace that draw's texture formats and indirect stage on the dump lane against the SDK's behaviour.
- **Test setup:** `mpt_mp4_copy_trap_tests` linked `mpt::gx`, the Dawn renderer in a Dawn build, and on Windows could not start without `webgpu_dawn.dll` (a loader dialog blocked `ctest`). It now links the null renderer, which it tests. Public CI builds only the null backend on Windows, so no CI job builds this configuration.

## Sections still to fill (P1-18)
- Spike S1 result and the pinned GX backend version (P1-12). **Reported:** `docs/spikes/s1-gx-backend.md`. Recommendation: aurora GX/MTX on Dawn at `514339438178ef2bed1b14e5149d90ece0c6e0cc`, with a host shim, a null backend and a small patch set. A gate runs at the start of P1-14. Folded into ADR 0007 §6 (Accepted). GX surface measured on Party Board's sources: 131 distinct entry points, 104 implemented by aurora at the pin, one real gap (`GXSetZTexture`, m419), no `GD*` calls.
- Hash manifest. Starting areas: RNGs (§7), `GWSystem`/`GWPlayer`/`GWPlayerCfg`/game stats, HuPrc process list, object manager, overlay history, pad state, data-dir cache and ARAM map, MusyX sequencer and voice state, CARD/save media, virtual clock.
- Known issues and the E2 input coverage table. The E1 sign-off checklist is §22. The E7 reference-machine specs live in the Phase 1 plan's D7 table.
