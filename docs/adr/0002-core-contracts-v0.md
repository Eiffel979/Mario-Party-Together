# ADR 0002: Core contracts v0

**Status:** Accepted
Approved by: architect, 2026-10-08

## Context
Phase 0 needs real headers for the contracts sketched in `docs/PLAN.md` §5 so that the shell, asset-import, tests and later runtimes can compile against them. The sketch left the shared types undefined, and several of its methods could fail with no way to report it.

## Decision
The contracts live in `core/include/mpt/core/`, namespace `mpt`, and are versioned by `kCoreContractVersion` (now `0`, declared in `title_runtime.h`). The version covers every header in the table below, including serialized layouts (`PadState`, `InputFrame`), `TitleId` values and the `StateHasher` algorithm, not only the two interfaces. Any change bumps it and needs an ADR.

| Header | Contents |
|---|---|
| `result.h` | `Result`: success, or failure with a message. `[[nodiscard]]`. |
| `title_id.h` | `TitleId` enum (`mp1`–`mp10`, `smp`, `mpss`), `kAllTitles`, `to_string` / `parse_title_id`. The values are stable: append only, never reorder. |
| `content_id.h` | `ContentId`, which parses `<title>.<kind>.<snake_name>`. The title must be a known `TitleId`. Kind and name must each be lower snake_case: `[a-z0-9_]`, starting with a letter, with no trailing `_` and no `__`. The kind is **not** checked against a list, and nothing checks that a `MinigameId` has kind `minigame`. `MinigameId`, `BoardId` and `CharacterId` are aliases of it. |
| `input.h` | `kMaxPlayers` (4), the canonical `Button` bitmask, and `InputFrame` = frame number + 4 × `PadState`. `PadState` is a 32-byte POD with no padding (canonical buttons, sticks, triggers, connected flag, motion, pointer). Only fixed-width integers, no floats. A `static_assert` enforces the size. `InputFrame` is 136 bytes with no padding. |
| `media.h` | `FrameOutput` (CPU RGBA8 pixels **or** a GPU texture handle) and `AudioChunk` (interleaved s16, with sample rate and channel count). |
| `state.h` | `SaveState` (title, format version, frame, opaque bytes) and `StateHasher` (64-bit FNV-1a, with `update_u64` feeding integers little-endian). |
| `rng.h` | `DeterministicRng` (SplitMix64, with `below()` free of modulo bias) for non-title logic such as the Director, rules and cross-title picks. `DeterministicRng(rng.state())` resumes a stream exactly. |
| `dump.h` | `VerifiedDump` (title, region, revision, sha256, local `build_dir`): the only thing a runtime learns about the user's game. |
| `ruleset.h` | `ResolvedRuleset`: the adapter-facing result of rules resolution. Unset fields mean "title default". `rules/` owns the JSON format. |
| `party.h` | `PartySnapshot`, `PlayerState`, `MinigameSetup`, `MinigameResult`. |
| `title_runtime.h` | `kCoreContractVersion`, `ITitleRuntime`, `RuntimeConfig`. |
| `title_adapter.h` | `ITitleAdapter`, `Catalog`, `CatalogEntry`. |
| `title_registry.h` | `TitleRegistry`: TitleId → runtime/adapter factories. Core never names a concrete title. |

Differences from the §5 sketch:
- `loadState`, `applyRuleset` and `launchMinigame` return `Result` instead of `void`. A state from another build, a rule the title cannot honor, or (before Phase 5) an unsupported minigame launch is a normal failure, not a crash.
- `stepFrame` contract: `inputs.frame` equals the number of frames already stepped, and `stateHash()` covers all state that affects future frames but not the A/V buffers.
- `video()`/`audio()` return views that stay valid only until the next `stepFrame()`, so runtimes don't copy frames.
- `Result` is a small in-house type because C++20 has no `std::expected`.

`core` (the `mpt_core` library) has no SDL dependency. Only the shell executable links SDL3.

### Normative semantics (v0)
These rules belong to the contract even where the header comments are shorter. When the two disagree, this ADR wins until the comments are brought in line.

