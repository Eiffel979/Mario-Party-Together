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
   - **Explicit `fma`/`fmaf` calls are allowed; contraction is not.** `-ffp-contract=off` forbids the *compiler* from fusing `a * b + c`, because whether it does so depends on the compiler, flags and target. An `fmaf` written in the source is a different thing: one IEEE 754 fusedMultiplyAdd, correctly rounded, with one specified result. aurora's `PSVEC*` reciprocal-square-root emulation (a bit-exact `frsqrte` table plus an explicit `fmaf`) is therefore acceptable, on three conditions:
     1. Every `fma`/`fmaf` in code linked into the game module resolves to **one pinned, correctly rounded software implementation in our math library**. It never resolves to the host CRT (whose `fmaf` quality varies), and never to a hardware FMA instruction selected per machine (our x86-64 baseline is SSE2, so FMA3 is not assumed).
     2. Golden tests cover hard rounding cases (including results near halfway points, subnormals and signed zeros) and pin the outputs of the three `PSVEC*` functions.
     3. A source grep in P1-22 lists every explicit `fma`/`fmaf` in the game module, so new uses are reviewed.

     If condition 1 cannot be met by P1-13, those three functions use our plain-C versions instead.
   - **Float-to-int overflow.** Out-of-range float-to-int casts are undefined behaviour in C, and x86-64 and ARM64 give different results. The dump lane includes a **UBSan `float-cast-overflow` run** of the full-party replay, and every hit is fixed in game code (through a shim or patch) or shown to be unreachable. This is not a Windows/Linux issue today (both are x86-64), but it is a prerequisite for any ARM64 target.
6. **Fixed-width types.** No `long` or `unsigned long` in game-state structs or disc-data structs. Disc data is byte-swapped and converted to host structs **at load time**. 32-bit on-disc offsets become host pointers only in loader code.

