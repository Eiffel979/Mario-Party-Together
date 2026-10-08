# ADR 0008: Determinism and state model for decomp ports

**Status:** Accepted
Author: architect, 2026-10-08. Amended after P1-01 (`docs/titles/mp4.md`) on 2026-10-08. Approved by: architect, 2026-10-08. Applies to `runtimes/gc_wii/` and every title built on it. Contains a **time-boxed exception** to ADR 0002 rule 5.

## Context
ADR 0002 requires that `stateHash()` be identical across platforms, compilers and processes, never cover pointers, padding or uninitialized memory (rule 4), and that `SaveState` load on any supported platform (rule 5).

A decomp port runs the original C code natively on a 64-bit little-endian host. That raises problems an emulator does not have:
- Game state is spread across globals and heap objects full of **host pointers and function pointers**, which differ between platforms, builds and runs.
- Struct copies carry **padding bytes** from the stack, so raw memory is not reproducible.
- **Floating-point** results depend on the compiler (FMA contraction, x87 vs SSE) and on the platform libm (`sinf`, `powf`).
- The console SDK has **threads, async I/O and wall-clock time**.
- Disc data is **big-endian** with 32-bit offsets, while host `long` is 32-bit on Windows and 64-bit on Linux.

## Decision

### Execution
1. **Single-threaded, deterministic scheduling.** `OSThread` **and the game's own cooperative processes** (MP4's HuPrc, `mp4.md` §6) are coroutines. Switches happen only at SDK or process-system call boundaries (`OSSleepThread`, `OSYieldThread`, message-queue waits, VI retrace, `HuPrcVSleep`) under a fixed priority and ordering policy. The game's `main()` is the root coroutine and yields at the retrace wait: **one `stepFrame` = one VI field = one main-loop iteration**. No game code runs on a host thread.
   - **Coroutine stacks are allocated from the arena** (rule 7), never from the host heap or the OS, so save states capture them and their addresses are reproducible. The coroutine library must accept caller-provided stack memory and behave identically on Windows and Linux. libco (with caller-provided memory) and minicoro (with a custom allocator) are both acceptable, and P1-13 chooses. Saved register contexts are part of `SaveState`, never of the hash.
2. **Virtual time.** The host clock is never read. Virtual time (the console bus-timer tick count) is derived as follows:
   - **Epoch from the seed.** At `boot()`, the virtual clock's starting value is derived from `RuntimeConfig::seed` through `DeterministicRng`, within a bounded range that always gives a valid calendar date. MP4 seeds its board RNG (`boardRandSeed`) from `OSGetTime()`, and re-seeds `frand` from it whenever its seed is 0 (`mp4.md` §7). So the epoch is how the session seed reaches those RNGs, and the hash manifest covers it.
   - **Frame advance.** Each `stepFrame` advances virtual time by exactly one field period at the console rate (60000/1001 Hz for NTSC), using an integer remainder accumulator.
   - **Fixed quantum per query.** Each **game-code** call to `OSGetTime`/`OSGetTick` (or any other time query) advances virtual time by a fixed, documented quantum before returning. The number of calls is deterministic, so this stays deterministic. Without it, the game's non-yielding busy-waits (MP4's sound-wait loop, which spins until voices stop) would never terminate. Our own SDK-layer code never calls the public time API, so internal or presentation-only paths (null vs real renderer or audio) cannot change the number of queries (E6).
   - **Virtual interrupts at SDK call boundaries.** Alarms, the VI retrace, AI-DMA/sound-engine ticks and other virtual interrupts run when virtual time passes their deadline, and **only at SDK call boundaries** (including time queries). They run in deadline order, with ties broken by a fixed source order. They never run asynchronously and never on a host thread.
3. **I/O completes synchronously.** DVD reads, ARAM DMA and CARD operations complete inside the call, or at the latest at the next SDK call boundary, never after a multi-frame latency (ADR 0007 §4). MP4 spins on these completions without yielding.
   - **Fixed hardware answers.** Settings and hardware getters (sound mode, progressive scan, console type, memory size, language, reset code) return fixed values (ADR 0007 §4). They are never read from the host.

