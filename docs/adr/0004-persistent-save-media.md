# ADR 0004: Persistent save media through the host

**Status:** Accepted
Author: architect, 2026-10-08. Approved by: architect, 2026-10-08. Part of core contract v1 (with ADRs 0003 and 0005). Amends ADR 0002 rules 2 and 11.

## Context
ADR 0002 rule 11 forbids runtimes from reading or writing memory cards, EEPROM or other persistent saves until an ADR defines a channel, and asks for one before Phase 1 exit. MP4 reads its memory card at boot and writes to it during and after parties. Unlocked content (boards, minigames) lives on the card. If each runtime read local files on its own:
- the save contents would be a hidden input, and replays and peers would diverge;
- runtimes would need filesystem paths and policy, which belong to the host.

## Decision
The host owns save media. Runtimes see it only as bytes.

```cpp
// mpt/core/save_media.h
struct SaveMedia {
  std::uint32_t format_version = 0;  // runtime-defined layout version
  std::vector<std::byte> data;       // opaque, runtime-defined (e.g. a memory card image)
};

struct RuntimeConfig {
  ...
  // Initial persistent save image. nullopt = blank media (fresh, formatted card).
  std::optional<SaveMedia> save_media;
};

class ITitleRuntime {
  ...
  // Increments each time the game commits a write to its save media. Starts at 0 after boot().
  virtual std::uint64_t saveMediaGeneration() const = 0;
  // Current image. Only meaningful to persist when the generation has changed.
  virtual SaveMedia saveMedia() const = 0;
};
```

Rules:
1. **Runtimes never touch the filesystem for persistence.** `CARD`, EEPROM, flash and similar APIs are served from the in-memory image.
2. **The image is game state.** It is part of `SaveState` and covered by `stateHash()` wherever it can affect future frames.
3. **Amended determinism inputs (ADR 0002 rule 2).** A runtime's `stateHash()` sequence is a function of dump identity, `RuntimeConfig::seed`, **the initial `save_media` bytes**, and the `InputFrame` sequence.
4. **Writes complete deterministically.** Asynchronous save APIs finish after a fixed, documented number of frames (zero for the GameCube layer, which completes CARD operations synchronously, ADR 0007 §4), never "when the disk is done". The generation counter changes on the same frame on every peer.
5. **The shell persists.** When the generation changes, the shell writes the image to `<user data>/saves/<title>/<region>/` atomically. Which slot to load at boot is a shell/UI choice. Replays and tests use blank media unless the replay says otherwise (ADR 0006).
6. **Ephemeral sessions.** When the shell runs a replay, a test or (Phase 2) a netplay guest, it never writes the image back.
7. **Stable media identity.** If the console API exposes an identity for the medium (GameCube `CARDGetSerialNo`; MP4 ties save files to it), the runtime stores it **inside the image**, so it persists with the saves. When a blank card is formatted, every runtime of a title uses the **same fixed constant** serial, never a value derived from the seed, the host or the time. A card therefore keeps its saves across sessions, replays and machines, and the host's image works unchanged for netplay guests.
8. **Save timestamps are synthetic in Phase 1.** Games that stamp saves with the console clock (MP4 writes `OSGetTime()` into save files and shows it on the file-select screen) get **virtual time** (ADR 0008 §2). Its epoch is derived from the session seed, so saves show **synthetic dates** that do not match the real date and are not ordered across sessions. This is accepted for Phase 1.
   - **Real dates later.** Using the real date would make it a **determinism input**: a `RuntimeConfig` field such as a host-chosen RTC epoch, recorded in the replay header and set by the host in netplay. That is a core contract change and needs its own ADR. It is not planned for Phase 1 or 2.
   - **Check for P1-16.** Confirm that no MP4 logic compares save timestamps against the clock, for example to detect "newest save". If any does, the epoch must also be kept at or after the newest timestamp in the initial image. That is still deterministic, because the initial image is a determinism input (rule 3).
9. **`loadState` restores the image and the generation** (added in P1-03). The generation is game state like the image, and both are part of `SaveState`. After `loadState(s)` succeeds, `saveMedia()` and `saveMediaGeneration()` return what the producing runtime returned at `s.frame`. A rewind can therefore lower the generation.
10. **The shell detects writes only across `stepFrame()`** (added in P1-03). It compares the generation just before and just after each `stepFrame()`, and persists only when they differ. `loadState` is a **resync point, not a write**: the shell takes the restored generation as its new baseline and persists nothing. So:
    - a rewind or a rollback never persists anything by itself, even when the generation goes down or comes back to an earlier value;
    - a write made while re-simulating after a `loadState` is still persisted, because it is detected against the restored baseline;
    - a write that was already persisted from frames that a rewind later undoes stays persisted, as it would on a real card. Rule 6 still applies: replays, tests and netplay guests never persist.
    Shell work goes in P1-06(g). The P1-20c conformance suite checks rule 9 (`saveMedia()` and `saveMediaGeneration()` match the producer after `loadState`).

## Alternatives
- **A save directory in `RuntimeConfig` that the runtime reads itself** (the shape suggested in the ADR 0002 review). Rejected: it makes the file contents an unrecorded input and spreads file I/O into every runtime.
- **No persistence in Phase 1** (always a blank card). Rejected: players lose unlocks every session, and the problem only moves to Phase 2.

## Consequences
- `kCoreContractVersion` goes to 1. ADR 0002 rule 11 is satisfied.
- Save images are **user data that contain game-made content** (GameCube saves embed the game's icon and banner graphics). They are never committed (the scanner rejects card-image extensions, P1-20e1), never embedded in committed replays, and never uploaded to `server/`.
- **Phase 2:** in netplay the host's image is the truth. It is sent peer to peer to guests (who already own the game), used ephemerally, and never written to guests' saves. The netplay protocol ADR must confirm this and handle "unlock everything" as a ruleset option instead.
- `FakeRuntime` gets a trivial image (a counter) so the conformance suite can test generation and round trips.

## Review (architect, 2026-10-08)
**Verdict: Accepted.** No user decision was needed. This satisfies the ADR 0002 rule 11 follow-up ("persistent save media before Phase 1 exit") with a different shape from the one the review suggested, for the reason under Alternatives: the save bytes become a recorded input instead of a hidden file read.
- Rule 5's save directory is under the shell's user data directory (`SDL_GetPrefPath`), never inside the repo or the build cache. Save images are user data that carry game-made icons and banners, so the scanner rejects card-image extensions (P1-20e1).
- Copying the image in `saveMedia()` is acceptable because the shell calls it only when the generation changes. If a title's image is large, a span-returning accessor can be added later as an additive change.
- Committed replay fixtures always use blank media (ADR 0006), so dump-lane tests never depend on a developer's own save.
- The Phase 2 consequence (the host's image is authoritative, guests never write back) is carried into the Phase 1 plan §11.
- **Amended after P1-01** (`docs/titles/mp4.md` §7, §10): rules 7 (stable card serial) and 8 (synthetic save dates in Phase 1, with real dates as a future determinism input), and zero-frame CARD completion in rule 4. These are runtime rules and add nothing to the header, so the ADR stays Accepted. The visible effect for players is that MP4's file-select screen shows synthetic dates.
- **Amended in P1-03** (qa-reviewer review of the v1 headers): rules 9 (`loadState` restores image and generation) and 10 (the shell detects writes only across `stepFrame()`, and `loadState` is a resync point). Both were unspecified, and they are now stated in `title_runtime.h` as well. `SaveMedia` also gained a defaulted `operator==` (ADR 0002, amendment 13). No layout change, so the ADR stays Accepted.