### Memory
7. **One arena.** All game heaps (`HuMem`-style heaps and the SDK's `OSAlloc` arenas), emulated ARAM and coroutine stacks come from one runtime-owned arena, allocated and zeroed at `boot()`. Its size matches the console's main RAM plus emulated ARAM (24 MB + 16 MB), plus a fixed region for coroutine stacks. *(Amended after P1-16 boot: main RAM is a per-title, per-build constant; MP4 uses 112 MiB. See the amendment at the end.)* Game code never calls the host `malloc`. Overlay (REL) code and data live in the game module, not the arena (ADR 0009, P1-15), so the arena layout is the same on every platform.
   - **No host-varying bytes in the arena.** Where game code stores a caller or return address in memory (MP4's `HuMemDirectMalloc` records its caller in heap block headers), it stores a **fixed constant**. Otherwise arena bytes would differ between runs and builds, which breaks save-state comparison and the deep-hash debug mode.
   - **Game heaps return zeroed blocks.** Every allocation's user area is zero on return, from `OSAlloc`, `HuMem` and every sub-allocator inside their blocks or in emulated ARAM, so no block carries bytes from an earlier occupant *(amendment of 2026-11-08, "Stale heap bytes")*.

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
    - **Game module at a fixed base** *(the base and the reasons for it: amendment of 2026-11-07, "Module words", point 1)*. The SDK layer is linked into the game module (both are built with the same Clang flags, rule 4), so game code, SDK code and their data share one base. Function pointers stored in the arena (process entry points, object callbacks, overlay hooks) then stay valid. On **Windows**, the module is linked with a fixed image base and no dynamic base, and `boot()` checks that it was loaded at that base. On **Linux**, `dlopen` cannot choose an address, so either the module is linked as **non-PIE at a fixed address** and mapped there by our own loader, or it is **relocated on load** to the fixed base. P1-16 picks one.
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
  - explicit `fmaf` allowed under three conditions (§5, ruling requested by spike S1);
  - fixed module and arena bases for fresh-process save states (§11), with the E5 narrowing rule.

## Amendment after P1-13 (architect, 2026-10-09)
These decisions come from the SDK core branch (`p1-13-sdk-core`) and qa-reviewer's review of it. They refine §1–§3 and §5. No core contract changes, so the ADR stays Accepted.

1. **Coroutines (§1): our own context switch is accepted** in place of libco or minicoro.
   - **Why:** §1 asks for caller-provided arena stacks and the same behaviour on Windows and Linux, and the switch meets both. Neither library is in vcpkg, so either one would need a new fetch path and a pin. The switch is about 40 instructions per ABI, written out and reviewable.
   - **Conditions** (runtime-gc-wii; qa-reviewer checks them at merge):
     - Tests check that every callee-saved register survives a switch: Win64 `rbx rbp rdi rsi r12–r15 xmm6–xmm15`, MXCSR, the x87 control word and the TIB stack fields; SysV `rbx rbp r12–r15`, MXCSR and the x87 control word. These run on both CI OSes.
     - No C++ exception, `longjmp` or SEH unwind ever crosses a switch. Game code is C, and SDK entry points that can fail return error codes.
     - Unsupported targets fail at compile time (the existing `#error`). ARM64 is a later item.
     - The assembly lives in one file, owned by runtime-gc-wii, with a header comment giving the frame layout.
   - **Fallback:** if a defect turns up that the tests cannot pin down, switch to libco with caller-provided memory, pinned in the source lock (ADR 0009 §1). libco was already the alternative this section named.
   - Register contexts are part of `SaveState` and never part of the hash, as before.
2. **Virtual time (§2): the field clock is a floor.** At the start of frame `n`, `now = max(now, R_n)`, where `R_n = epoch + n × field` is computed exactly (NTSC field = 675,675 ticks at 40.5 MHz).
   - Each time query then adds the fixed quantum (405 ticks).
   - The VI retrace fires exactly once, at the start of each frame `n ≥ 1`, driven by the frame. It never fires because a busy-wait passed `R_n`. One `stepFrame` therefore stays one main-loop iteration, which is the point of §1.
   - This is how §2's "advances by exactly one field period" is to be read. Over time, virtual time follows the field rate. Time spent in queries within a frame is never taken back, and time never runs backwards.
   - When every thread is blocked, the scheduler may jump `now` to the next interrupt deadline before `R_{n+1}`.
   - A frame that spends more than 60 virtual seconds in time queries faults instead of hanging.
   - These constants feed every golden trail. Changing one is an amendment to this ADR.
3. **I/O completion callbacks (§3).**
   - By default, callbacks run at the **next SDK call boundary**. This satisfies §3 and still works for code that sets its "busy" flag after issuing the call.
   - `SdkConfig::io_callbacks_before_return` runs them before the call returns instead.
   - The mode is a **per-title constant** chosen by the runtime and recorded in `docs/titles/<id>.md`. It is never a user option, and it is part of the build fingerprint (§10).
   - P1-16 chooses MP4's mode by auditing every completion-wait loop.
4. **One MTX implementation (§5), owned by the aurora MTX target.** P1-13's plain-C `mtx.cpp` computes the `PSVEC*` reciprocal square root as `1 / mpt_sqrtf`. aurora computes it with a bit-exact `frsqrte` table and an explicit `fmaf`. The two give different numbers, and linking both would make the `PS*`/`C_*` symbols collide.
   - **Decision:** aurora's MTX sources at the pin (with patch 2, ADR 0007 §6) are the only implementation of `PSMTX*`, `PSVEC*`, `PSQUAT*` and `C_*` linked into the game module.
   - They are built by the P1-14 MTX target under these flags, with `fmaf` mapped to P1-13's correctly rounded `mpt_fmaf` and trig mapped to our math library.
   - **Why aurora's:** §5 and ADR 0007 §6 already chose it. It is closest to the console, because the `frsqrte` table is bit-exact. Party Board's game code is exercised against it. P1-13's `mpt_fmaf` now meets condition 1 of §5.
   - **What follows:**
     - The SDK stops exporting `PS*`/`C_*` names. Its plain-C versions are deleted, or kept only as a test-only differential reference under non-colliding names.
     - The MTX golden values move to P1-14 and are recorded against aurora's MTX. The trig and `mpt_fmaf` goldens stay with P1-13.
     - `docs/titles/mp4.md` §14 (owned by runtime-gc-wii) is corrected to say so.
     - The other two conditions of §5 stay open: the hard-case `PSVEC*` goldens (P1-14) and the explicit-`fmaf` source grep (P1-22).
5. **Hash sentinels (§8).** For now, the SDK hashes every function pointer, and every pointer outside the arena, as one presence sentinel. Perturbation tests cannot tell a changed callback or target apart from an unchanged one. P1-16's registration table must replace the sentinel with stable function IDs, and make an out-of-arena pointer a fault or a registered ID. This is a P1-16 acceptance criterion for E4.

## Amendment after P1-06: host side and Clang side (architect, 2026-10-13)
qa-reviewer's review of P1-06 found a gap. The shell's generated title registration links CMake targets, but `runtimes/gc_wii` is a separate Clang ExternalProject whenever the main compiler is not Clang. So nothing built there can be a registration target. **Decision: split each GameCube/Wii title into a host side and a Clang side. Do not import Clang-built libraries, and do not require clang-cl for the whole tree.**
- **Clang side** (§4): the SDK layer, the GX front end and MTX, the GX renderer (`mpt::gx`: null, or aurora's on Dawn), MusyX, and the game sources. All of these are linked into the **game module**, `mp4_game.dll` / `.so`. The module is built by the asset-import recipe (ADR 0009 §3) and, for SDK tests, by the Clang sub-project. It exports **only a narrow C ABI**: plain C types, no C++ classes and no standard-library types across the boundary.
- **The renderer sits on the Clang side** although it is presentation only (ADR 0007 §6). It talks to the front end through aurora's C++ internals, so it must share the front end's compiler. "Host-side library" in ADR 0007 §6 means "outside game-visible memory", not "built by the host compiler". The renderer and Dawn hold no game-visible state, and Dawn stays a separate shared library. Frames reach the host side as `cpu_rgba8` through the module's C ABI.
- **Host side**: `Mp4Runtime : ITitleRuntime`, `Mp4Adapter : ITitleAdapter` and the module loader (§11). These are built by the tree's own compiler, MSVC or GCC, as ordinary targets in a host subdirectory of `runtimes/gc_wii`, which the root adds outside the Clang-only branch. The runtime registers with `mpt_register_title(TARGET <host lib> FUNCTION <fn>)`.
- **Why:**
  - A C ABI keeps compiler and standard-library mismatches out of the picture: MSVC against clang-cl on Windows, and GCC's against Clang's library on Linux.
  - It matches §11, which already loads the module at a fixed base through a narrow ABI.
  - Game-visible code still gets one compiler (§4), while the shell and core keep theirs.
  - Importing a Clang-built C++ static library into an MSVC link would put C++ ABI compatibility on the critical path for no benefit.
  - The whole-tree `windows-clang-*` presets stay a developer convenience (ADR 0007, P1-14 gate amendment), not a requirement.
- **Enforced by:**
  - `mpt_register_title`: it now rejects imported targets and accepts only a plain identifier as `FUNCTION`.
  - P1-16's acceptance criteria.
- **The C ABI** is defined in P1-15/P1-16 by runtime-gc-wii, with architect review: boot, step, video, audio, save and load state, hash, frame rate, save media, and party reads for the adapter. Each call gets an explicit struct version. It is not a core contract, because only `Mp4Runtime` calls it.

## Amendment after P1-14: NaN in the state hash (architect, 2026-10-13)
### Context
P1-14's MTX goldens failed on Linux on **NaN sign and payload bits** (`docs/titles/mp4.md`, d983f0a).
- IEEE 754 leaves the sign and payload of a NaN result unspecified. On x86, an operation with two NaN operands returns the one the compiler placed first, and compilers may swap the operands of commutative operations even under strict FP (§4).
- On the reference machine, `-Od` and `-O2` already disagree on 38 NaN signs in one golden group.
- Game state can hold NaN floats. §8 hashes fields by value, so a NaN could split the state hash between builds or platforms when behaviour is identical.

### Decision: canonicalize NaNs when hashing floats
1. **`StateHasher` gains `update_f32` and `update_f64`.** They hash the IEEE 754 bits little-endian: 4 bytes for f32, 8 bytes for f64. Every NaN (quiet or signalling, any sign, any payload) is first replaced by the canonical quiet NaN: `0x7fc00000` for f32, `0x7ff8000000000000` for f64. All other values are hashed exactly, including `+0`/`-0`, infinities and subnormals.
2. **Every float field in a hash manifest goes through these functions**, never through `update` on raw bytes or `update_u64` on bit-cast values. This applies to the SDK's hash area, the title manifests and `FakeRuntime` if it ever gains floats. runtime-gc-wii moves the SDK's existing float fields over in its next SDK change. P1-18 states the rule for the MP4 manifest.
3. **Why this is safe:**
   - A NaN's sign and payload can only change behaviour through four paths: `copysign`/`signbit` on the NaN, a bit cast to an integer, formatting it, or a float-to-int conversion.
   - Comparisons are false for every NaN, arithmetic just propagates "a NaN", and our fast casts map every NaN to 0 (§5).
   - On any of the four paths, the payload difference becomes a different **non-NaN** value, an integer or a branch. That value is hashed exactly. So canonicalizing only delays detection to the first frame where the payload has an effect, which is exactly where we want a desync to show.
   - A payload that never has an effect should not split the trail. Otherwise the trail would depend on optimisation level, which ADR 0002 rule 4 forbids.
4. **What canonicalizing hides:** two runs whose only difference is a NaN's bits, with no effect yet. The perturbation tests (E4) still cover every manifest area, because each perturbation changes a non-NaN value.
5. **Signed zero stays exact.** Under strict FP (no fast-math, `-ffp-contract=off`), the sign of a zero result is fully specified by IEEE 754, so a difference there is a real divergence. `1/x` and `atan2` already make it behaviour.
6. **`SaveState` is unaffected.** It keeps the raw bytes (§10), so `loadState` restores every NaN bit for bit, and the run continues exactly as the producer's would on the same build. The hash after a load still equals the producer's (ADR 0002 rule 5), because the same canonical hashing applies to both. The deep-hash debug mode (§9) hashes raw memory. It can show NaN-bit differences between two builds, and that is expected; it is only ever compared on one machine and one build.
7. **Contract version.** The two functions are added to `state.h`, and every existing digest (`update`, `update_u64`) is unchanged. So no recorded hash, replay or golden trail changes, and `kCoreContractVersion` stays 1 (ADR 0002, amendment 16).

## Review of the MP4 game-module C ABI v1 (architect, 2026-10-19)
I reviewed the draft `runtimes/gc_wii/mp4/include/mpt/mp4/game_abi.h` (P1-15, branch `p1-15-mp4-module`) against the host/Clang split above.

**Verdict: approved as the shape, with required changes.** They must land before P1-16 merges.

The design is right:
- one export, `mpt_mp4_get_api(version)`, returning a function table;
- plain C types only, and no struct passed by value;
- memory the module returns stays owned by the module;
- the input frame mirrors `mpt::InputFrame` (checked with `static_assert` in `abi.cpp`);
- frames cross as `cpu_rgba8`;
- one game per process, with `MPT_MP4_ERR_BUSY`, matching ADR 0002 rule 10.

It covers everything `Mp4Runtime` needs for `ITitleRuntime`: boot, step, frame numbering, frame rate, `video`/`audio`, `saveState`/`loadState`, `stateHash`, and save media with its generation. Required changes:

1. **Lifetimes follow ADR 0002 rule 6, not "until the next call".** The draft makes memory valid "until the next call that names the same game". Under that rule, calling `audio()` invalidates the pointer from `video()`, but the shell reads both after every step.
   - Returned buffers (`video`, `audio`, `party`, `fault`) must stay valid until the next `step`, `load_state` or `destroy`.
   - Query functions never invalidate anything.
2. **`audio()` returns the samples of the last step and is idempotent.** Today it calls `takeAudio()`, so a second call returns nothing, while `ITitleRuntime::audio()` is "output of the last stepFrame()". The module collects each step's samples during `step`.
3. **Boot flags are determinism inputs.** `SKIP_BOOT_SEQUENCE` and the two unlock flags change the game's trajectory. Under ADR 0002 rule 2 they would be inputs that no replay or peer records.
   - **Phase 1:** `Mp4Runtime` always passes one fixed constant set of gameplay flags, documented in `docs/titles/mp4.md`. Gameplay flags are never user-settable.
   - **Later:** the unlock options become title-specific rules, through the ruleset follow-up ADR (ADR 0002 review), and are recorded in the replay's ruleset field.
   - The module **includes the gameplay flags in `state_hash`**, so a mismatch shows on the first checkpoint.
   - `DEBUG_COPY_TRAP`, `video_backend`, `internal_scale`, `audio_output`, `cache_dir` and `arena_base` must not affect `state_hash` (rule 2, E6).
4. **No host pointers in game-visible memory.** `disc_root` and `cache_dir` are copied at `create`. The report callback and its `user` pointer live in the module's host-side object, as they do today. None of these may live in the arena or in a registered global that `save_state` captures (§11).
5. **Forward compatibility, done properly.**
   - `create` accepts any `struct_size` at least as large as the **v1.0 size** of each struct. A named constant, not `sizeof` of the current struct, so a v1.1 module still accepts a v1.0 host. Fields beyond the caller's `struct_size` get v1.0 defaults.
   - `MptMp4Api::struct_size` is set by the **module**. The host checks that it covers every function pointer it calls. Entries added in a minor version may be NULL.
   - Fixed-layout structs without `struct_size` (`MptMp4Pad`, `MptMp4InputFrame`, `MptMp4FrameRate`, `MptMp4PartyPlayer`) are frozen for major version 1. The header asserts their sizes in C (`_Static_assert`), and the host asserts them against `mpt::` types as `abi.cpp` does.
   - The module folds stick value -32768 to -32767 (ADR 0002, amendment 12).
6. **Save states.**
   - The blob carries the full `build_id` and its own layout version, and `load_state` rejects any mismatch with `MPT_MP4_ERR_STATE`. `Mp4Runtime` fills `SaveState::format_version` from that layout version (§10).
   - After `load_state`, `frame()` returns the state's frame (rule 3).
   - The comment "valid in the same process" must say "same build and platform, at the same arena and module base". E5 needs fresh-process loads (§11) unless the M1 narrowing rule triggers.
   - Card image and generation are restored with the state (ADR 0004 rule 9).
7. **The party struct must be enough to derive `PartyPhase`.** The overlay index and name give board and minigame, but cannot tell `results` from `finished` (ADR 0005: `finished` only once results are final).
   - Add the raw fields the adapter needs, as `uint32_t` flags or game values: at least "a party is in progress", "in party setup" and "results final". Which game variables back them is runtime-gc-wii's call, in `docs/titles/mp4.md`.
   - `party()` must stay cheap and must never change game state (ADR 0002 rule 9).
   - Mapping overlay names and character indices to content IDs is the host adapter's job, which is right.

**Open item for core (architect):** `step` can return `HALTED` or `FAULTED`, but `ITitleRuntime::stepFrame` has no way to report it.
- `RESET_REQUESTED` (`OSResetSystem`) should be handled **inside the module**, as a deterministic soft reset, so it never needs the host.
- For halts and faults, I will write a small additive core ADR before P1-16 merges: a runtime status query that `Session::step` turns into a failed `Result`. Until then, `Mp4Runtime` logs the fault and stops stepping the module, but keeps the frame count.

`rumble()` stays presentation-only. It is derived from game state and may be ignored until the host-feedback follow-up (ADR 0002 review, Phase 4).

## Amendment after P1-16 boot: per-title main-RAM size (architect, 2026-10-21)
### Context
§7 sized the arena's main-RAM region at the console's 24 MB. Party Board's `TARGET_PC` build multiplies MP4's game heaps by 4 (78 MiB in total), so boot failed at `OSAlloc(44040192)`. The growth is deliberate:
- on a 64-bit host, game structures that hold pointers are larger than on the console;
- the HSF model data is converted at load time into 64-bit host structures (§6).

runtime-gc-wii added `SdkConfig::main_ram_bytes`, default 24 MiB, and MP4 uses 96 MiB. With that, MP4 boots on the user's dump and runs 600 frames without halting or faulting.

### Decision: accept a per-title main-RAM size, fixed per build
Patching Party Board's heap sizes back to console values is rejected. The console sizes were budgeted for 32-bit pointers and the console's own data layout, so the same heaps would run out on a 64-bit host, most likely late in a party, on large boards. That is exactly where an out-of-memory fault is hardest to find. Party Board's multiplier is the upstream port's answer to that, and it is part of the pinned source (ADR 0009).

1. **The size is a build constant of the title's game module, never a runtime option.**
   - `main_ram_bytes` is set by the module itself (MP4: **112 MiB**, see "Revised for MusyX" below) when it configures the SDK.
   - It is not a field of the module's boot config, `RuntimeConfig` or any user setting, and it is the same on every platform.
   - ARAM stays at the console's 16 MB, and the coroutine-stack region keeps its fixed size (§7).
   - Every title states its size, and why, in `docs/titles/<id>.md`.
2. **Rule for choosing it:**
   - Use the smallest multiple of 8 MiB that is at least the sum of the game's own heap sizes as built (Party Board's values for MP4) plus the SDK's fixed allocations, plus a margin of at least 10%.
   - P1-16 measures the peak use over the full-party replay on the dump lane, and shows that the size covers it with the margin to spare. The result goes in `mp4.md`.
   - An `OSAlloc` failure is a deterministic **fault** (ADR 0011), never a host error, so a size that is too small shows up as a reproducible fault at a fixed frame.
3. **It is a determinism input through the build, not through the session.**
   - Heap placement, and therefore arena offsets, depend on the main-RAM size. Hash-manifest pointers are hashed as arena offsets (§8). So the size is part of what makes two builds comparable.
   - It is included in the module's `build_id` (ADR 0008, ABI review item 6), alongside the pins, patches and compiler. Golden trails and replays already require the same build (ADR 0006, §10), so they also pin the size.
   - The save-state header records the arena layout: main-RAM size, ARAM size and stack-region size. `load_state` rejects a mismatch with `MPT_MP4_ERR_STATE`, even if `build_id` somehow matched.
   - Changing a title's size changes its `build_id` and requires re-recording that title's golden trails. Fixtures are re-recorded deliberately, as for a pin bump.
4. **What the game can see stays console-shaped.** Hardware getters keep their fixed console answers (§3). `OSGetPhysicalMemorySize` and similar calls still report 24 MB, so no game logic branches on the host's larger arena. Only `OSGetArenaLo`/`OSGetArenaHi` and the heap calls see the real bounds, because Party Board's heap setup needs them. If Party Board's PC heap setup turns out to derive its sizes from the reported memory size, the module may report the build size there instead. That is still a per-build constant and so still deterministic, and the choice is recorded in `mp4.md`.
5. **Save-state size:**
   - A Phase 1 save state is the whole arena (§10): about 112 MiB of main RAM, 16 MiB of ARAM and the stack region, plus the header. That is acceptable for the same-build, same-machine states Phase 1 uses: E5 round trips and dump-lane tests.
   - P1-16 records the save and load times. If E5 or a dump-lane test becomes slow, compressing the zero-filled tail (or run-length encoding the blob) is an allowed layout change inside the module.
   - The Phase 2 ADR that closes §10 must budget netplay state transfer against this size, not against 24 MB.
6. **Fixed base and spacing (§11):**
   - The arena is reserved as one region at the fixed base, laid out from it in this order: main RAM, ARAM, stacks.
   - Its total size must stay below the spacing reserved between fixed bases (`0x400000000000`, 64 TiB), which a 112 MiB main RAM does by a very wide margin. A title whose layout would ever exceed it must change the spacing here first.
   - `boot` still fails with a clear message if the address range cannot be reserved.

§7's "Its size matches the console's main RAM" now reads: **"main RAM is the title's fixed per-build size (the console's 24 MB unless its `docs/titles/<id>.md` gives another and why), plus emulated ARAM (16 MB) and the fixed stack region."**

### Revised for MusyX: MP4 main RAM is 112 MiB (architect, 2026-10-22)
Integrating MusyX's PC renderer (P1-13b, ADR 0009 musyx pin) moves sample memory out of ARAM.
- On the console, sample data lived in the 16 MB ARAM. The PC renderer keeps every resident sample, plus a decoded copy of each group, in `msm`'s heap in main RAM.
- Party Board patch 0018 therefore grows `HEAP_MUSIC` by 16 MB, and sizes the `msm` heap at three times the console size plus 16 MB.
- MP4's main RAM goes from 96 MiB to **112 MiB**, which still follows the sizing rule (point 2): the built heaps, including these, plus at least 10% margin, in 8 MiB steps.
- **The sizing rationale for MP4 is 64-bit host structures plus MusyX's PC sample residency.** Emulated ARAM stays 16 MB, because the SDK's ARAM and ARQ still serve the game's own uses of it.
- Everything else in this amendment is unchanged:
  - 112 MiB is a build constant set by the module, part of `build_id` and recorded in the save-state layout header;
  - M1's trails (10,000 frames, seed 1 and 2, separate processes) were recorded at this size;
  - the P1-16 peak-use measurement must cover the full-party replay with music playing.

## Amendment after P1-16 hash coverage: one cross-platform state hash (architect, 2026-11-05)
### Context
P1-16 (`p1-16-mp4-runtime`, `docs/titles/mp4.md` §20) made MP4's `stateHash` cover game memory:
- the game core and REL data ranges;
- allocated `OSAlloc` and `HuMem` blocks;
- the ARAM in use;
- stack records;
- the SDK and manifest fields.

E4 perturbation passes 60 of 60, and M1 holds on Windows. Hashing raw memory also found real bugs. For example, Party Board's file-scope `std::unordered_set` sat in the game data range and held host-heap pointers.

A raw-memory hash is only valid for **one build on one platform**:
- **Layout.** A Windows DLL and a Linux `.so` order and pad their data sections differently, even at the same fixed base. So the data ranges are different byte sequences, and pointers the game stores into the module differ.
- **Floats.** NaN payloads in memory are not canonicalized (amendment after P1-14), because raw words have no types.
- **Padding.** Struct copies from stack temporaries can carry garbage padding that depends on code generation.

That conflicts with the user's decision D5 and exit criterion E3c: identical Windows and Linux trails at Phase 1 exit. §8 already says the hash is "a reviewed, typed list... by value, never as raw struct bytes", and §9 already keeps raw memory as a debug-only "deep hash". The P1-16 hash is a deviation from §8, not a new option.

### Decision: (a), as §8 already says, with the raw hash kept as the deep hash
1. **`stateHash()` is cross-platform.** It is the hash that trails, replays, the conformance suite, E3 and netplay use. It is computed from the **hash manifest** (§8):
   - typed game state hashed by value;
   - data pointers as `(region, offset)`, where the regions are the arena, the module's data symbols and the module's code;
   - function pointers as stable IDs from the registration table;
   - floats through `update_f32`/`update_f64`.

   To keep the manifest tractable for a whole game, it may be built in two layers:
   - **Globals by symbol.** A build step writes the module's game data symbols (name, size, type class) from the game sources' object files, the same set on both OSes. The hash visits them in name order, so section layout does not matter. Symbols from the host C++ runtime, the CRT and other non-game translation units are excluded. **Game data ranges must hold game state only** (the `unordered_set` lesson): a host object found there is moved out of the game data range, or is a P1-16 fault in debug builds.
   - **Heap blocks by allocation.** `OSAlloc` and `HuMem` blocks are visited in allocation order. Each block is hashed by its type, from the allocation site's registered type, where the manifest knows it. Otherwise it is hashed as words with three canonicalizations:
     - a 64-bit aligned word inside the arena becomes its arena offset;
     - a word inside the module becomes its symbol ID and offset *(narrowed on 2026-11-07: in a code section, only at an exact function start. See "Module words: a safe rule must not depend on layout")*;
     - an aligned word holding the x86 default NaN with either sign becomes the canonical NaN *(narrowed after #55: see "Untyped words: only the default NaN's sign is merged")*.

     Canonicalization can only merge values, never split them. The cost is that an integer that happens to look like a NaN or a module pointer hides a difference.
   - **Choosing the image base.** *(Replaced on 2026-11-07: the base is now `0x5B97_9E3C_0000`. See "Module words: a safe rule must not depend on layout", point 1.)* History: this point chose `0x500000000000` on the assumption that a pair of game `s32` fields would rarely form a module address. The layout probe disproved that for `(small value, 0x5000)` pairs.
   - **Fixing failures.** Any area where Windows and Linux still differ after this (garbage padding, MS versus Itanium bitfield layout, `wchar_t`) gets a typed manifest entry. A byte-wise exception is never acceptable.
2. **The raw-memory hash becomes the deep hash (§9).**
   - It stays exactly as P1-16 built it, valid for the same build on the same platform.
   - It is exposed as a debug entry of the module ABI, not as `state_hash`.
   - It is also the **coverage oracle for E4**: on every perturbation, if the deep hash changes but `state_hash` does not, the manifest misses an area, and the test fails. That keeps the bug-finding value of P1-16's work.
3. **Staging:**
   - While the cross-platform manifest is incomplete, `Mp4Runtime` may use the canonicalized memory hash, or temporarily the raw one, for same-platform work: E3a and E3b, E4, E5 and M1.
   - **No MP4 golden trail is committed** until `state_hash` gives identical trails on Windows and Linux for the full-party replay (E3c). A `.trail.txt` is a cross-platform golden by definition (ADR 0006).
   - The first P1-16 step after this ADR is to run the same replay on Linux and diff the trails. The first differing checkpoint, with the deep-hash region diff, says which areas need typed entries.
4. **Option (b), same-platform trails, is not mine to take.** D5 is the user's decision. If E3c cannot be met by milestone **M4**, architect escalates to the user with the measured differences, to amend D5 and E3 or to extend the schedule. Until then E3 stays as written.
5. **Option (c), identical module layout on both platforms, is rejected as infeasible and insufficient.**
   - lld-link has no linker scripts, and `/ORDER` arranges functions, not data.
   - COFF and ELF differ in relocation, TLS, import and CRT-initializer layout, and in which C++ runtime statics land inside the module.
   - Even an identical layout would leave the NaN and padding problems.
6. **Save states are unaffected.** `SaveState` keeps the raw arena and registered globals (§10). They load bit for bit on the same build and platform, and `SaveState::format_version` stays the module's `state_format()`. §10's exception (same build, same platform, until Phase 2 entry) still covers the conflict with ADR 0002 rule 5. `title_runtime.h`'s comment now points to it.
7. **Cost.** 3.5–3.8 ms per `stateHash()` is acceptable for Phase 1: it runs at checkpoints, every 60 frames, which is about 0.4% of run time, although a checkpoint frame itself spends about a fifth of its 16.7 ms budget on it. Netplay's hash cadence is the Phase 2 netplay ADR's to set. The canonicalized walk should not cost much more than the raw one; P1-16 measures it.
8. **Conformance and E3.**
   - The conformance suite is unchanged: same-process and cross-process equality, and `loadState` restoring the hash.
   - E3's definition is unchanged: E3c compares each OS against the committed trail.
   - qa-reviewer adds an E4 self-test that a perturbation the deep hash sees but `state_hash` misses fails.

### ABI v1.2 `debug_perturb`: accepted, with a guard
Appending a test-only entry to the function table is the right way to add a debug hook under the one-export rule. The loader requiring only v1.1 is correct: debug entries are never required. Conditions:
- `debug_perturb` works only on a game created with a new boot flag, `MPT_MP4_BOOT_DEBUG_HOOKS`. Without it, it returns `MPT_MP4_ERR_ARGUMENT` and changes nothing, so a normal session (live, replay or netplay) can never be perturbed by accident.
- The flag is a debug flag: like `DEBUG_COPY_TRAP`, it is not hashed.
- The deep-hash entry from point 2 goes in the same minor version or the next, under the same flag.

### Untyped words: only the default NaN's sign is merged (architect, 2026-11-06)
runtime-gc-wii implemented the cross-platform `state_hash` (#55). It deviated from the rule above, which canonicalized every NaN bit pattern in untyped words, and the deviation is correct. **Accepted, with one extension.**
- **Why the original rule was wrong.** An untyped 32-bit word with a NaN bit pattern is far more often an integer than a float:
  - every `s32` from -1 to -8,388,607 (`0xFF800001`–`0xFFFFFFFF`) has one;
  - so does every value from 2,139,095,041 up (`0x7F800001`–`0x7FFFFFFF`).

  Merging them all would hide real divergence in common game values such as -1 sentinels and small negative offsets. "Merge, never split" is only safe when the merged set is small.
- **The rule now:**
  - **Typed float fields** (`update_f32`/`update_f64`, amendment after P1-14) still merge every NaN.
  - **In untyped words**, only the sign of the **x86 default NaN** is merged. The f32 values `0x7FC00000` and `0xFFC00000` hash as one value, and an aligned 64-bit word holding the f64 default NaN, `0x7FF8000000000000` or `0xFFF8000000000000`, hashes as one value. The f64 case is my extension: #55 should add it if it doesn't already. Every other bit pattern is hashed exactly.
- **Why that is enough in practice:**
  - Both of our platforms run the same Clang on the same ISA (x86-64, SSE).
  - An invalid operation produces the default NaN, which on x86 has the sign bit set.
  - The cross-codegen difference we expect is in which operand's NaN propagates, and the sign of that NaN after negation, `fabs` or `copysign` along different paths. Both cases are the default NaN with either sign.
  - Payload NaNs, meaning anything other than the default NaN, only appear if game data or a bit cast creates one, and MP4 has no known source.
  - ARM64's default NaN is `0x7FC00000`, so the same rule covers a later ARM64 target.
  - Merging one specific pair of 32-bit values, -4,194,304 and 2,143,289,344, costs almost nothing in hidden divergence.
- **If the Linux comparison run finds another NaN difference**, the fix is a typed manifest entry for the field that holds it (by symbol or allocation site), hashed through `update_f32`/`update_f64`. The untyped rule is never widened.
- **Cost and evidence:**
  - `state_hash` at 5.8–6.0 ms per checkpoint, every 60 frames, is about 0.6% of run time. That is accepted for Phase 1. The Phase 2 netplay ADR sets its own cadence and may ask for it to be faster.
  - The deep hash takes 3.95 ms.
  - Perturbation passes 60 of 60 fixed cases and 640 of 640 sampled.
  - The layout-probe build, which shifts every function and global, keeps `state_hash` identical while the deep hash changes. That is direct evidence for the cross-platform claim, so **it becomes a standing regression check** on the dump lane, run with the E4 perturbation tests.

## Module words: a safe rule must not depend on layout (architect, 2026-11-07)
### Context
The layout probe on PR #66 found an integer pair that the untyped-word rules read as a module pointer (`docs/titles/mp4.md` §20):
- A game struct in a `HuMem` block holds two aligned `s32` fields, `0x216e` and `0x5000`. A neighbouring struct holds `0x598e` and `0x6000` in the same slot.
- Read as one 64-bit word, the first pair is `0x00005000_0000216e`: module base + `0x216e`, inside `.text`.
- The module rule maps it to a function and an offset: `CoinChgDisappear+0xe` normally, `mpt_layout_probe_code+0x116e` under the ON probe, and something else on Linux. So `state_hash` moves whenever code moves. There were 14 hits by frame 6,000.

"Choosing the image base" assumed such pairs are rare. For `0x5000`, a plausible flag word or size, they are not.

runtime-gc-wii proposed canonicalizing a code word only when it is exactly a function symbol's address, and hashing every other in-module code word raw. The alternatives were (a) a typed manifest entry for that allocation site, and (b) also moving the module base.

### The criterion
A canonical rule for untyped words is safe against false positives only if **its value for a given raw word is the same on every platform**. A word that is really an integer then hashes the same everywhere, even when the rule misreads it.
- **The arena rule passes.** Its value is a tag plus `w - arena_base`. The arena base and the arena layout are the same on both OSes (§7, §11), so a false positive in the arena range merges at most. It never splits.
- **The default NaN rule passes.** It maps two fixed bit patterns.
- **The module rule fails, in every section.** Its value depends on which section and symbol the offset falls in, and the DLL and the `.so` lay out sections differently:
  - The offset `0x216e` is in `.text` on Windows. On Linux, lld's first segment at the base holds the ELF headers, `.dynsym`, `.dynstr`, relocations and usually `.rodata` before any code, so the same offset is probably not code at all.
  - A false positive in `.data` maps to a data symbol and an offset, which depend on layout.
  - A false positive in `.rodata` hashes the text of whatever literal sits at a layout-dependent address.

So the proposal alone does not fix this hit. On Windows the word would hash raw, but on Linux it would probably hash as a literal's text or as the unknown sentinel. **The real fix is to make false positives in the module range improbable, and only the base can do that.** A narrower code rule is still worth having as a second defense.

### Decision: (b) plus the narrowed code rule, with (a) for anything left over
1. **Move the module base to `0x5B97_9E3C_0000`** (`layout.cmake`, §11), replacing `0x5000_0000_0000`.
   - **Both halves are now implausible.** A false positive needs an aligned 64-bit word whose upper `s32` is exactly 23,447 (`0x5B97`) and whose lower `u32` lies in `[0x9E3C_0000, 0x9E3C_0000 + image size)`. As an `s32`, that lower half is about -1.64 billion. As an `f32`, it is a tiny negative value of the order of -1e-20. Game integer pairs, flag words and coordinates do not take that shape. The old base needed only `(small value, 0x5000)`.
   - **The image must not cross a 4 GiB boundary**, so that every module address has the same upper half. `0x9E3C_0000` leaves about 1.5 GiB of room. The host's base check at `boot()` also checks that the module's end stays at or below `0x5B98_0000_0000`.
   - **`special()`'s window is unchanged.** The upper half `0x5B97` stays inside `[0x4000, 0x6000)`, so `canonFor()`'s check still holds.
   - **The arena stays at `0x4000_0000_0000`.** By the criterion above, its false positives are harmless.
   - The base is 64 KiB aligned (Windows) and page aligned (ELF). It stays inside the 47-bit user address space, away from where either OS places images, heaps and stacks by default.
   - **Save states.** States from builds at the old base do not load into the new build. The build fingerprint (§10) already rejects them, so no format rule changes.
2. **In a code section, only an exact function start is canonicalized.**
   - A module word in a code section becomes a symbol ID only if it equals the address of a **function symbol**: `STT_FUNC` on ELF, or on COFF a symbol whose complex type is `IMAGE_SYM_DTYPE_FUNCTION`. Section symbols, labels and untyped symbols do not count. Neither do layout-probe symbols, which exist only in the ON build.
   - Every other word in a code section is **hashed raw**. That covers code interiors, padding between functions, and code below a section's first function symbol.
   - **C++ symbols follow the same rule in code.** A C++ function start becomes the one C++ value (the `cxx` class, since MSVC and Itanium mangle differently). A word inside a C++ function hashes raw. In data and read-only data the `cxx` rule is unchanged.
   - The rule needs no symbol sizes, so ELF and COFF follow it the same way.
   - **Why it only narrows: game state never legitimately holds a mid-function code address.**
     - Coroutine records hash the **entry** function (`mpt_co_stack_record`), and the saved stack pointer as an offset.
     - Saved continuations (return addresses and callee-saved registers) live on coroutine stacks, whose contents are never hashed.
     - Heap headers hold a constant caller address, not a return address (`mp4.md` §11).
     - Callbacks, process entries and overlay hooks are function pointers, so they point at function starts.

     A genuine interior code address in hashed memory would be a new kind of state. Hashed raw, it stays deterministic on one platform, and E3c flags it across platforms. That is the right outcome.
3. **Outside code sections:**
   - **Data and read-only data keep the current rule.** Data pointers legitimately point into an object (an array element, a struct field), and string pointers into a literal, so these sections cannot be narrowed the same way. The base move in point 1 protects them.
   - **A module word in no section is hashed raw.** That covers the PE and ELF headers, and any gap between sections. No game state points there. The unknown sentinel that `canonical()` returns today for a word outside every section is dropped, because it merged values that no legitimate pointer produces. The sentinel remains only for a word inside a data or read-only data section with no symbol at or below it.
4. **Function addresses must be unique, and the same set on both platforms.**
   - Identical code folding (ICF) could merge functions differently under lld-link and ld.lld. That would change which name a start canonicalizes to. It would also change game behaviour wherever the game compares callbacks.
   - The module links with ICF off on both OSes (`/OPT:NOICF`, `--icf=none`), and the build states this explicitly instead of relying on defaults.
   - **No incremental linking** (`/INCREMENTAL:NO`, stated explicitly next to `/OPT:NOICF`). An incremental link routes function addresses through incremental-link-table thunks, so a stored function pointer would be a thunk address, not a function start. If `link.exe` cannot be held to this, its path in `layout.cmake` is dropped and lld-link becomes the only Windows linker for the module.
5. **(a) remains the fix for any hit left over.**
   - If E3c or the dump lane still finds a false positive after points 1 and 2, the field gets a typed manifest entry for its allocation site or symbol, as "Fixing failures" says.
   - The untyped rules are never widened.
6. **Acceptance:**
   - Synthetic unit tests on both the COFF and the ELF reader, with no game material:
     - an exact function start canonicalizes to its ID;
     - an interior code address, padding between functions and a header address hash raw;
     - a C++ function start gives the `cxx` value, and a word inside a C++ function hashes raw;
     - a data or label symbol in a code section is not a start;
     - a probe symbol is not a start.
   - **Real linkers keep the function type.** The synthetic stub module in `tests/mp4_loader`, linked by the real lld-link and ld.lld, reports a static C function, an extern C function and a function written in assembly as function starts. This shows that `IMAGE_SYM_DTYPE_FUNCTION` and `STT_FUNC` survive both linkers. Hand-written assembly needs explicit type directives (`.type sym,@function`, `.def`/`.scl`/`.type 32`), and the test covers that.
   - The host rejects a module loaded at the old base, and a module that crosses the 4 GiB boundary.
   - On the dump lane, the layout-probe run (ON against OFF) gives identical `state_hash` trails. **The gate is the full party played to `finished`, with exit 0 on both lanes.** The layout test's default (`--party --frames 6000`, where both lanes exit 1) is a quick check, not this gate.
   - In that run, `count module_code_interior` is 0, and `count module_unknown` is 0 or each hit is explained, with a typed entry per point 5. *(Tightened on 2026-11-08: in heap blocks every module count is 0, after game heaps return zeroed blocks. See "Stale heap bytes", Acceptance.)*
   - One dump-lane run writes a note for **every** module hit, not only C++, unknown and code-interior hits. runtime-gc-wii checks that each one is a real pointer and records the counts in `mp4.md` §20.
   - E3c, the Windows and Linux replay, is the test that decides it.

### Alternatives
- **The proposal alone.** Rejected as insufficient. The section and symbol an offset falls in are themselves layout-dependent. A code-only rule leaves false positives in data and read-only data, and a hit that is code on one OS can be something else on the other.
- **(a) alone.** Rejected as the primary fix, because it is whack-a-mole: every struct that holds a `(small, 0x5000)` pair would need its own entry. It is kept for leftover hits (point 5).
- **No module canonicalization in untyped words**, so that module pointers appear only in typed entries. Not now. E3c would need a typed entry for every heap struct that stores a function pointer before it could pass, and the base move makes the false-positive risk small.
- **A base outside `special()`'s window.** Not needed. A non-round upper half inside the window works as well, and the fast path stays as it is.

### Consequences
- runtime-gc-wii changes:
  - `layout.cmake`: the base, and the link flags (`/OPT:NOICF`, `/INCREMENTAL:NO`, `--icf=none`);
  - the end-of-image check: it lives in the **host's base check** in `mp4/host`, at `boot()`, next to the existing base check, using the image size from the loaded module's headers. It does not live in `ModuleSymbols::load`, which is a hash helper and must not decide whether a module may run;
  - in `ModuleSymbols::canonical()`: the code-section rule, the C++ rule in code, and raw words outside every section; plus a function-symbol flag in both symbol readers;
  - the comment on `special()`'s window in `abi.cpp` (line 515 on main), which names `0x5000'0000'0000`.
- `docs/titles/mp4.md`, kept by runtime-gc-wii:
  - §20 records the new base and closes its open item;
  - the fixed-address table (line 783) gets the new image base;
  - the cost note on the check (line 869) names the new base.
- In the amendment after P1-16 hash coverage, point 1, "a word inside the module becomes its symbol ID and offset" now holds *in a code section, only at an exact function start*. "Choosing the image base" is replaced by point 1 above.
- `module_code_interior` stays as a diagnostic count, although those words now hash raw. It is expected to be 0 at the new base.
- The layout-probe standing check (amendment of 2026-11-06) stays, with the acceptance checks of point 6 added.

## Stale heap bytes: game heaps return zeroed blocks on PC (architect, 2026-11-08)
### Context
runtime-gc-wii implemented "Module words" (branch `mp4-code-word-canonicalization`). With the new base, `module_unknown`, `module_raw` and `module_misaligned` are 0 over the full party, and rodata against OFF is identical for seeds 1–5 at 20,000 frames. Two findings remain that no untyped rule can handle (`docs/titles/mp4.md` §20):
1. **The full-party layout gate fails at frame 82,000** (Candlelight Flight, `m416Dll`). One canonical heap word differs, `0x00000001_9E41D410`: the low half of `UpdateEffect`'s address with `0x00000001` above it, in a particle record's `animTime`/`blendMode` slot. An earlier record 0x40 lower had its `hook` at exactly that address. The bytes are stale: the new record wrote the upper half and never wrote the lower half.
2. **`module_code_interior` = 3,315 hits, 16 distinct words**, such as `0x5B97_9E6F_0000` and `0x5B97_9E74_50EC`, in `HuMem` blocks of `m417Dll`, `m430Dll`, the item pick and the multi-view camera. They are module addresses whose low 16 bits have been overwritten: stale pointers partly overwritten by a 16-bit field. They do not split the probe trail, but their value is whatever the earlier occupant left, so they would differ on Linux.

There is a common cause. `HuMem`, `OSAlloc` and the sub-allocators that run inside their blocks return blocks holding whatever the previous occupant left. On a 64-bit host that is often half of an 8-byte pointer. The console used 4-byte pointers, so the PC layout already differs there. Stale bytes are history, not state: they are deterministic on one build and platform, but their content depends on what earlier allocations stored, and that includes host pointers and layout. This also matters for game behaviour, not just the hash: a field read before it is written reads those same bytes, so it can **behave** differently on Windows and Linux, which D5 forbids.

### Decision: zero every block on allocation, in every game heap
**Scope.** The rule applies to every GameCube/Wii title on this runtime, not only MP4. `OSAllocFromHeap` is in the shared SDK (`runtimes/gc_wii/sdk`), so its zeroing covers MP5–MP8 as soon as they boot on it. Each title's own heap and sub-allocators (its `HuMem` successor, its sound heap, its ARAM allocator) get the same patches when the title is ported, and its `docs/titles/<id>.md` §11 lists them. MP4 is the first title, and the evidence below is MP4's.

1. **Every byte of a block's user area is zero when the allocator returns it**, for every allocator whose memory the hash reads. That is each level of allocation, not only the outermost:
   - `HuMem`: the static `HuMemMemoryAlloc2`, which both `HuMemMemoryAlloc` and `HuMemMemoryAllocNum` go through, so `HuMemDirectMalloc`, `HuMemDirectMallocNum` and every heap built on them are covered. This is a Party Board patch in our series.
   - The SDK's `OSAllocFromHeap`.
   - **To the block's end, not the requested size.** The hash reads the whole block, so the zeroed range is everything after the header up to the block's end: `block->size - header` in `HuMem` and `b->size - kHeader` in the SDK's `OSAllocFromHeap`. In both, a block can be larger than the request when the remainder is too small to split. *(Corrected on 2026-11-09: this said `need - kHeader` for the SDK, which misses the unsplit tail; see "Gate status after the implementation", finding 3.)* Every sub-allocator below follows the same rule.
   - **msm's sub-allocator.** msm takes one `HuMemDirectMalloc(HEAP_MUSIC, …)` block at boot, hands it to `msmMemInit`, and from then on sub-allocates inside it with its own first-fit allocator, `msmMemAlloc`. MusyX's `salMalloc` reaches that allocator through `sndSetHooks`. The hash reads the whole msm block, freed sub-blocks included, so zeroing the outer `HuMem` block once at boot does not help. `msmMemAlloc`, and `msmSndMalloc` if it is a separate path, zero on allocation, in a Party Board patch of their own.
   - **ARAM: `HuARMalloc`.** ARAM in use is hashed, so `HuARMalloc` blocks zero on allocation as well. Emulated ARAM lives in the arena (§7), so this is a plain `memset`. ARAM is not exempt: its contents are DMA'd from main RAM as well as from disc, so a stale ARAM block can carry host-layout bytes too.
   - **The general rule:** any sub-allocator that hands out pieces of a game-heap block, or of emulated ARAM, zeroes on allocation by the same rules. runtime-gc-wii lists every such allocator in `docs/titles/<id>.md` §11 (for MP4: `HuMem`, `msmMemAlloc`/`msmSndMalloc`, `HuARMalloc` and the SDK's `OSAlloc`), and a newly found one is added there and patched before its title's gate counts as met. A game pool that reuses fixed slots without an allocation call is not an allocator in this sense; point 6 covers it.
   - **No realloc exists today.** `HuMem` has none, MSL's `realloc` is a stub and the SDK has none. If a realloc-style path is ever added, it keeps the old contents and zeroes the grown tail to the block's end.

   This is part of the PC build, the same on every platform. It is never a runtime option or a boot flag, and it goes into `build_id` through the patch set and the SDK sources, as any patch does.
2. **On allocation, not on free.**
   - Zero-on-alloc gives one invariant that holds whatever the block's history was: a fresh block is all zero. That includes bytes that held the allocator's own free-list headers before blocks were split or merged, which zero-on-free never reaches.
   - Zero-on-free would also change what a use-after-free reads. That is a separate class of bug, and hiding it is not this ADR's job.
   - The arena is already zeroed at `boot()` (§7), so zeroing a block that was never used before changes nothing.
3. **"Only blocks reused after free" is rejected for now.** Tracking it needs a per-heap high-water mark in game-visible state (saved, restored and covered by perturbation), and that is only worth it if point 5's cost measurement says so. If it is ever added:
   - The mark covers **every byte the heap has ever written**, including the allocator's own split headers (the header written for the free remainder when a block is split), not only the user areas it handed out. Otherwise a block placed over an old split header skips zeroing and keeps those bytes.
   - The mark lives in the arena or a registered global.
   - It must give exactly the same bytes as zeroing every block. The test is the full-party replay run twice on one build and platform, once zeroing every block and once with the mark: their deep-hash trails (§9) must be identical, with the mark's own storage excluded from the comparison.
4. **Why this does not change game behaviour that matters.**
   - The decomp's intent, and the console's, is that a field is written before it is read. Where that holds, zeroing changes only bytes no code reads.
   - Where a field is read before it is written, the console read undefined stale data, and the PC port already reads different stale data (different layout, 8-byte pointers). Zero is one fixed, cross-platform value in place of an arbitrary one, and the value a heap held early in its life.
   - Game code that depends on a non-zero stale value would be a port bug. It is found and fixed with a patch, as with any port bug.
   - Each title's `docs/titles/<id>.md`, starting with `mp4.md`, records "PC game heaps return zeroed blocks" as a deliberate divergence from the console, next to the constant heap-header caller (§7).
5. **Cost.**
   - Zeroing costs memory bandwidth for the bytes allocated, not for the arena size: the 112 MiB is never cleared in one go after boot. Steady-state frames allocate kilobytes. Overlay and file loads allocate megabytes, a few milliseconds at most on a load frame, and the load overwrites most of it anyway.
   - **Boot clears nearly the whole arena once.** `HuMemInitAll` takes every `HuMem` heap from `OSAlloc` at boot, so the SDK's zeroing clears almost all of main RAM in the boot frames. The arena is already zero then (§7), so this changes no bytes. It costs a one-time `memset` of about 112 MiB, tens of milliseconds, and boot frames are exempt from the budget below.
   - **Double zeroing is harmless.** Where one allocator's block becomes a heap for another, the same bytes are zeroed twice: for example, the outer `HuMemDirectMalloc` block that holds a process's heap is zeroed when it is allocated, and the `HuMemMemoryAlloc` sub-allocations inside it zero the same bytes again. That costs a second pass over the same bytes. It is not optimised away unless point 5's measurement asks for it, and any such optimisation must keep point 1's invariant at every level.
   - **The budget:** **under 2% more wall time** over the full-party replay, and **no steady-state frame worse by more than 0.25 ms** at the 99th percentile.
   - **Exempt frames are defined by game-visible events,** so that both builds exempt exactly the same frames: the boot frames up to the first frame of `bootDll`; any frame in which the overlay changes; and any frame in which a DVD read or an ARAM DMA completes. The runtime reports these frames from the SDK's own counters, never from timing.
   - **Method:** the same machine, both builds (the current one and the zeroing one), the same full-party replay, N ≥ 5 runs of each. Report the median of each run's total wall time and the median of each run's p99 over non-exempt frames, and record both numbers and N in `mp4.md` §20.
   - Over budget, the remedy is point 3's high-water mark, not per-site exemptions. A site that skips zeroing because its data is read from disc would need an amendment here, backed by a measurement.
6. **What is left over gets fixed at the source.** Zeroing removes bytes left by an earlier **allocation**. It cannot remove bytes left inside one block when the game reuses its own pool slots, or when a struct reuses a field as a different type.
   - **A field the game reads**: a Party Board patch initialises it at the allocation or pool-reuse site (option (ii)). A typed entry that skips such a field would hide divergence in a value that changes behaviour.
   - **A field nothing reads** (padding, a union's dead arm): a typed manifest entry for the allocation site (option (iii); "Module words", point 5).
     - **How the entry finds its block.** Heap headers carry a constant caller (§7), so they cannot name the site. The entry identifies its block by the `HuMem` `num` tag the site passes to `HuMemDirectMallocNum`, where that tag is unique to the site, or else by an owning pointer read from a hashed global or from a block that is itself typed. Allocation order alone is never used, because it shifts whenever any earlier allocation changes.
     - **"Nothing reads it" is recorded, not assumed.** `mp4.md` §20 lists, for each skipped field, every read of it in the pinned source (or that there is none), and why none of them can see the stale bytes. qa-reviewer checks that list at merge.
   - The untyped rules are never widened, and no hit is waived.
7. **C++ runtime pointers in heap blocks.** The one `module_cxx` hit, a pointer to `std::exception`'s RTTI in a `HuMem` block, is a host C++ runtime pointer in game memory. That goes against §11 ("nothing in the arena points into host memory") and the "game state only" rule of the amendment after P1-16 hash coverage, even though it points into the module and the `cxx` class hashes it as one value on both platforms.
   - If the run after point 1 no longer shows it, it was stale, and `mp4.md` §20 records what put it there if that can be found.
   - If it is still there, the block holds a **live** host object. runtime-gc-wii finds the allocation (site, size, allocation index) and the code that placed it, and moves that object out of game heaps. A C++ exception thrown in game code is a defect in any case (amendment after P1-13: no exception crosses a switch).
   - In data and read-only data symbols, `cxx` hits stay as "Module words" has them.

### Acceptance (replaces the dump-lane counts in "Module words", point 6)
The rest of "Module words" point 6 stands: the synthetic reader tests, the real-linker stub test and the base checks. The dump-lane part now reads:
- **Unit:** a synthetic `OSAlloc` test in the SDK tests, and synthetic `HuMem`, `msmMemAlloc` and `HuARMalloc` tests where those allocators can be built without game material (otherwise a dump-lane check of the same thing). A block is filled with a pattern, freed and reallocated (whole, split, and merged with a neighbour) and reads back as all zero to the block's end, including a block larger than its request. If a realloc-style path is ever added, its test checks that the old contents are kept and the grown tail is zero.
- **Layout gate:** the layout probe (ON against OFF) over the **full party played to `finished`**, exit 0 on both lanes, gives identical `state_hash` trails.
- **Counts over that run, in heap blocks:** `module_code_interior`, `module_unknown`, `module_raw`, `module_misaligned` and `module_cxx` are all 0. A non-zero count is fixed by point 6 or 7 before the gate counts as met. "Explained" is no longer enough in heap blocks.
- **Notes:** one run writes a note for every module hit, and runtime-gc-wii checks that each is a real pointer, as before.
- **Regression:** rodata against OFF, seeds 1–5 at 20,000 frames, stays identical. Conformance and E4 perturbation pass, including the deep-hash coverage self-test.
- **Cost:** the point 5 numbers are within budget and recorded in `mp4.md` §20.
- **Re-recording.** Zeroing changes heap contents, and so `state_hash`, from the first reused block onward. The same-platform MP4 hashes are re-recorded deliberately in the same change: the M1 hashes in `mp4.md`, the M4-01 final hash in `mp4.md`, and the local full-party replay's trail, which stays uncommitted. The fake-runtime golden is unaffected. No cross-platform golden exists yet (amendment after P1-16 hash coverage, point 3), so none needs re-recording.
- E3c, the Windows and Linux replay, still decides.

### Alternatives
- **(ii) Per-site patches alone.** Rejected as the primary fix. There are already at least five sites (the particle records and the four block types above), and every board and minigame not yet played to the end can add more. Each miss is a cross-platform behaviour risk, not only a hash one. Kept for leftovers (point 6).
- **(iii) Typed entries per allocation site alone.** Rejected as the primary fix. No allocation site has a registered type yet, and heap headers carry a constant caller (§7), so each entry needs its own way to find its block (point 6). A typed entry that skips a stale field also hides it without making behaviour cross-platform. Kept for dead fields (point 6).
- **Zero-on-free.** Rejected (point 2).
- **Hash only the bytes the game has written** (write tracking). Rejected: instrumenting every store in game code is far more expensive and invasive than a `memset` at allocation, and a read-before-write would still behave differently across platforms.

### Consequences
- runtime-gc-wii: new Party Board patches (next in the series) for `HuMemMemoryAlloc2`, for msm's `msmMemAlloc`/`msmSndMalloc`, and for `HuARMalloc`; zeroing in the SDK's `OSAllocFromHeap`; the allocator list in `mp4.md` §11; the unit tests; the cost measurement; the re-recorded same-platform trails; and `mp4.md` §11 (the divergence note) and §20 (the results, closing the open item on layout-dependent words).
- qa-reviewer checks at merge that the zeroing is unconditional in the PC build, that the trails were re-recorded in the same change, and that the counts come from the full-party run.
- No core contract changes. `kCoreContractVersion`, the module ABI version and the save-state layout stay as they are. The build fingerprint (§10) already keeps states from before and after apart.

## Gate status after the implementation (architect, 2026-11-09)
### Context
runtime-gc-wii implemented "Module words" and "Stale heap bytes" on `mp4-code-word-canonicalization` (`docs/titles/mp4.md` §11, §20). Windows, seed 1:
- **Met:** the synthetic reader, real-linker and allocator tests; the base checks; rodata against OFF, seeds 1–5 to 20,000 frames, identical; `--perturb` 0 failed; `--conformance` 0 problems, 70 of 70 targets; the cost, −1.3% total and −0.03 ms at p99, within budget; trails re-recorded. Over the full party, `module_cxx`, `module_unknown`, `module_raw` and `module_misaligned` are 0.
- **Not met:** the layout gate. ON and OFF are identical to frame 104,000 and differ from frame 105,000 (turn 9, on the board). Before zeroing they split at 82,000. `module_code_interior` is 58, which is one word, at most once per checkpoint.
- **The split** is two words, 40 bytes apart, in the matrix section of a loaded HSF model (`HSFV037`) in a `HuMem` block. One is `0x00005B979E745100`, a full code address inside `m430Dll`, which is not a function start and is seen long before `m430Dll` runs. The other is `0x0000727D9E727524`, the low 32 bits of a code address. Under ON both move by the same `0x2814`, so they come from code addresses, not from the file. They are written **after** the block was allocated, so zeroing cannot reach them.

Three further findings came with the report:
1. msm's PC block header is 48 bytes, but `msmMemAlloc` sizes and places blocks as if it were 32. So a nearly full msm heap can run past its end into the next `HuMem` block. This predates the branch, and patch 0027 clamps its own zeroing to msm's heap end.
2. The state hash covers ARAM only up to the SDK's `ARAlloc` top. `HuARMalloc` hands out ARAM from `0x20` to `ARGetSize()`, over a range that also contains msm's `ARAlloc` region. Most `HuARMalloc` memory is therefore probably not hashed.
3. The SDK zeroes `b->size - kHeader`, not `need - kHeader` as "Stale heap bytes" point 1 said.

### Decision
**1. The frame-105,000 split is a port bug, and it is fixed at its source.**
- **Why it is a bug.** Two code-derived values sit in a model's matrix data, and the game reads that data: the renderer and any code that takes a bone or node transform. The value depends on where code is linked, so it differs between Windows and Linux. That breaks D5 and rule 4 whatever the hash does.
- **The likely causes are all port bugs:** reading uninitialised stack or heap memory, writing through a stale or dangling pointer, or a 32/64-bit width or layout mismatch (for example, a loader or fixup that writes an 8-byte host pointer, or one half of it, into a 4-byte file slot).
- **No typed entry by default.** Point 6 of "Stale heap bytes" allows a typed entry only for a field nothing reads. Matrix data is read, so a typed entry is ruled out unless the investigation below shows otherwise, with the read list point 6 requires. A write into memory the writer does not own is always fixed by a patch, even if nothing reads the bytes, because it is a memory-safety bug.
- **Finding the writer (runtime-gc-wii).**
  - The run is deterministic and the arena is at a fixed address, so the word's address is the same in every run of one build.
  - Bisect on OFF to the first frame where either word holds a module address: per-frame hash items or notes over the window between the last clean checkpoint and the first bad one.
  - Then set a data watchpoint on that arena address for that frame. That can be a hardware watchpoint in a debugger, or a test-only write check built like the layout probe. Neither may change game state, and neither ships in a normal build.
  - Optional aid: a test-only lane that fills coroutine stacks with a fixed non-zero pattern at creation. If its trail differs from the normal lane's, something reads uninitialised stack. Keeping it as a standing check would be a separate ruling.
- **If the writer cannot be found, or the fix needs more than a Party Board, aurora or SDK patch,** the work comes back to architect with the evidence. Nothing is waived, and the untyped rules are never widened.
- **Acceptance.** Every item must be met before the gate counts as met:
  - `mp4.md` §20 records the writer for both words: the function, the call path, the file and line in the pinned source, the class of the cause, and why the value is code-derived. If one writer explains both words, one record covers both.
  - The fix is a patch at the source: Party Board, aurora or the SDK, whichever owns the code. It gets a synthetic regression test where the code can be built without game material, such as a loader or fixup fed a hand-made input. Otherwise the regression check is the dump-lane rerun below.
  - Over the full party on OFF, every heap module count is 0, including `module_code_interior`, with notes for every hit as before.
  - **The layout gate:** ON against OFF over the full party played to `finished`, both lanes exit 0, identical trails, on **seed 1 and seed 2**. Seed 2 is added because each fix so far has moved the split later into the same seed-1 party, so later boards and minigames have been covered by one seed only. A split on seed 2 is handled by the same procedure.
  - The regression set still passes: rodata against OFF (seeds 1–5, 20,000 frames), `--perturb`, `--conformance` and the deep-hash coverage self-test.
  - Same-platform hashes are re-recorded in the same change if the fix changes state, as in "Stale heap bytes", "Re-recording".
  - Finding 2's coverage (point 3 below) is in place before the gate is run for acceptance.

**2. The branch lands now as its own PR, with the gate recorded as open.**
- **What it does.** It implements both rulings in full and meets every acceptance item except the gate and the one `module_code_interior` word. It moves the split from 82,000 to 105,000 and removes every other class of hit.
- **The remaining split is not caused by this branch.** It is a separate port bug with its own root cause. Holding the branch would mean either growing it with unrelated investigation or stacking a fix branch on it, and both are worse than landing.
- **Conditions:**
  - `mp4.md` §20 keeps the gate and the word listed as open. It already does.
  - The PR description says the gate is open and names this amendment as the follow-up.
  - The default layout CTest (the 6,000-frame quick check) passes.
  - Nothing in the PR claims the gate is met.
  - qa-reviewer reviews the rest of the acceptance of "Module words" point 6 and "Stale heap bytes" as usual.
- **This amendment ships on the same branch,** so that the PR that lands with the gate open carries the ruling that allows it.
- **The follow-up** starts from `main` after the merge, never from the branch. It contains point 1 and finding 2 (point 3).

**3. Rulings on the findings.**
- **Finding 1, msm's heap overrun: fix in a follow-up PR (runtime-gc-wii, reviewed by qa-reviewer).**
  - **Why it must be fixed.** It is a memory-safety bug: an overrun writes into another `HuMem` block, which the same code could not do on console.
  - **Why it is not urgent.** Patch 0018 gives msm's heap large headroom, so it is not expected to trigger in normal play today.
  - **Order.** It lands after the gate follow-up, so that the two PRs do not both re-record hashes at once. The exception: if the watchpoint in point 1 shows msm as the writer, it is fixed in the gate PR.
  - **Acceptance:**
    - msm sizes and places blocks with the real PC header size (`sizeof(MSMBLOCK)`), in a Party Board patch.
    - `msmMemAlloc` fails cleanly, returning no block, instead of writing past msm's heap end.
    - A synthetic test fills msm's heap to exactly full, with guard bytes after its end, and checks that the guard bytes are untouched and that the next allocation fails.
    - A `static_assert` fixes `sizeof(MSMBLOCK)` to one value on both platforms. LLP64 and LP64 differ on `long`, so this is not automatic.
    - `mp4.md` §11 drops the open note and records msm's peak use over the full party.
    - Hashes are re-recorded if state changes.
  - It must be fixed before E3c is run for acceptance.
- **Finding 2, ARAM coverage: fix, as part of the gate follow-up (runtime-gc-wii, reviewed by qa-reviewer).**
  - **Why it must be fixed.** `HuARMalloc` blocks are game state: cached data dirs, DMA'd from main RAM and from disc. They must be hashed. Identical trails over a hash that leaves them out prove less than they claim.
  - **What is hashed:**
    - Every ARAM byte that an allocator has handed out and not freed: the SDK's `ARAlloc` range, together with `HuARMalloc`'s in-use blocks as recorded in `ARInfo`.
    - Each byte is hashed once, with the canonical walk, because ARAM can hold main-RAM bytes.
    - Free ARAM is not hashed.
  - **Where it is hashed.** The `HuARMalloc` part lives in the MP4 hash manifest (`runtimes/gc_wii/mp4`), never in the SDK, because `HuARMalloc` is MP4 code.
  - **Coverage checks.** The conformance targets and the deep-hash coverage self-test cover the new range, and perturbation can sample it.
  - **The overlap.** runtime-gc-wii checks in the pinned source how `HuARMalloc`'s range and msm's `ARAlloc` region are kept apart on console.
    - If they can hand out the same bytes on PC, that is a live aliasing bug. It gets a patch, and a check fails the run if it happens again.
    - If they are disjoint on console because of something the port lost, the patch restores it.
    - `mp4.md` §11 records the answer.
  - New hits that the wider coverage finds are handled as in point 1.
- **Finding 3, the SDK's zeroing size: confirmed, no code change.**
  - `b->size - kHeader` is right. When the remainder is too small to split, `b->size` is larger than `need`, and the hash reads the whole block.
  - "Stale heap bytes" point 1 is corrected above.
  - The synthetic `OSAlloc` test's case of a block larger than its request covers this.

### Alternatives
- **Wait for the gate before landing.** Rejected. It either grows the branch with an unrelated investigation or forces a stacked PR, and it keeps work whose acceptance is already met off `main`.
- **A typed entry for the two words.** Rejected as a first resort. The data is read, and the value differs across platforms, so a typed entry would hide a behaviour difference (point 1).
- **Keep seed 1 as the only gate seed.** Rejected. Seed 2 costs little, and one seed has been the only coverage of the later part of the party.

### Consequences
- **runtime-gc-wii:**
  - updates the PR description for `mp4-code-word-canonicalization` as in point 2, and pulls this commit before committing again on that branch;
  - after the merge, opens one follow-up branch from `main` for point 1 and finding 2, then another for finding 1;
  - updates `mp4.md` §11 and §20 as each lands.
- **qa-reviewer** reviews the current PR against point 2's conditions. For the follow-up, it checks the writer record, the patch, the seed-1 and seed-2 full-party gate runs and the ARAM coverage targets.
- **No core contract change.** The module ABI and the save-state layout are unchanged. The build fingerprint keeps states from builds before and after these fixes apart, as before.

## Gate status after the writer fixes (architect, 2026-11-09)
### Context
runtime-gc-wii did the follow-up of "Gate status after the implementation" on `mp4-layout-gate-writer` (`docs/titles/mp4.md` §11, §20; Party Board patches 0029–0031). Windows:
- **The writers are found and patched at the source.** Seed 1's two words: `SetRevMtx` (`EnvelopeExec.c:430`, 443–444) concatenates its local `sp38` when `MTXInverse` fails on a singular matrix, so uninitialised stack holding host frames reached the model's reverse matrices. Patch 0029 clears `sp38`, and the synthetic `mp4_envelope` test fails without it. Seed 2's split at frame 71,000: three `m427Dll` `map.c` functions set only `x` and `z` of hit-box Vecs, so `y` carried the low half of a module data address. Nothing reads `y` (read list in §20). Patch 0031, with no synthetic test.
- **Finding 2 is fixed.** `HuARMalloc`'s blocks overlapped other ARAM on PC, because Party Board's `TARGET_PC` base was `0x20` instead of `HU_AMEM_BASE`. The live overlap was with the SDK's reserved base [0, 0x4000), not with MusyX (point 3's correction). Patch 0030 restores the console base, a per-step alias check faults the run if it happens again, `HuARMalloc`'s in-use blocks are hashed, and the perturbation and conformance targets (72 of 72) and `mp4_armem` cover them.
- **Met:** every heap module count is 0 over the full seed-1 party, and 0 on seed 2 to frame 111,000. The layout gate on seed 1: both lanes reach `finished` at frame 250,789 with all 250 checkpoints identical. rodata seeds 1–5, `--perturb` and `--conformance` pass. Same-platform hashes are re-recorded (M1 seed 1 `7722e010cc2f09b1`, M4-01 `43198e9219fd8f37`).
- **Not met: the layout gate on seed 2.** Both lanes give identical trails to frame 111,000 and then crash identically at frame 111,514: an access violation in `byteswap_animbankdata`, from `HuSprAnimRead` ← `Hu3DAnimCreate` ← `RoundItemInit` (`REL/w01Dll/mg_item.c:116`). `main` at `8a1431d`, before this branch, crashes at the same frame and the same address.
- §11 leaves one question open: msm's own ARAM region, [`ARGetBaseAddress()`, + `aramSize`). Neither allocator hands it out, and it is not hashed. (The report first said it holds MusyX's zero buffer and stream buffers; that is MusyX's Dolphin path, which we do not build. On PC nothing writes the region. See point 3's correction.)

### Decision
**1. The branch lands now as its own PR. The seed-2 gate is recorded as blocked by the crash, and the crash moves into M4-03.**
- **Why.** The crash is not caused by this branch and is not a layout word: `main` crashes at the same frame and address, and both lanes fault on the same address, so the bad value does not move with the code layout. It is a port crash on a seed nobody had played that far, which is what M4-03, the board and seed sweep, exists to find and fix. Every other acceptance item of "Gate status after the implementation" is met. Holding the branch would mean growing it with an unrelated crash investigation or stacking a fix branch on it, and both are worse than landing (the same reasoning as that amendment's point 2).
- **The gate stays open.** The layout gate of "Gate status after the implementation", point 1, is met for seed 1 and **open for seed 2**. Nothing here waives it or drops seed 2.
- **The PR description must state:**
  - the writers and patches 0029 and 0031, and the finding 2 fix (patch 0030, the alias check, the `HuARMalloc` hash coverage);
  - that the layout gate is **met for seed 1 and not met for seed 2**, blocked by the frame-111,514 crash, which reproduces on `main` at `8a1431d` and is moved into M4-03;
  - the re-recorded hashes, by name and value;
  - this amendment, as the ruling that allows landing with the seed-2 gate open.

  Nothing in the PR, its commits or the docs claims the gate is met without the seed qualifier.
- **`docs/titles/mp4.md` must state** (runtime-gc-wii, on this branch before the PR is opened):
  - §20, seed 2: replace "This goes back to architect with the gate open for seed 2" with the ruling: open, blocked by the crash, which is an M4-03 item, rerun as in point 2 below; cite this amendment.
  - §11, msm's region: replace "open for architect" with point 3's ruling and its read list.
  - §20, patch 0031: the regression check is the seed-2 dump-lane run (point 4).
- **qa-reviewer** reviews against these conditions, against the acceptance of "Gate status after the implementation" except the seed-2 gate, and against point 4's caller check.

**2. The crash and the seed-2 gate, in M4-03 (runtime-gc-wii, reviewed by qa-reviewer).**
- M4-03 starts from `main` after both this PR and #76 (M4-04) have merged, never from either branch.
- The frame-111,514 crash is the first M4-03 fix. M4-03 is closed only when, in addition to its sweep table:
  - `mp4.md` records the crash's cause (function, call path, file and line in the pinned source, class of cause) and the fix, as a Party Board, aurora or SDK patch. It gets a synthetic regression test where the code can be built without game material (for example `byteswap_animbankdata` fed a hand-made anim bank); otherwise the dump-lane rerun is the check.
  - **The layout gate on seed 2:** ON against OFF over the full party played to `finished`, both lanes exit 0, identical trails. A new split is handled by the procedure of "Gate status after the implementation", point 1.
  - Every heap module count is 0 over the full seed-2 party, with notes for every hit.
  - The seed-1 gate and the regression set (rodata seeds 1–5, `--perturb`, `--conformance`, the deep-hash coverage self-test) still pass, and hashes are re-recorded if the fix changes state.
- **Until the seed-2 gate is met,** "Module words" and "Stale heap bytes" are not complete, and E3c is not run for acceptance. E3c runs for diagnosis are fine.
- **Order with finding 1** (msm's header overrun). That PR was ordered after the gate follow-up so that two PRs do not re-record hashes at once. It may now go after this PR, before or after the M4-03 crash fix, but the two land one after the other from `main`, not in parallel.

**3. msm's ARAM region: excluded from the state hash, with a reason and a check. Owner: runtime-gc-wii (qa-reviewer reviews).**

*Correction (2026-11-09, same day, after runtime-gc-wii checked the build and architect checked the source).* The first version of this point said the region holds MusyX's zero buffer and stream buffers, and gave "derived audio state that only the DSP reads" as the reason. That describes MusyX's `MUSY_TARGET_DOLPHIN` path. Our MusyX is built with `MUSY_TARGET=0` (`MUSY_TARGET_PC`, `runtimes/gc_wii/musyx/CMakeLists.txt`). In that block of `runtime/hw_aramdma.c`, `aramInit` touches no ARAM, `aramGetZeroBuffer` returns 0, `aramUploadData` is a `memmove` between host pointers, and `aramStoreData` and `aramAllocateStreamBuffer` take `salMalloc` blocks, which are in msm's heap in main RAM and already hashed. Every `ARQPostRequest` and `ARGetBaseAddress` in MusyX is in the Dolphin block, which is compiled out. msm itself (`msm/msmsys.c`) only takes `ARGetBaseAddress` for its size check, with `skipARInit`, and starts no DMA. The ruling below replaces the first version.
- **The ruling stands: the region is excluded from `state_hash`.** The reason is now stronger and simpler. On PC nothing hands the region out and nothing writes it, so it holds the zeros the arena had at boot (§7) for the whole run. Hashing it would add a constant.
- **The condition that makes the reason true.** The exclusion holds only while the region is untouched. So:
  - `mp4.md` §11 keeps the read list (every path in the pinned sources that reads ARAM, none of which reads msm's region) and the correction note runtime-gc-wii added.
  - **The run check changes: it covers both directions.** Any ARAM DMA, main RAM to ARAM or ARAM to main RAM, whose ARAM range overlaps msm's region faults the step ("ARAM msm region: …"). On PC the region should see no traffic at all, so a write is as much a bug as a read-back, and checking only reads would miss a new writer, whose bytes would then be unhashed state. The check lives next to the alias check in the MP4 module, or in the SDK's DMA path behind an MP4-supplied range, never in `core/`. A synthetic test triggers it in each direction.
  - If the check ever fires in real play, the exclusion ends: it comes back to architect, and the region is hashed like `HuARMalloc`'s blocks, or its user is moved out.
- **Not affected: save states.** The region lies in the arena (§7) and is saved and restored with it. The exclusion is from `state_hash` only.
- **When.** Unchanged. The §11 read list ships with this PR (doc only). The run check and its test are part of M4-03's seed-2 gate work and must be in place before the seed-2 gate is run for acceptance.
- **The overlap patch 0030 removed** was with the SDK's reserved base, not with MusyX: `HuARMalloc` from `0x20` covered [0, `AR_RESERVED_BYTES` = 0x4000), which the state hash already reads as the `ARAlloc` range. Those bytes were hashed twice and had two owners. Patch 0030 and the alias check (the `ARAlloc` top at or below `HU_AMEM_BASE`, `HuARMalloc` at or above it) close that as ruled. No ruling changes because of it.
**4. Patch 0031 without a synthetic test: accepted.**
- "Gate status after the implementation", point 1, allows the dump-lane rerun as the regression check where the code cannot be built without game material, and `m427Dll` cannot be built standalone.
- The patch sets a field that the §20 read list shows is never read, so the risk is a missed site, not wrong behaviour. **qa-reviewer checks at merge** that §20 names every caller of `fn_1_15F10` and `fn_1_15FD0` in the pinned source, and that each either sets all three components or is one of the three patched sites.
- **The regression evidence** is the seed-2 run: identical trails from frame 71,000, where the split had been, to frame 111,000, with all module counts 0. The full seed-2 gate in M4-03 (point 2) repeats it to `finished`.

### Alternatives
- **Fix the crash in this PR first.** Rejected. The crash predates the branch and does not depend on the layout, the branch's own acceptance is met, and fixing it here either grows the PR with an unrelated investigation or stacks a branch on it. M4-03 is where port crashes on new seeds are found and fixed.
- **Drop seed 2 from the gate, or gate seed 2 only to frame 111,000.** Rejected. Seed 2 was added because one seed alone covered the later party, and a partial run leaves exactly that part uncovered.
- **Hash msm's region.** Rejected for now: on PC nothing writes it, so it stays all zero, and hashing it adds cost without coverage. The DMA check keeps the exclusion honest, and point 3 says when it ends.
- **Leave msm's region as an open follow-up.** Rejected: an unanswered coverage question at the gate is what finding 2 was. A ruling with a check is cheap.
- **Keep point 3's first reason (derived audio state) and its read-back-only check.** Superseded by the correction: the reason described MusyX's Dolphin path, which we do not build, and on PC the stronger invariant (nothing touches the region at all) is the true one and is as cheap to check.
- **Require a synthetic test for patch 0031.** Rejected: it would need `m427Dll` built without game material, which the pin does not allow, and the rule already provides the dump-lane alternative.

### Consequences
- **runtime-gc-wii:** the `mp4.md` §11 and §20 edits of point 1, with point 3's read list, on this branch (done in `d52d4c0`; one wording fix left: §11's last read-list sentence names the run check as ARAM-to-main-RAM only, and point 3 now makes it both directions, "ARAM msm region: …"); then the PR as in point 1. After this PR and #76 merge: M4-03 from `main`, the crash first, with the seed-2 gate and the msm-region DMA check in its acceptance; finding 1 one after the other with it.
- **qa-reviewer:** reviews this PR against point 1's conditions and point 4's caller check. For M4-03, checks the crash record, the seed-2 gate run, and the msm-region DMA check and its test.
- **architect:** M4-03's "Closed by" in `docs/phases/phase-1.md` gains the seed-2 crash, the seed-2 gate and the msm-region DMA check (same commit as this amendment).
- **No core contract change.** `kCoreContractVersion`, the module ABI version and the save-state layout are unchanged.
