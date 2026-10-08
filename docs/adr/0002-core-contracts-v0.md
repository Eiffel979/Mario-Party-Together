# ADR 0002: Core contracts v0

**Status:** Proposed (needs `architect` approval)

## Context
Phase 0 needs real headers for the contracts sketched in `docs/PLAN.md` §5 so that the shell, asset-import, tests and later runtimes can compile against them. The sketch left the shared types undefined, and several of its methods could fail with no way to report it.

## Decision
The contracts live in `core/include/mpt/core/`, namespace `mpt`, and are versioned by `kCoreContractVersion` (now `0`). Any change bumps it and needs an ADR.

| Header | Contents |
|---|---|
| `title_id.h` | `TitleId` enum (`mp1`–`mp10`, `smp`, `mpss`). The values are stable: append only, never reorder. |
| `content_id.h` | `ContentId`, which parses and validates `<title>.<kind>.<snake_name>`. `MinigameId`, `BoardId` and `CharacterId` are aliases of it. |
| `input.h` | `InputFrame` = frame number + 4 × `PadState`. `PadState` is a packed 32-byte POD (canonical buttons, sticks, triggers, motion, pointer) so replays and netplay can serialize it byte for byte. |
| `media.h` | `FrameOutput` (CPU RGBA8 pixels **or** a GPU texture handle) and `AudioChunk` (interleaved s16). |
| `state.h` | `SaveState` (title, format version, frame, opaque bytes) and `StateHasher` (64-bit FNV-1a with little-endian integer feeding). |
| `rng.h` | `DeterministicRng` (SplitMix64) for non-title logic such as the Director, rules and cross-title picks. |
| `dump.h` | `VerifiedDump`: the only thing a runtime learns about the user's game. |
| `ruleset.h` | `ResolvedRuleset`: the adapter-facing result of rules resolution. Unset fields mean "title default". `rules/` owns the JSON format. |
| `party.h` | `PartySnapshot`, `PlayerState`, `MinigameSetup`, `MinigameResult`. |
| `title_runtime.h` | `ITitleRuntime`, `RuntimeConfig`. |
| `title_adapter.h` | `ITitleAdapter`, `Catalog`. |
| `title_registry.h` | `TitleRegistry`: TitleId → runtime/adapter factories. Core never names a concrete title. |

Differences from the §5 sketch:
- `loadState`, `applyRuleset` and `launchMinigame` return `Result` instead of `void`. A state from another build, a rule the title cannot honor, or (before Phase 5) an unsupported minigame launch is a normal failure, not a crash.
- `stepFrame` contract: `inputs.frame` equals the number of frames already stepped, and `stateHash()` covers all state that affects future frames but not the A/V buffers.
- `video()`/`audio()` return views that stay valid only until the next `stepFrame()`, so runtimes don't copy frames.
- `Result` is a small in-house type because C++20 has no `std::expected`.

`core` (the `mpt_core` library) has no SDL dependency. Only the shell executable links SDL3.

## Alternatives
- **Separate ID types per kind** (`MinigameId` distinct from `BoardId`). Rejected for v0 because the kind is already in the string and validated. We can revisit if mix-ups show up.
- **Variable player count in `InputFrame`.** Rejected: every mainline title is 4-player, and a fixed size keeps the POD trivially serializable.
- **xxHash for state hashes.** Deferred. FNV-1a needs no dependency and is fast enough for Phase 0/1. Swapping it is an ADR'd contract-version bump.

## Consequences
- Runtimes, adapters, `rules/`, `net/` and `input/` can start coding against stable headers.
- The determinism harness (`tests/support/replay_harness.h`, `FakeRuntime`) already exercises the runtime contract, so Phase 1's MP4 runtime plugs into existing tests.
- `PadState` is 32 bytes, which caps per-frame input at 4 × 32 B + 8 B per peer packet before compression.