### Numerics and types
Cross-platform identity is a **Phase 1 exit requirement** (D5): the same replay must give the same hash trail on Windows and Linux. Rules 4–6 exist to make that hold.

4. **One compiler for game code.** The game module and the SDK layer are built with **Clang on every platform** (clang-cl on Windows). Strict IEEE: `-ffp-contract=off`, no fast-math, SSE2 on x86-64, no x87, and the same flags on ARM64.
5. **Our math library, not the platform's.** Game code calls the MSL math from the pinned game source (ADR 0009), or a pinned portable libm, compiled with the flags above, never the host CRT or glibc. This matters for MP4's roughly 3,300 double-precision trig calls. `sqrt` and `fmod` are exactly specified by IEEE, so host versions are allowed. Paired-single matrix and vector functions are plain C (ADR 0007 §4). The console's fast float-to-int casts (`OSf32tos16` and similar) are C functions with the console's **saturating** semantics. Golden tests pin outputs (P1-13, P1-21).
   - **Float-to-int overflow.** Out-of-range float-to-int casts are undefined behaviour in C, and x86-64 and ARM64 give different results. The dump lane includes a **UBSan `float-cast-overflow` run** of the full-party replay, and every hit is fixed in game code (through a shim or patch) or shown to be unreachable. This is not a Windows/Linux issue today (both are x86-64), but it is a prerequisite for any ARM64 target.
6. **Fixed-width types.** No `long` or `unsigned long` in game-state structs or disc-data structs. Disc data is byte-swapped and converted to host structs **at load time**. 32-bit on-disc offsets become host pointers only in loader code.

