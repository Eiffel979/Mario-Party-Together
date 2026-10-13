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

No title is registered yet, so every fixture is skipped. P1-16 registers `Mp4Runtime` in
`register_titles` (`main.cpp`) and fills in `resolve_dump`, which turns the dump in
`MPT_DUMP_DIR` into a `VerifiedDump` through asset-import's build (P1-10, P1-15).

## Fixtures

- `tests/replays/<name>.mptr` plus `<name>.trail.txt`, recorded from a build of the pinned
  game source, never from an `unpinned` local override (ADR 0006).
- The compliance scanner validates every `.mptr` (P1-20e2) and rejects them anywhere else.
- They hold our inputs and 64-bit hashes only. Never commit save media.

## PR checklist line

Changes to `runtimes/gc_wii/`, `adapters/mp4/` or the replay format must record a local
run in the PR description: the command, OS, result and game-source pins. The PR template
has the line.
