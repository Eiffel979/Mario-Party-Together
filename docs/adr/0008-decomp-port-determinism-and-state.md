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