### Memory
7. **One arena.** All game heaps (`HuMem`-style heaps and the SDK's `OSAlloc` arenas), emulated ARAM and coroutine stacks come from one runtime-owned arena, allocated and zeroed at `boot()`. Its size matches the console's main RAM plus emulated ARAM (24 MB + 16 MB), plus a fixed region for coroutine stacks. Game code never calls the host `malloc`. Overlay (REL) code and data live in the game module, not the arena (ADR 0009, P1-15), so the arena layout is the same on every platform.
   - **No host-varying bytes in the arena.** Where game code stores a caller or return address in memory (MP4's `HuMemDirectMalloc` records its caller in heap block headers), it stores a **fixed constant**. Otherwise arena bytes would differ between runs and builds, which breaks save-state comparison and the deep-hash debug mode.

### State hash: an explicit manifest
8. `stateHash()` is computed from a **hash manifest**: a reviewed, typed list of everything in game logic that affects future frames. Typical areas: game RNG state(s), virtual clock (including its epoch) and frame counters, scheduler and process state, party and board work structures, the object manager's live objects, overlay history, pending I/O, **sound-engine state visible to game code** (for MP4, the MusyX sequencer, voice and stream state, including MIDI controller values), THP playback state, and save media (ADR 0004). **Audio state is game logic** whenever the game polls it, so a null-audio run still steps the synth (ADR 0007 §4).
   - Fields are hashed by value with `StateHasher::update_u64` and friends, never as raw struct bytes.
   - Data pointers are hashed as **arena offsets** (or object indices). Function pointers are hashed as **stable IDs** from a registration table. Null is hashed as a sentinel.
   - The manifest is documented in `docs/titles/<id>.md`. Every area has a perturbation test (exit criterion E4).
9. A **deep-hash debug mode** (per-region hashes over arena and globals) exists for bisecting desyncs on a single machine and build. It is never used for comparisons between peers.

### Save states: the time-boxed exception
10. **Phase 1 `SaveState` = arena + registered globals + virtual hardware state**, valid **only for the same build on the same platform**. `SaveState::format_version` embeds a build fingerprint, and `loadState` rejects anything else with a `Result` failure. This **deviates from ADR 0002 rule 5** until **Phase 2 entry**. Phase 2 must not start netplay desync recovery without an ADR that closes it. The candidates to evaluate:
    - (a) reserve the arena at the same fixed virtual address on all platforms, plus function-pointer translation through the registration table and pointer fix-ups for globals;
    - (b) a semantic (manifest-driven) serializer that writes state by value and rebuilds the pointers;
    - (c) recover from desyncs by reloading the last agreed checkpoint state **only among peers on the same platform and build**, and otherwise by re-simulating from a checkpoint the host broadcasts;
    - (d) restrict netplay to peers on the same platform.

### Save states into a fresh process (Phase 1 exit criterion E5)
11. A state saved in one process is loaded into a **fresh process** of the same build on the same platform. That only works if every address stored in the arena or in registered globals is the same in both processes:
    - **Arena at a fixed virtual address,** reserved early at `boot()`. `boot()` fails with a clear `Result` if the address is unavailable.
    - **Game module at a fixed base.** The SDK layer is linked into the game module (both are built with the same Clang flags, rule 4), so game code, SDK code and their data share one base. Function pointers stored in the arena (process entry points, object callbacks, overlay hooks) then stay valid. On **Windows**, the module is linked with a fixed image base and no dynamic base, and `boot()` checks that it was loaded at that base. On **Linux**, `dlopen` cannot choose an address, so either the module is linked as **non-PIE at a fixed address** and mapped there by our own loader, or it is **relocated on load** to the fixed base. P1-16 picks one.
    - **Nothing in the arena points into host memory** outside the module and the arena: no host heap, CRT objects or host-runtime code.
    - P1-16 demonstrates this on both OSes by milestone **M1**. If it fails, **E5 narrows to in-process save states** (save, destroy the runtime, create a fresh runtime in the same process, load), and the fresh-process case moves to the Phase 2 ADR that closes §10.

## Answered by P1-01
MP4's heap setup (`mp4.md` §11), thread and process inventory (§6), RNGs and time reads (§7), floating-point profile (§9). The object-manager layout and the full manifest are written in P1-16/P1-18.

## Alternatives
- **Hash raw memory (arena + globals).** Rejected: pointers, function pointers and stack padding make it differ across platforms and even runs. It violates ADR 0002 rule 4.
- **Use the host compiler for game code (MSVC on Windows, GCC on Linux).** Rejected: two optimizers and two libms make FP equality across platforms a matter of luck.
- **Accept platform-specific hashes, or let cross-platform equality slip to Phase 2.** Rejected by the user's decision D5 in the Phase 1 plan: Windows and Linux must produce identical hash trails at Phase 1 exit (E3c). Platform-specific hashes would rule out cross-platform netplay, and FP divergence is cheaper to find now than after netplay is built on top.

## Consequences
- runtime-gc-wii owns the manifest and its perturbation tests. qa-reviewer audits manifest coverage at every exit review.
- A Clang toolchain becomes part of the local build requirements (ADR 0009).
- The exception in §10 is listed as a Phase 2 entry blocker in `docs/PLAN.md` and `docs/phases/phase-1.md` §11.

## Review (architect, 2026-10-08)
**Verdict: Accepted.** The open question (Phase 1 plan Q5) is resolved by decision D5: identical Windows and Linux hash trails are required at Phase 1 exit. That confirms the strictest option this ADR was written for, so the decision text needs no change beyond recording it.
- The only deviation from ADR 0002 is the time-boxed `SaveState` exception in §10. It expires at Phase 2 entry and is listed as a Phase 2 entry blocker in `docs/PLAN.md` §7.
- The items listed under "Answered by P1-01" size the hash manifest and the arena. They do not change the decisions here, so acceptance does not wait for them.
- Golden trails are recorded only from builds made from the pinned game source (ADR 0009), because the build manifest is part of what makes two builds comparable.
- **Amended after P1-01.** All of these refine how the decisions above are implemented. None changes a core contract, so the ADR stays Accepted:
  - fixed time quantum per query;
  - virtual interrupts at SDK call boundaries;
  - seed-derived clock epoch, noting that MP4's board RNG is seeded from `OSGetTime`;
  - game processes as coroutines with arena stacks;
  - constant caller addresses in heap headers;
  - synchronous I/O;
  - fixed hardware answers;
  - saturating fast casts;
  - the UBSan float-cast-overflow run;
  - fixed module and arena bases for fresh-process save states (§11), with the E5 narrowing rule.
