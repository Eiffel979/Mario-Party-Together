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
2. **Layout independence (the layout probe).** Run `mpt_dump_lane --boot mp4 --frames 3000 --every 100
   --hash-items <file>` on a normal host build. Reconfigure the host build with
   `-DMPT_MP4_RECIPE_LAYOUT_PROBE=ON`, rebuild, and run the same command again. That build's module
   moves every function and every game global. The two `frame N hash` trails must be identical, and the
   `frame N deep` trails must differ. Reconfigure with `=OFF` afterwards.

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
mpt_dump_lane --boot mp4 --verify-replay FILE
```

- `--party` plays a whole party through a `Session` on an autopilot (`party_autopilot` in
  `dump_lane.h`): player 1's pad is a pure function of the frame, the Director's phase, the running
  overlay and the frames since the last party change (phase, overlay or turn). It pulses START on
  the title, on the instruction screen and on the final ranking, pulls the stick and mashes A in minigames, and pulses A
  everywhere else. The default menu choices give Toad's Midway Madness, 20 turns, player 1 human
  and three COM players (mentDll asks for at least one human player).
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
  (frames since mentDll started). Only the default board has been validated.
- Diagnosis: `--party-save-at N FILE` saves the state before frame N, and `--party-resume FILE`
  goes on from it (no replay, not evidence). State files hold game memory: keep them outside the
  repository.

## Paths outside the repository

The lane refuses to start if any of these lies inside the repository (`path_guard.h`, tested by
`tests/unit/test_dump_lane_paths.cpp`):

| Path | Holds | Default |
|---|---|---|
| `MPT_DUMP_DIR` | your dumps | required |
| `--capture DIR` | frame captures | off |
| `--replay FILE`, `--party-save-at N FILE` | a party's replay, a diagnostic state | off |
| `MPT_BUILD_CACHE` | the extracted disc and the built game module | `<MPT_DUMP_DIR>/mpt-build-cache` |
| `MPT_SOURCE_CACHE` | pinned source checkouts (`<name>/<commit>`), used instead of fetching | off |

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
