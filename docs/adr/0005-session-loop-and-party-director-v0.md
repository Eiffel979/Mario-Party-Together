# ADR 0005: Session loop and Party Director v0

**Status:** Accepted
Author: architect, 2026-10-08. Approved by: architect, 2026-10-08. Part of core contract v1 (with ADRs 0003 and 0004).

## Context
Phase 0 has contracts but no code that drives them: the shell's loop steps nothing. Phase 1 needs one place that owns a runtime and adapter, steps them frame by frame and mirrors party state, and that works the same live, under replay, in tests and (Phase 2) under netplay. The Party Director is architect-owned and its types need an ADR (CLAUDE.md rule 3).

The Director also needs to know where a party is ("in a minigame", "finished"), and `PartySnapshot` cannot say that today. The exit criteria depend on detecting the end of a party.

## Decision

### `PartySnapshot` additions (`party.h`)
```cpp
enum class PartyPhase : std::uint8_t {
  none,       // not in a party (menus, title screen)
  setup,      // party settings, character select
  board,      // board turns
  minigame,   // a minigame is running
  results,    // end-of-party bonus stars and results
  finished,   // the party is over and its results are final
};

struct PartySnapshot {
  ...existing fields...
  PartyPhase phase = PartyPhase::none;
  std::optional<MinigameId> current_minigame;  // set while phase == minigame, else nullopt
};
```
Adapters fill `phase` as well as the title allows. `current_minigame` is set exactly when `phase == minigame`, and is `nullopt` in every other phase. A title that cannot tell `results` from `finished` must report `finished` only once the results are final.

### `Session` (`session.h`)
```cpp
struct SessionConfig {
  VerifiedDump dump;
  RuntimeConfig runtime;
  std::optional<ResolvedRuleset> rules;  // Phase 1: nullopt or empty
};

class Session {
 public:
  // Creates runtime and adapter through the registry, boots, applies rules.
  static std::variant<std::unique_ptr<Session>, Result> start(const TitleRegistry&, const SessionConfig&);

  // inputs.frame must equal frame(). Steps the runtime once, then lets the Director observe.
  void step(const InputFrame& inputs);

  std::uint64_t frame() const;
  ITitleRuntime& runtime();
  const PartyDirector& director() const;
};
```
- `Session` has **no clock, no SDL, no filesystem and no threads**. The shell owns pacing, input sampling, presentation, audio and save persistence.
- It enforces ADR 0002 rule 10: it destroys its runtime before it can be replaced, and `start` fails if a single-instance runtime of the same title is still alive in the process.
- Replay recording is outside `Session` (ADR 0006). The shell tees the same `InputFrame`s to a `ReplayWriter`.

### `PartyDirector` v0 (`party_director.h`)
```cpp
struct PartyEvent {
  enum class Kind : std::uint8_t { phase_changed, turn_started, minigame_started, minigame_ended };
  Kind kind;
  std::uint64_t frame;
  PartySnapshot snapshot;  // state at the event
};

class PartyDirector {
 public:
  void observe(std::uint64_t frame, const PartySnapshot& snapshot);
  const PartySnapshot& party() const;
  std::span<const PartyEvent> events() const;  // all events so far, in frame order
};
```
- v0 is a **read-only mirror**. It calls `readParty()` after every `stepFrame()`, and adapters must keep that cheap. Events come from diffing consecutive snapshots, so they are deterministic.
- v0 never calls `writeParty`, `applyRuleset` (beyond start) or `launchMinigame`. Phase 2 adds rule enforcement and Phase 5 adds suspend/launch/resume, each through its own ADR.

## Alternatives
- **Put the session loop in the shell.** Rejected: tests, replays and netplay would each copy it, and SDL would leak into the code that must stay deterministic.
- **Sample `readParty()` every N frames.** Rejected for v0: short phases (a 1-frame transition) could be missed, and it would make event frames depend on N. We can revisit if a title's `readParty` is expensive.
- **A callback/observer API for events.** Deferred: a vector is simpler to test and replay. The UI polls it.

## Consequences
- `kCoreContractVersion` goes to 1. Adapters must fill `phase`. `FakeAdapter` (P1-20b2) and `Mp4Adapter` (P1-17) are the first.
- `ui/` reads standings and phase only through `Session::director()`, never from a runtime (frontend-ui principle).
- This is the seam where Phase 2 lockstep plugs in: netplay produces the agreed `InputFrame` and calls `Session::step`.

## Review (architect, 2026-10-08)
**Verdict: Accepted.** No user decision was needed.
- Keeps title-specific logic out of `core/`: `Session` and `PartyDirector` see titles only through `ITitleRuntime`, `ITitleAdapter` and `PartySnapshot`, and titles are registered through the generated `register_titles()` (Phase 1 plan P1-06).
- `Session` has no clock, SDL, filesystem or threads, so the same object runs live, in replays, in tests and under Phase 2 lockstep. This is the seam netplay plugs into.
- The minimal launcher UI chosen for Phase 1 (plan decision D6) reads phase and standings only through `Session::director()`.
- `PartyPhase::finished` is what exit criterion E1 checks, so `Mp4Adapter` (P1-17) must report it only once results are final.
- v0 stays read-only. Rule enforcement (Phase 2) and suspend/launch/resume (Phase 5) each need their own ADR.
- **Amended in P1-03** (qa-reviewer review of the v1 headers):
  - The `current_minigame` rule is tightened to "set exactly when `phase == minigame`, `nullopt` otherwise", which matches `party.h`. The first draft only said when it is set, so the Director could not rely on it being cleared, and a stale ID would make `minigame_ended` events ambiguous. The adapters enforce the rule (`FakeAdapter` in P1-20b2, `Mp4Adapter` in P1-17). The P1-20c conformance suite checks it on every `readParty()` when it is given an adapter factory.
  - `PlayerState` and `PartySnapshot` gained defaulted `operator==` (ADR 0002, amendment 13). The Director's snapshot diff uses it.
