# Dump lane (manual, local only)

The dump lane verifies committed replay fixtures against the real game, on a developer's
own machine with their own dump (ADR 0006, Phase 1 decision D4). No dump is ever placed on
CI infrastructure, hosted or self-hosted. Public CI builds the lane and reports it as
**skipped**.

## Run it

Point `MPT_DUMP_DIR` at a directory **outside the repository** that holds your dump. The
lane refuses a directory inside the repository.

Windows (PowerShell):

```powershell
$env:MPT_DUMP_DIR = "D:\path\to\dumps"
ctest --preset windows-debug -L dump --output-on-failure
```

Linux:

```bash
MPT_DUMP_DIR=/path/to/dumps ctest --preset linux-debug -L dump --output-on-failure
```

Exit codes of `mpt_dump_lane` (the CTest test `dump_lane_replays`, label `dump`): 0 when
every runnable fixture passed, 1 on any failure, 77 (reported as skipped) when
`MPT_DUMP_DIR` is unset, there is no fixture, or no fixture's title has a runtime yet.

## MP4 state-hash checks

Run both before changing MP4's hash manifest, `runtimes/gc_wii/mp4/src/abi.cpp`'s hashing or
`module_symbols.cpp`, and record the results in the PR (ADR 0008, `docs/titles/mp4.md` section 20):

1. **Coverage, with the deep hash as the oracle.** `mpt_dump_lane --boot mp4 --frames 10000 --every 1000
   --perturb --perturb-samples 64`. Every check must print `ok`.
