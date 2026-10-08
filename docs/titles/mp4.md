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
| `https://github.com/AxioDL/musyx` | MusyX sound-library decomp (submodule `extern/musyx` of both repos) | Party Board pins `2a0e01aeff0d06a9ee9e12966c435cb75144af9e`. Upstream HEAD is `86851a3a8d51788d166ea42d137b49526f570400`. | CC0 1.0 |
| `https://github.com/encounter/aurora` | GameCube/Wii source-level compatibility layer used by Party Board. **S1 recommends adopting its GX front end, Dawn renderer, MTX and headers, without `aurora_core`** (pending architect, `docs/adr/0007-s6-proposed-amendment.md`). | Party Board pins `514339438178ef2bed1b14e5149d90ece0c6e0cc`. Upstream HEAD on 2026-10-08 is `50e5be30ec12cc72768bc564bc016b55e82be868`, 132 commits later. | MIT |
| `https://github.com/higan-emu/libco` | Coroutines used by Party Board for game processes | Party Board pins `e18e09d634d612a01781168ad4d76be10a7e3bad` | ISC (check during P1-15) |

The decomp also has branches `port` (now a stub whose README points to the Party Board repo), `pal` and `mod` (2024, unrelated).

**The local copy the user supplied** is a zip of `marioparty4` with no `.git`. I compared the git blob hashes of seven files against `main` at `147b165`, including all three files touched by that commit, and every one matched. So the copy is consistent with `main` at `147b165`, but a zip cannot prove a commit.

**How we pin** (ADR 0009 §1):
- `runtimes/gc_wii/mp4/decomp.lock` records, for each source: URL, full commit hash, license and SPDX ID, plus the submodule commits we use (musyx, and aurora/libco if adopted).
- The build recipe gets sources with `git clone --filter=blob:none <url>`, then `git checkout <sha>`, into a gitignored cache directory. It refuses to build if `git rev-parse HEAD` differs from the lock.
- A user-supplied zip is accepted only if its computed git tree hash equals the tree of the locked commit. Otherwise the recipe asks for a git checkout.
- To bump a pin, a PR changes `decomp.lock` and `docs/titles/mp4.md` together and must pass the dump lane.

**Decided (ADR 0009, Phase 1 plan A1):** the game source is **Party Board** at `98ac07fd1e8f7ab938bbb6fe75789fad99419010`, with musyx at `2a0e01aeff0d06a9ee9e12966c435cb75144af9e`, and aurora at `514339438178ef2bed1b14e5149d90ece0c6e0cc` and libco at `e18e09d634d612a01781168ad4d76be10a7e3bad` only if adopted (S1, P1-13). `marioparty4` is a read-only reference. Pinning it after PR #663 merges is the documented fallback.

## 2. License status

- `marioparty4` `main`: **no license file anywhere** (verified in the local copy and on GitHub, where `license: null`). By default that means all rights are reserved by the contributors. Reading it is fine. Building and distributing anything derived from it is not covered until a license lands.
- Upstream intends to use CC0: a maintainer opened PR #663 on 2026-09-24. It has no review comments yet.
- `partyboard` is CC0 1.0 today. It contains the same game sources (adapted for PC), so it is the only MP4 game-source tree with a clear license right now.
- musyx is CC0 and aurora is MIT, both compatible with this project.
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

AxioDL musyx has a PC backend with a pull API (`sndPCRender(out, frames)`) and a null host backend, which fits exactly. Its SDL backend renders on its own SDL thread and **must not be used**.

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
- **Save states into a fresh process (E5)** need: the arena reserved at a fixed virtual address; HuPrc coroutine stacks inside the arena; and the game module loaded at a fixed base (ADR 0008 §11. Windows: fixed image base, no dynamic base. Linux: no fixed-address `dlopen`, so either a non-PIE module mapped at its fixed address by our own loader, or relocation on load). Without a fixed base, every function pointer saved in the arena (process entry points, object callbacks, overlay hooks) is wrong in a new process. **Flagged as a risk to E5.**

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

---

## Sections still to fill (P1-18)
- Spike S1 result and the pinned GX backend version (P1-12). **Reported:** `docs/spikes/s1-gx-backend.md`. Recommendation: aurora GX/MTX on Dawn at `514339438178ef2bed1b14e5149d90ece0c6e0cc`, with a host shim, a null backend and a small patch set. A gate runs at the start of P1-14. Final once architect folds `docs/adr/0007-s6-proposed-amendment.md` into ADR 0007 §6. GX surface measured on Party Board's sources: 131 distinct entry points, 104 implemented by aurora at the pin, one real gap (`GXSetZTexture`, m419), no `GD*` calls.
- Hash manifest. Starting areas: RNGs (§7), `GWSystem`/`GWPlayer`/`GWPlayerCfg`/game stats, HuPrc process list, object manager, overlay history, pad state, data-dir cache and ARAM map, MusyX sequencer and voice state, CARD/save media, virtual clock.
- Known issues, the E1 sign-off checklist and the E2 input coverage table. The E7 reference-machine specs live in the Phase 1 plan's D7 table.