1. **Wire and replay encoding.** `PadState` and `InputFrame` are encoded field by field in declaration order, little-endian. On the supported hosts (x86-64 and ARM64, all little-endian) that is the same as their in-memory bytes, which is the only reason byte-for-byte copies are allowed.
2. **Determinism inputs.** A runtime's `stateHash()` sequence is a function of exactly these: the dump identity (`title` + `sha256`, never `build_dir`), `RuntimeConfig::seed`, and the `InputFrame` sequence. `internal_scale` and `widescreen` are local presentation options. They must not affect `stateHash()`, so peers can choose them freely. A runtime that cannot keep a presentation option out of game state must ignore that option during netplay.
3. **Frame numbering across states.** After `loadState(s)` succeeds, the next `stepFrame` takes `inputs.frame == s.frame`.
4. **Hash rules.** `stateHash()` must be identical across supported platforms and compilers, and across processes. It must not cover host addresses (pointers), padding bytes or uninitialized memory.
5. **SaveState portability.** `SaveState::data` must be loadable by the same runtime build on any supported platform, because desync recovery sends the host's state to peers. `loadState(s)` followed by `stateHash()` must return the hash that the runtime which produced `s` had at that frame.
6. **View lifetime.** `video()` / `audio()` views are also invalidated by `loadState()` and by destroying the runtime.
7. **Pixel format.** `cpu_rgba8` pixels are tightly packed (stride = `width`), top row first, and each pixel's bytes in memory are R, G, B, A.
8. **GPU path.** v0 gives the runtime no presenter device, so a `gpu_texture` handle cannot be shared yet. Runtimes use `cpu_rgba8` until an ADR defines the device handshake.
9. **Adapter mutations.** `applyRuleset`, `writeParty` and `launchMinigame` may only be called between `stepFrame()` calls, at a frame that the Director picks deterministically (the same frame on every peer). They must be deterministic functions of their arguments and the runtime state. `readParty`, `catalog` and `pollMinigameResult` must not change game state.
10. **Instances.** A runtime may support only one live instance per process (decomp ports and N64Recomp output keep global state). Callers must not create a second instance of a title while one is alive unless that runtime's `docs/titles/<id>.md` says it is safe. An adapter must not outlive the runtime it was created with.
11. **Persistent save media.** v0 has no channel for memory cards, EEPROM or other persistent saves. Runtimes must not read or write such files outside `SaveState`, because local save files would differ between peers. Persistence needs its own ADR before Phase 1 exit (see Review).

## Alternatives
- **Separate ID types per kind** (`MinigameId` distinct from `BoardId`). Rejected for v0 because the kind is part of the string. Note that `ContentId::parse` only checks that the kind is well formed, not what it is, so consumers (the `rules/` validator, adapters) must check the kind they expect. We can revisit if mix-ups show up.
- **Variable player count in `InputFrame`.** Rejected for v0: every title up to Phase 5 is 4-player, and a fixed size keeps the POD trivially serializable. MP10's Bowser Party has 5 players (4 + the GamePad player), so Phase 6 may need an ADR'd change to `kMaxPlayers`.
- **xxHash for state hashes.** Deferred. FNV-1a needs no dependency and is fast enough for Phase 0/1. Swapping it is an ADR'd contract-version bump.

## Consequences
- Runtimes, adapters, `rules/`, `net/` and `input/` can start coding against stable headers.
- The determinism harness (`tests/support/replay_harness.h`, `FakeRuntime`) already exercises the runtime contract. Phase 1's MP4 runtime plugs into the same harness, but its determinism tests must run instances one after another or in separate processes (rule 10), not side by side as the `FakeRuntime` tests do.
- A full `InputFrame` is 136 B (8 B frame + 4 × 32 B). In lockstep each peer sends only its local pads, so one peer's per-frame payload is 8 B + 32 B per local player before compression.

## Review (architect, 2026-10-08)
Reviewed against `core/include/mpt/core/*.h`, `core/src/`, `core/shell/`, `asset-import/` and `tests/` (`fake_runtime.h`, `replay_harness.h`, `test_core.cpp`, `test_determinism.cpp`).

**Verdict: Accepted.** No blocking issues. No header contains a contract bug that forces an ADR'd break in Phase 1–2, the contracts have no floats or platform-dependent sizes, and nothing title-specific leaks into `core/`.