2. **Layout independence (the layout probe).** Configure a second host build with
   `-DMPT_MP4_RECIPE_LAYOUT_PROBE=ON` (or `rodata`) and
   `-DMPT_DUMP_LANE_LAYOUT_REFERENCE=<the normal build's mpt_dump_lane>`, then run
   `ctest -L dump -R layout_perturbation` there (`dump_lane/layout_perturbation.cmake`). It plays the
   same autopilot party (`--party --every 10`) on both lanes in separate processes and requires the two
   `frame N hash` trails, exit codes and final `dump lane: party` lines to be identical. It first
   checks with `mpt_dump_lane --layout-probe` that the reference is a normal build and the other a
   probe build, and reports SKIPPED when a lane skips (exit 77).
   - `dump_lane_layout_perturbation` is the quick check: 6,000 frames, where both lanes exit 1.
   - `dump_lane_layout_perturbation_full` is the gate (ADR 0008, "Module words: a safe rule must not
     depend on layout"): the whole party to `finished`, and both lanes must exit 0. It takes hours.

   On a direct `cmake -P` run, `-DSEEDS=1;2;3`, `-DFRAMES=N` and `-DFULL=ON` choose the runs, and
   `-DHASH_ITEMS_DIR=<dir outside the repository>` also tallies the canonical rules over every
   checkpoint (`--party --hash-items`): `module_code_interior` must be 0 on both lanes, and
   `module_unknown` and `module_raw` are printed for review. ON moves every function, every game
   global and the game core's read-only data; `rodata` moves only the read-only data. The option is
   test only: it needs `MPT_BUILD_TESTS` and the recipe, is refused in CI presets, and changes the
   module's build ID. If a trail differs, run both lanes with `--boot mp4 --hash-items FILE` at the
   first differing checkpoint and diff the files; the `note misaligned`, `note code_interior`,
   `note raw` and `note unknown` lines name words the canonical rules leave layout-dependent.
   `--note-module-hits` notes every module hit, for checking that each is a real pointer.

`--hash-items` files and captures name or show game content: keep them outside the repository.

## What it checks

For each `tests/replays/<name>.mptr` with its sidecar `<name>.trail.txt`
(`dump_lane.h`, `verify_fixture`):

1. the committed-fixture rule (`validate_replay_fixture`: canonical, no embedded save media);
2. the sidecar equals the replay's own recorded state trail;
3. `session_config_for` maps the header onto the verified dump (and a save image, if the
   replay needs one, matched by hash);
4. a `Session` started through the registry plays the replay, and `verify_replay` finds
   every checkpoint's state hash and party digest equal;
5. the trail this build produced equals the sidecar, the golden every OS must match.

`tests/unit/test_dump_lane.cpp` proves this path in CI with FakeRuntime and FakeAdapter.

## Status

`register_titles` (`main.cpp`) registers every real title the shell does (`real_titles.h`). So
far that is Mario Party 4 (P1-16). `resolve_dump` finds the title's dump in `MPT_DUMP_DIR` and
builds it with asset-import, exactly as the shell's `--dump` does. No fixture is committed yet,
so the replay run still reports skipped.

## Boot mode (M1, M2, E4)

```
mpt_dump_lane --boot <title> [--frames N] [--every N] [--seed S] [--perturb]
              [--press button@frame[+frames][,...]] [--capture DIR [--capture-every N]]
```

- `--boot <title>` boots the title on your dump and steps it `--frames` frames (default 600)
  with no input. Every `--every` frames (default 60) it prints the state hash and the video
  size; the game's OSReport text goes to the output too. Exit 0 if the runtime is still running
  at the end, 1 if it halted or faulted. It also prints the mean time of a `stateHash()` call,
  which the harness measures; the runtime reads no clock.
- `--seed S` sets the session seed (default 1). Two runs with the same seed must print the same
  hashes; that is the M1 determinism check.
- `--perturb` (MP4) boots with the module's test-only debug hooks and checks the hash coverage
  at every checkpoint (E4). For each memory class it flips one byte, requires the hash to change
  (or, for free heap and process stacks, to stay the same), flips the byte back and requires the
  original hash. Any miss makes the exit 1. Without `--perturb` the lane checks the opposite: the
  module must refuse to perturb a game booted without the hooks.
- `--press` holds buttons on player 1, which is then connected. Each press is
  `button@frame[+frames]` (default 6 frames), for example `start@900,a@1500+10`. Buttons:
  `a b x y l r z start up down left right`.
- `--capture DIR` writes the frame at each checkpoint as `DIR/frame_NNNNNN.png`
  (with `--capture-every N`, only checkpoints whose frame is a multiple of N; make N a multiple of `--every`). Captures are your own game's output: **never
  commit them**.

## Party mode (M4, MP4)

```
mpt_dump_lane --boot mp4 --party [--seed S] [--frames N] [--every N] [--watchdog N] [--replay FILE]
              [--party-board N --party-wheel FROM:TO]
              [--frame-times] [--frame-log FILE] [--audio-out FILE]
mpt_dump_lane --boot mp4 --verify-replay FILE [--frames N] [--frame-log FILE] [--audio-out FILE]
              [--capture DIR [--capture-every N] [--capture-from N] [--capture-to N]]
```

- `--party` plays a whole party through a `Session` on an autopilot (`party_autopilot` in
  `dump_lane.h`): player 1's pad is a pure function of the frame, the Director's phase, the running
  overlay and the frames since the last party change (phase, overlay or turn). It pulses START on
  the title, on the instruction screen and on the final ranking, pulls the stick and mashes A in minigames, and pulses A
  everywhere else. The default menu choices give Toad's Midway Madness, 20 turns, player 1 human
  and three COM players (mentDll asks for at least one human player).
- Stall policy: after 18,000 frames with no party change (the longest normal stretch is 10,480) the
  autopilot also pulses B and moves the stick in a 720-frame cycle: 264 frames turning it clockwise,
  then a row-by-row sweep of a 192 by 96 pixel box. A human seat needs the stick in the lottery,
  which has no time limit: the turning cranks its ball game and the sweep rubs off its scratch
  ticket. B is for a menu that only B leaves; no run is known to need it. A run that never stalls
  is unchanged.
- The oracle is `Session::director()`: exit 0 only once its phase is `finished`. The run fails on a
  halt or fault, on a soft lock (`--watchdog`, default 36,000 frames with no party change) or when
  `--frames` (default 1,000,000) runs out.
- It prints the Director's events, the raw party read whenever it changes, the peak arena use (ABI
  v1.4 `memory_use`, sampled at every checkpoint) and the save and load times at the end.
- `--replay FILE` writes the run as an `.mptr` with blank media and a checkpoint every `--every`
  frames. `--verify-replay FILE` plays one back on a fresh `Session` and checks every checkpoint's
  state hash and party digest. Run it in a separate process for the E3 check. Replays of your own
  runs stay outside the repository unless they are fixtures (below).
- `--party-board N` picks another board on mentDll's wheel, inside the `--party-wheel` window
  (frames since mentDll started). The run fails if the party read names another board. All six
  boards have been played to `finished` on seeds 1 to 3 (`docs/titles/mp4.md` §21, M4-03); board 5
  needs `--party-unlock-bowser`.
- Diagnosis: `--party-save-at N FILE` saves the state before frame N, and `--party-resume FILE`
  goes on from it (no replay, not evidence). State files hold game memory: keep them outside the
  repository. Resume in the same build only, and expect a `/GS` fail-fast (0xC0000409) in a game
  function that was suspended at the save: its frame's stack cookie was made with the saving
  process's `__security_cookie`, which differs in every process (`docs/titles/mp4.md`, M4-03).
