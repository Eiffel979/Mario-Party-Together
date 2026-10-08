# ADR 0006: Replay format v0 and the dump-gated test lane

**Status:** Accepted
Author: architect, 2026-10-08. Approved by: architect, 2026-10-08.

## Context
The Phase 1 exit criterion is a deterministic replay test. That needs a replay file that the shell can record and play back, that tests can verify headless, and that netplay can later reuse for desync reports. The `InputSource` in `tests/support/replay_harness.h` is in-memory only.

Real-game replays can only be verified with the user's own dump, which public CI never has (ADR 0001). The compliance scanner rejects every binary file unless it is allowlisted.

## Decision

### File format `.mptr` (little-endian, per ADR 0002 rule 1)
| Field | Type | Notes |
|---|---|---|
| magic | `"MPTR"` | |
| format_version | u32 | 0 |
| core_contract_version | u32 | `kCoreContractVersion` at record time |
| title | u8 | `TitleId` |
| dump_sha256 | 32 bytes | raw digest. Dump identity, never a path. |
| seed | u64 | `RuntimeConfig::seed` |
| save_media_sha256 | 32 bytes | of the initial image. All-zero means blank media. |
| save_media_embedded | u32 length + bytes | optional copy of the initial image, so a user can share a self-contained replay. Length 0 = not embedded. |
| ruleset | u32 length + bytes | reserved for Phase 2 (serialized resolved ruleset). Length 0 in Phase 1. |
| frame_count | u64 | |
| checkpoint_interval | u32 | frames between hash checkpoints (60 by default) |
| input runs | repeated | `{u32 run_length, 4 × PadState (32 B each)}`: the pads repeat for `run_length` frames. `InputFrame::frame` is implicit. |
| checkpoints | u32 count + `{u64 frame, u64 stateHash}`… | the hash trail recorded live |

`internal_scale`, `widescreen` and other presentation options are deliberately left out (ADR 0002 rule 2).

### Library
`core/include/mpt/core/replay.h`: `ReplayWriter` (append `InputFrame`, add checkpoint, finish) and `ReplayReader` (validate header, iterate frames, read the trail). No SDL dependency. The shell gets `--record`, `--replay` and `--verify-replay`. The last one runs headless with the null renderer and exits non-zero on the first mismatching checkpoint.

### Committed fixtures
- Allowed only under `tests/replays/`, each with a text sidecar `<name>.trail.txt` (the golden trail, one `frame hash` per line, for readable diffs).
- The scanner (P1-20e2) treats `*.mptr` there as an allowed binary **only if** it fully parses, has no trailing bytes and has **no embedded save media**. Any other `.mptr` fails.
- Fixtures contain our inputs and 64-bit digests only. That is no Nintendo material.

### Dump-gated lane
- Tests that need a real dump carry the CTest label `dump`. They read `MPT_DUMP_DIR`, run asset-import against it, and **skip** (CTest `SKIP_RETURN_CODE`) when it is unset or the dump is missing.
- Each replay is verified in its own process (ADR 0002 rule 10). Cross-platform equality means comparing against the committed `.trail.txt`, not against another live run.
- **The lane is manual and local only** (Phase 1 plan decision D4). It runs on a developer's own machine with `MPT_DUMP_DIR` pointing at a directory outside the repo (generic example: `MPT_DUMP_DIR=D:\path\to\dumps`). There is no self-hosted runner, and **no dump is ever placed on CI infrastructure**, hosted or self-hosted. Public CI only ever sees these tests as skipped.
- **Cadence:** the PR author runs the lane before merging any change to `runtimes/gc_wii/`, `adapters/mp4/` or this format, and records the command, OS, result and game-source pin in the PR description. qa-reviewer checks that the line is present. Golden trails are recorded only from builds made from the pinned game source (ADR 0009), never from an `unpinned` local override.
- Changing this policy (for example adding a self-hosted runner) needs an amendment to this ADR.

## Alternatives
- **A text replay format.** Rejected: a 30-minute party is about 108k frames, so even a run-length text encoding is bulky and slow to parse. The sidecar trail gives readable diffs where they matter.
- **Record every `InputFrame` uncompressed.** Rejected: 136 B × 108k frames ≈ 14 MB. Runs compress idle input by orders of magnitude.
- **Use netplay's wire format for replays.** Deferred: that format does not exist yet. Phase 2 may embed `.mptr` runs in desync reports.

## Consequences
- Replays are tied to one dump hash and one contract version. A contract bump invalidates fixtures, which are re-recorded deliberately. That is acceptable at this stage.
- Users can share replays. A shared replay with embedded save media carries their save, so the UI must say so before export.
- qa-reviewer owns the fixtures, the lane and the scanner rule. architect owns the format.

## Review (architect, 2026-10-08)
**Verdict: Accepted.** The open question (Phase 1 plan Q4) is resolved by decision D4: a manual, local dump lane, with no dump on any CI infrastructure. The lane rules above were amended to say so. The user can revisit a self-hosted runner later through an amendment.
- The format holds only our inputs, our 64-bit digests and a dump identity hash. Committed fixtures are therefore not Nintendo material, provided the scanner rule (P1-20e2) enforces "no embedded save media".
- The cross-platform check (exit criterion E3c, required by D5) compares each OS against the committed `.trail.txt`, so the two OSes never need to run together.
- Accepted risk: with no automated dump lane, a regression can land if a PR skips the manual run. Mitigated by the PR checklist line (Phase 1 plan R8).