**Fixes made to this ADR (documentation only):**
- Added `result.h`, `kMaxPlayers`, `Button`, `CatalogEntry`, `kAllTitles` and the `VerifiedDump` fields to the table.
- The kind was described as "validated". In fact only its form is checked, not its value. Corrected under Alternatives and in the table.
- "Every mainline title is 4-player" was wrong (MP10 Bowser Party has 5 players). Narrowed it.
- The packet-size claim mixed up the full `InputFrame` with one peer's payload. Corrected.
- "Packed" now reads "no padding": the header uses no `#pragma pack`; the layout just has no padding.
- Said that `kCoreContractVersion` covers every contract header. The header comment names only the two interfaces.
- Added "Normative semantics (v0)", which pins down endianness, determinism inputs, hash and SaveState portability, pixel format, adapter call timing, instance cardinality and save media. None of these needs a header change.

**Follow-ups that need an ADR before the named gate (additive, non-blocking now):**
- *Before Phase 1 exit:* persistent save media (memory card for MP4). Suggested shape: a host-supplied save directory in `RuntimeConfig` plus an ephemeral/netplay flag, with the save image carried in `SaveState`. `title_runtime.h:17-21`.
- *Before the GX layer renders on the GPU:* a presenter device handshake for `FrameOutput::gpu_texture`. `media.h:17`, `title_runtime.h:17-21`.
- *Before Phase 1 exit:* expose the runtime's native frame rate (PAL dumps run at 50 Hz, and the shell hard-codes 60 Hz at `core/shell/app.cpp:25`). `title_runtime.h:29-48`.
- *Before Phase 2 netplay ships:* settle the state-hash algorithm. Byte-wise FNV-1a over a 24 MB GameCube RAM image takes tens of ms. Either hash a runtime-chosen subset or switch to xxh3. `state.h:20-45`.
- *Before Phase 2 rules work:* a way to carry title-specific rule options (for example `std::vector<std::pair<ContentId, std::int64_t>>` with kind `rule`) so that each MP4 option doesn't need a struct change, plus an adapter capability query so the ruleset editor can grey out unsupported rules. `ruleset.h:14-24`, `title_adapter.h:35`.
- *Before Phase 4 (MP8):* define the units for `accel`/`gyro` in the contract rather than in `input/`, and add a pointer-valid / off-screen flag (the `reserved` byte can hold it). Add a host-feedback output (rumble, Wii Remote speaker). `input.h:46-50`.
- *Phase 5:* a Director-facing event when a title reaches a minigame-selection point, `is_cpu`/difficulty in `MinigameSetup`, and canonical cross-title character IDs. `title_adapter.h:41-42`, `party.h:30-35`.

**Non-blocking header and test suggestions (next contract touch, no version bump for comments):**
- `input.h:66`: add `static_assert(sizeof(InputFrame) == 136)`, `std::is_trivially_copyable_v` / `std::is_standard_layout_v` checks, and `std::endian::native == std::endian::little`.
- `content_id.h:17-18`: the comment omits the no-trailing-`_` / no-`__` rules. Consider a `parse(text, expected_kind)` helper.
- `title_runtime.h:14`, `26-28`, `36`, `39`: sync the comments with the normative rules 2, 3, 6 and the version scope above.
- `media.h:16`: document the byte order and stride (rule 7).
- `title_adapter.h:37-41`, `title_registry.h:18`: document rules 9 and 10 (call timing, adapter lifetime).
- `state.h:13-18`: consider carrying the dump `sha256` (or a build ID) in `SaveState` so that `loadState` can reject a state from another revision without depending on runtime-internal data.
- `ruleset.h:15` and `title_runtime.h:18`: two seeds with no stated relation. Document that both derive from the host session seed (through `DeterministicRng`).
- `tests/unit/test_determinism.cpp:27-34,45-62` (owner `qa-reviewer`): add a variant that runs instances one after another and one that compares hashes across two processes, so the harness matches rule 10 and catches address-dependent hashes before MP4 lands. `FakeRuntime::saveState`'s `memcpy` of a struct (`tests/support/fake_runtime.h:42-43`) is fine for the fake but is not a pattern for real runtimes (rule 5).