- Diagnosis: `--press` also works in party mode and adds its buttons on top of the autopilot (to probe
  a soft lock).
- Diagnosis: `--capture DIR` also works in party mode and when playing a replay (`--verify-replay FILE`
  with `--capture`, which plays without checks, like `--audio-out`). It writes frame N as
  `DIR/frame_NNNNNN.png` when N is a multiple of `--capture-every` (default 1, independent of the
  checkpoints) and lies in `[--capture-from, --capture-to]`, to look at one minigame closely. A replay
  played this way prints the running overlay whenever it changes, to find a minigame's frames. The PNG
  holds the frame image's four channels as the runtime returns them. Captures are your own game's
  output: never commit them.
- Diagnosis: `--frame-times` prints the total step time and the p99 over steady frames (the boot,
  overlay changes and frames that complete a DVD read or an ARAM transfer are exempt). `--frame-log
  FILE` (implies `--frame-times`) writes one line per step: the frame, its step time in ms, the
  overlay and the DVD read and ARAM transfer counts. `--audio-out FILE` writes the game's audio as it
  is stepped, raw interleaved S16 PCM at the runtime's sample rate and channel count. Both work in a
  `--party` run and with `--verify-replay`, which then plays the replay without checks (like
  `--capture`), through to its end unless `--frames N` stops it first. Anywhere else the lane refuses them (exit 2).
  The audio is your own game's output: never commit it.
- `--party-unlock-bowser` boots with `MPT_MP4_BOOT_UNLOCK_BOWSERS_GNARLY_PARTY`
  (`Mp4Runtime::setTestGameplayFlags`), so `--party-board 5` can reach Bowser's Gnarly Party. No replay
  records the flag, so the lane refuses it with `--replay`, and such a run is diagnosis only.

## E5: the in-process save-state round trip, and a fresh runtime on the kept module (MP4, M4-16)

```
mpt_dump_lane --supervise --boot mp4 --e5-round-trip N [--frames M] [--every K] [--seed S] [--e5-reload-module]
mpt_dump_lane --supervise --boot mp4 --reboot-trail [--frames M] [--every K] [--seed S]
mpt_dump_lane [--supervise] --guard-reload-selftest STUB keep|reload
```

The game module stays loaded for the whole process once a runtime has loaded it (ADR 0008, "Game
module kept loaded for the process"). Every normal exit of the lane releases it through the titles'
`shutdown` (`shell::shutdown_titles`) and prints `dump lane: exit: game module unloaded`.

- `--e5-round-trip N` is E5 as narrowed (`docs/phases/phase-1.md`, M4-D): one runtime steps `--frames`
  frames with no input (or `--press`), saving the state before frame N and keeping the state hash at
  every `--every` checkpoint. The lane destroys it, boots a fresh runtime with the same seed, loads the
  state and steps frames N to `--frames`. Exit 0 only if every checkpoint after N has the uninterrupted
  run's hash and the module stayed loaded across the two runtimes, with the same handle and the same
  `/GS` cookie. On Windows it also prints how many `/GS` guard words the arena holds at the save
  (`stack_guard.h`: words that are the module's cookie XOR an address next to them, i.e. suspended
  guarded game frames). Results: `docs/titles/mp4.md` §11, "E5 in process (M4-16)".
- `--e5-reload-module` (diagnosis only, expected to fail fast) unloads the module between the two
  runtimes through MP4's `shutdown`, so the fresh runtime loads it again with a new cookie. Run it under
  `--supervise`: the saved guarded frame fails its check (0xC0000409) when it returns. It is the
  reason the module is kept loaded.
- `--reboot-trail` checks that a fresh runtime on the kept module starts as one on a freshly loaded
  module: runtimes A (`--seed`), B (`--seed` + 1) and C (`--seed`) each step `--frames` frames in turn,
  each destroyed before the next is created. It prints every checkpoint as `reboot <A|B|C> frame N hash
  H` for comparison with separate boot-mode runs (the M1 trail), and exits 0 only if C's trail equals
  A's and B's differs from it.
- `--guard-reload-selftest STUB keep|reload` needs no dump: it suspends a guarded function of a synthetic
  stub (`tests/mp4_loader/guard_stub.cpp`) on a fiber, keeps or reloads the stub, and resumes it. CTest
  runs both (`dump_lane_guard_reload_keep`, `dump_lane_guard_reload_fails_fast`) on Windows x64.

## E2: the game-side input proof (MP4, M4-07)

```
mpt_dump_lane --boot mp4 --e2 [--seed S] [--e2-rows N[,N...]] [--e2-ports P[,P...]] [--e2-cap N]
              [--e2-quick] [--e2-verbose] [--e2-items]
```

Ruling M4-E's game-side proof for every row of `docs/titles/mp4.md` section 18 (`e2.h` holds the rows,
the injection and the verdict; `tests/unit/test_dump_lane_e2.cpp` checks them against section 18 without
a dump).

