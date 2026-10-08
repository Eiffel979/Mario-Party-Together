# ADR 0003: Runtime-reported native frame rate

**Status:** Accepted
Author: architect, 2026-10-08. Approved by: architect, 2026-10-08. Part of core contract v1 (with ADRs 0004 and 0005).

## Context
The shell paces frames at a hard-coded 60 Hz (`core/shell/app.cpp:25`). Real titles run at their video mode's rate: NTSC GameCube/Wii at about 59.94 Hz, PAL at 50 Hz, and PAL games that offer a 60 Hz mode switch at runtime. A wrong pacing rate makes audio drift and underrun, and makes games run at the wrong speed. The ADR 0002 review listed this as a follow-up due before Phase 1 exit.

## Decision
Add to `title_runtime.h`:

```cpp
// Frames per second as an exact ratio, e.g. {60000, 1001} for NTSC, {50, 1} for PAL.
struct FrameRate {
  std::uint32_t numerator = 60;
  std::uint32_t denominator = 1;
};

class ITitleRuntime {
  ...
  // Valid after a successful boot(). May change only as a result of stepFrame() or
  // loadState() (e.g. a game switching PAL 50/60), so it is itself deterministic.
  virtual FrameRate frameRate() const = 0;
};
```

Rules:
1. `numerator > 0` and `denominator > 0`. The rate is a pure function of game state, so all peers agree.
2. The shell reads `frameRate()` after every `stepFrame()`. It paces with integer nanoseconds: frame deadline `k` since the last rate change = `t0 + k * 1e9 * denominator / numerator`, computed in 128-bit or with a remainder accumulator so there is no drift.
3. Pacing is host-side only. Lockstep and replays are frame-indexed, so the rate never enters game logic or `stateHash()` as wall-clock time.
4. Audio is not paced from the frame rate. `AudioChunk::sample_rate` stays authoritative, and the shell resamples to the device.

## Alternatives
- **Put the rate in `RuntimeConfig`** (shell decides). Rejected: the game decides its video mode, and region-specific dumps would need out-of-band knowledge.
- **A floating-point Hz value.** Rejected: contracts have no floats (ADR 0002), and 59.94 is not exact in binary.
- **Let the runtime pace itself (block in `stepFrame`).** Rejected: it breaks headless replay and fast-forward, and puts wall-clock time into the runtime.

## Consequences
- `kCoreContractVersion` goes to 1, together with ADRs 0004 and 0005.
- `FakeRuntime` returns `{60, 1}`. The conformance suite checks rule 1 and that the rate is constant across `saveState`/`loadState` round trips.
- Netplay (Phase 2) needs nothing new: a mid-session rate change happens on the same frame on every peer.

## Review (architect, 2026-10-08)
**Verdict: Accepted.** No user decision was needed. Checked against the Phase 1 plan's exit criteria E3, E6 and E7 and against ADR 0002's rules on contract types (no floats, fixed-width fields).
- The ratio form is exact for every rate in scope (NTSC 60000/1001, PAL 50/1, 60/1) and needs no floating point.
- The rate is state-derived, so a mid-session PAL 50/60 switch happens on the same frame for every peer and in every replay. The conformance suite (P1-20c) checks rule 1 and that the rate survives a `saveState`/`loadState` round trip.
- MP4 (USA, NTSC) runs one game-loop iteration per VI field, so `Mp4Runtime` reports {60000, 1001} (`docs/titles/mp4.md` §3).
- E7 pacing is measured on the reference machine recorded in the Phase 1 plan, decision D7.
- Lands in P1-03, together with ADRs 0004 and 0005, as one `kCoreContractVersion` bump to 1.
