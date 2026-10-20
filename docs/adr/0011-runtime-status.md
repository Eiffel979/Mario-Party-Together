# ADR 0011: Runtime status (halt and fault)

**Status:** Accepted
Author: architect, 2026-10-19. Approved by: architect, 2026-10-19. Additive core change: `kCoreContractVersion` stays 1 (ADR 0002, amendment 16).

## Context
`ITitleRuntime::stepFrame` returns nothing. A runtime has no way to say that its game can no longer run. P1-15's game-module ABI has two such outcomes:
- `HALTED`: the game's main loop returned;
- `FAULTED`: a deterministic fault, such as the SDK watchdog, a copy-destination trap, an unimplemented SDK call, or an exhausted arena.

The ADR 0008 ABI review left this open for core. Without it, `Mp4Runtime` could only log the fault and keep producing frozen frames. Replays, the dump lane and the shell would then report a hash mismatch, or nothing at all, instead of the real cause.

`RESET_REQUESTED` (`OSResetSystem`) is not covered here. The module handles it internally as a deterministic soft reset (ADR 0008, ABI review).

## Decision
`title_runtime.h` gains a status query with a default implementation:

```cpp
enum class RuntimeStatus : std::uint8_t {
  running,  // stepFrame() may be called
  halted,   // the game ended by itself (its main loop returned)
  faulted,  // the runtime hit a deterministic fault it cannot continue from
};

struct RuntimeHealth {
  RuntimeStatus status = RuntimeStatus::running;
  std::string message{};  // empty while running; why it stopped otherwise
};

class ITitleRuntime {
  ...
  // Valid after boot(). Changes only as a result of boot(), stepFrame() or loadState(),
  // so it is itself deterministic: every peer and replay sees the same status on the same
  // frame. halted and faulted are terminal until a loadState() restores a running state.
  virtual RuntimeHealth status() const { return {}; }
};
```

Rules:
1. **Deterministic.** The status is a function of the same inputs as `stateHash()` (ADR 0002 rule 2, as amended). A runtime never reports a fault caused by the host (out of host memory, a missing file at boot); those are `boot()` or `loadState()` failures. It is part of the runtime's state: `saveState`/`loadState` restore it.
2. **Terminal.** Once a runtime is not `running`, it must still accept `video()`, `audio()`, `stateHash()`, `saveState()` and the other queries. `stepFrame()` must not be called again until a `loadState()` brings back a running state.
3. **`Session` turns it into a `Result`:**
   - `Session::start` fails if the status after `boot()` and the ruleset is not `running`.
   - `Session::step` steps as usual, then checks the status. The frame that halted or faulted still counts: `frame()` advances, a save write is taken, and the Director observes it. Then `step` returns a failure naming the status, the frame and the message.
   - Every later `step` fails without calling `stepFrame`, until a `loadState` restores a running state.
   - The failure message starts with `runtime halted` or `runtime faulted`.
4. **Recording keeps the frame that stopped.** The step happened, so a recorder that receives a failed `step` with `frame()` advanced still appends its inputs. A replay then reproduces the halt or fault on its last frame. Two places do this: `ReplayRecorder` callers in the shell's `FrameLoop`, and `verify_replay`, which reports the failure, so a replay of a fault fails with the fault's message, not with a missing checkpoint.
5. **The default implementation always reports `running`.** Existing runtimes (`FakeRuntime`, test runtimes) need no change, and every existing hash, replay and golden trail is unchanged.

## Alternatives
- **Make `stepFrame` return a status.** Rejected: it changes a pure virtual signature, so every implementer would have to change. That would also be a contract version bump, invalidating replays, for no gain over a query.
- **A pure virtual `status()`.** Rejected for the same reason. With the default implementation the change stays additive (ADR 0002, amendment 16).
- **Throw from `stepFrame`.** Rejected: the contract has no exceptions, and the ABI behind `Mp4Runtime` is C.
- **Let the runtime keep stepping a frozen game.** Rejected: the failure would surface frames later as a hash difference, or not at all.

## Consequences
- `Mp4Runtime` (P1-16) maps the module's `HALTED`/`FAULTED` step results and `fault()` text to `status()`. Its conformance run then fails with the fault's message.
- The shell reports the failure and stops the session, exiting non-zero for `--replay`. A future UI shows the message.
- **For qa-reviewer (P1-20c):** the conformance suite should check that `status()` is `running` after `boot()`, stays `running` across a `saveState`/`loadState` round trip, and that `FakeRuntime` never leaves `running`.
- Netplay (Phase 2) needs nothing new: a deterministic fault happens on the same frame on every peer.

## Review (architect, 2026-10-19)
**Verdict: Accepted.** It is additive under ADR 0002 amendment 16: one new virtual function with a default, two new types, and new failure paths in `Session` that existing runtimes never take. No existing output changes, so `kCoreContractVersion` stays 1. `status()` adds a vtable entry, but runtimes are compiled into the same build as core. The only runtime boundary that crosses a compiler is the MP4 module's C ABI (ADR 0008), which this ADR does not touch.