- **Start.** For each row and port the lane boots a fresh runtime with `Mp4Runtime::setTestStart`: the
  module does bootDll's one-time setup, seats four human players and starts the minigame through instDll.
  In a seated category (1-vs-3, 2-vs-2, Bowser, Fortune, Item) the minigame gives roles by config slot
  (the single seat or team 0's first player is slot 0, and some item minigames create only slots 0 and
  1), so the pads rotate: slot i is played on pad (i + focus) % 4, with the port as the focus pad first.
  Other categories keep slot i on pad i. The test start is a determinism input in the state hash; no
  replay records it. The state is saved on the minigame's first frame and every run below starts from it.
- **Runs.** Idle (every pad connected, nothing pressed) twice, whose trails must be equal; then Z as a null
  control, and each listed control on the port. A row with a context set (`e2.h`: a prompted pair, a claw
  that must first be over a target, a gear shift that needs speed) proves each control in it
  leave-one-out: the whole set against the set without that control. Controls come in bursts: active
  24 frames of every 32 (buttons pulse 4 on, 4 off; the stick holds right, up, left or down; analog
  triggers hold their pull), idle for the other 8.
- **Checkpoints** are the last frame of each idle gap. The state hash covers the game's own pad arrays,
  which copy the pad every frame, so a hash taken while a control is held would differ from idle even in
  a minigame that ignores it. At the end of the gap the pad and those arrays are idle again, so a
  difference is game state the input changed. Z is the null control: no listed row reads it, but some
  minigames copy the pad into their own objects and stop refreshing the copy in some states, so a press
  can stay in game memory with no reaction behind it. Z runs first, and a listed control counts only
  where it diverges before Z does. `--e2-items` names the state-hash items each divergence changed.
- **Verdict.** A control is proven on the port when its trail leaves its baseline at a checkpoint where
  both runs are still in the minigame. In a seated category, controls still unproven with the port in
  slot 0 are tried again with the port in another seat: slot 3 (a team member) for 1-vs-3, Bowser,
  Fortune and Item, slot 1 (team 0's second player) for 2-vs-2. Every run must end the minigame with no halt or fault within
  `--e2-cap` frames (default 36,000) and run 300 frames more; a minigame that waits for input (its idle
  run never ends, as Reversal of Fortune's plunger) passes when an injected run ends it. Bowser's Bigger
  Blast also needs R=128 and R=255 to differ from each other. After the minigame, instDll's history goes
  to resultDll, which has no party to show here and returns inside the frame it starts in.
- `--e2-quick` stops each control run at its first divergence (no end check), for a fast look.
  `--e2-verbose` also prints the game's OSReport text and each overlay change.
- Output: one `e2 <id> m<NNN> port P focus F <control> diverge=t<N>|none vs idle|context; ...` line per
  run, one `e2 row ... PASS|FAIL` line per row and port, and `dump lane: e2: N row-port(s) pass, M fail`.
  Exit 0 only if every row-port passes. The lane writes nothing.

## Diagnosis: arena watches (`watch.h`, MP4)

```
mpt_dump_lane --boot mp4 --party ... [--watch-word OFF[,OFF...]] [--find-word VALUE@FRAME]
              [--watch-write OFF[,OFF...]@FRAME] [--dump-arena FRAME FILE]
```

The SDK arena sits at a fixed address, so an arena offset (hex) names the same bytes in every run of
one build. None writes game memory; `--watch-write` can perturb stale stack bytes (see below). None is in the shell or the module.
- `--find-word VALUE@FRAME` lists every aligned arena word equal to VALUE (hex) after FRAME steps.
- `--watch-word` prints a word whenever it differs after a step: the first frame a word changes.
- `--watch-write` (Windows x64) arms a hardware data breakpoint on up to four 8-byte-aligned words
  from FRAME on and prints, for every write, the new value and the unwound call stack as module
  offsets (`llvm-nm -n` on the installed `mp4_game.dll` names them). The trap's own handler runs on
  the game's stack, so a value read from uninitialised stack can differ under it; that is itself
  evidence of such a read.
- `--dump-arena FRAME FILE` writes the arena (main RAM, then ARAM) after FRAME steps, for diffing two builds' memory. The file is game memory: keep it outside the repository.

The watches act in `--party` mode only. They work under `--supervise`: the supervisor passes first-chance exceptions, including the data breakpoint's single-step trap, back to the lane. Combine them with `--party-save-at` and `--party-resume` to start near the frame. This is how the
layout gate's writer was found (`docs/titles/mp4.md` §20).

## Crash reports (M4-04)

```
mpt_dump_lane --supervise [--crash-report FILE] --boot mp4 ...
```

- Without `--supervise` the lane reports what it can in-process (`crash_report.h`): a fatal exception,
  `abort`, `std::terminate` and CRT invalid-parameter failures print the breadcrumbs below and a
  module-relative stack on stderr. A `/GS` stack-cookie failure (0xC0000409) skips all of that.
- `--supervise` (Windows) runs the lane as a child under a small debugger (`supervise.h`) that also
  sees fail-fasts. On any crash it prints a report on stdout, and with `--crash-report FILE` writes it
  to FILE (outside the repository; removed when the run starts, so it exists only after a crash). The
  exit code is the child's.
- A report has: the status (exception or fail-fast code), the run's mode, seed and board, the frame
  being stepped with the party phase, turn and running overlay, the last checkpoint's frame and state
  hash, the faulting address, the `culprit` (the first frame outside the CRT's failure machinery: for
  /GS, the function whose cookie broke) and the stack. Frames are `module+offset symbol+offset`;
  `mp4_game.dll` is named from its COFF symbol table. No game text and no paths, so a report can be
  quoted in an issue or `docs/titles/mp4.md`.
- On Linux the lane defines `__stack_chk_fail` itself (`stack_chk_fail.cpp`) and writes the same
  breadcrumbs and the failing function to stderr and `--crash-report FILE`; `--supervise` is accepted
  and runs in-process. This path is compile-only until the Linux runtime work resumes (M4-U2).
- `--crash-selftest MODULE gs|av|abort` needs no dump: it crashes the synthetic stub
  `tests/mp4_loader/overrun_stub.cpp` with made-up breadcrumbs. CTest runs it under `--supervise` on
  Windows (`dump_lane_crash_report_*`, `crash_report_check.cmake`).

## Paths outside the repository

The lane refuses to start if any of these lies inside the repository (`path_guard.h`, tested by
`tests/unit/test_dump_lane_paths.cpp`):

| Path | Holds | Default |
|---|---|---|
| `MPT_DUMP_DIR` | your dumps | required |
| `--capture DIR` | frame captures | off |
| `--replay FILE`, `--party-save-at N FILE` | a party's replay, a diagnostic state | off |
| `--crash-report FILE` | a crash report (holds no game material, but follows the same rule) | off |
| `MPT_BUILD_CACHE` | the extracted disc and the built game module | the shell's cache, `<user dir>/cache` (below) |
| `MPT_SOURCE_CACHE` | pinned source checkouts (`<name>/<commit>`), used instead of fetching | off |

The default build cache is the one the shell uses (`core/shell/user_dirs.h`), so the lane
and the shell share builds: `%APPDATA%\MarioPartyTogether\mpt\cache` on Windows and
`$XDG_DATA_HOME/mpt/cache` (usually `~/.local/share/mpt/cache`) on Linux. The lane never
puts it in `MPT_DUMP_DIR`, which holds your dumps only: if the default would land there, the
lane stops with an error. Setting `MPT_BUILD_CACHE` inside `MPT_DUMP_DIR` is your explicit
choice and is allowed, with a note.

`.gitignore` also ignores `captures/` and `mpt-build-cache/` folders, and the compliance
scanner rejects any committed binary, but neither is a reason to put these folders in the tree.

## Fixtures

- `tests/replays/<name>.mptr` plus `<name>.trail.txt`, recorded from a build of the pinned
  game source, never from an `unpinned` local override (ADR 0006).
- The compliance scanner validates every `.mptr` (P1-20e2) and rejects them anywhere else.
- They hold our inputs and 64-bit hashes only. Never commit save media.

## PR checklist line

Changes to `runtimes/gc_wii/`, `adapters/mp4/` or the replay format must record a local
run in the PR description: the command, OS, result and game-source pins. The PR template
has the line.
