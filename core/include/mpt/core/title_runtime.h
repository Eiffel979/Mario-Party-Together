#pragma once

#include <cstdint>
#include <optional>

#include "mpt/core/dump.h"
#include "mpt/core/input.h"
#include "mpt/core/media.h"
#include "mpt/core/result.h"
#include "mpt/core/save_media.h"
#include "mpt/core/state.h"
#include "mpt/core/title_id.h"

namespace mpt {

// Version of the core contract. It covers every header in mpt/core/ listed in
// ADR 0002, including serialized layouts (PadState, InputFrame), TitleId values,
// the StateHasher algorithm and the normative rules in ADRs 0002-0005, not only
// ITitleRuntime and ITitleAdapter. Any change bumps it and needs an ADR.
//   v0: ADR 0002.
//   v1: ADR 0003 (frameRate), ADR 0004 (save media), ADR 0005 (PartySnapshot::phase,
//       current_minigame).
inline constexpr std::uint32_t kCoreContractVersion = 1;

// Frames per second as an exact ratio, e.g. {60000, 1001} for NTSC, {50, 1} for PAL
// (ADR 0003). Both fields are > 0. No floating point: 59.94 is not exact in binary.
struct FrameRate {
  std::uint32_t numerator = 60;
  std::uint32_t denominator = 1;

  friend bool operator==(const FrameRate&, const FrameRate&) = default;
};

struct RuntimeConfig {
  // Replaces every RNG seed source the title has. Determinism input (ADR 0002 rule 2).
  // Derived by the host from the session seed through DeterministicRng, as is
  // ResolvedRuleset::seed; the Session defines the derivation (ADR 0005). In netplay
  // the host's value is the truth.
  std::uint64_t seed = 0;

  // Local presentation options. They must not affect stateHash(), so peers may choose
  // them freely. A runtime that cannot keep an option out of game state must ignore it
  // during netplay (ADR 0002 rule 2).
  std::uint32_t internal_scale = 1;  // render resolution multiplier
  bool widescreen = false;

  // Initial persistent save image. nullopt = blank media (a fresh, formatted card).
  // Determinism input (ADR 0004 rule 3). Replays and tests use blank media unless the
  // replay says otherwise (ADR 0006).
  std::optional<SaveMedia> save_media;
};

// Runs one title's game code. However the title is implemented (static recomp,
// decomp port, embedded core), the shell drives it only through this interface.
//
// Determinism contract (ADR 0002 rules 2 and 4, amended by ADR 0004 rule 3): the
// stateHash() sequence is a function of exactly the dump identity (title + sha256,
// never build_dir), RuntimeConfig::seed, the initial RuntimeConfig::save_media bytes
// and the InputFrame sequence. It is identical across supported platforms, compilers
// and processes, and never covers pointers, padding or uninitialized memory.
// Implementations must not read wall-clock time, unseeded RNGs or thread timing.
//
// Instances (ADR 0002 rule 10): a runtime may support only one live instance per
// process. Callers must not create a second instance of a title while one is alive
// unless docs/titles/<id>.md says it is safe.
class ITitleRuntime {
 public:
  virtual ~ITitleRuntime() = default;

  virtual TitleId id() const = 0;
  virtual Result boot(const VerifiedDump& dump, const RuntimeConfig& config) = 0;

  // Advances exactly one frame. inputs.frame equals the number of frames stepped so
  // far, counted from boot() or from the frame of the last successful loadState()
  // (ADR 0002 rule 3).
  virtual void stepFrame(const InputFrame& inputs) = 0;

  // Output of the last stepFrame(). The views stay valid until the next stepFrame(),
  // loadState() or destruction of the runtime, whichever comes first (ADR 0002 rule 6).
  virtual FrameOutput video() = 0;
  virtual AudioChunk audio() = 0;

  // SaveState::data must load in the same runtime build on any supported platform.
  // After loadState(s) succeeds, stateHash() equals the producer's hash at s.frame and
  // the next stepFrame() takes inputs.frame == s.frame (ADR 0002 rules 3 and 5).
  virtual SaveState saveState() = 0;
  virtual Result loadState(const SaveState& state) = 0;

  // Hash of all game state that affects future frames (not video or audio buffers).
  virtual std::uint64_t stateHash() = 0;

  // Native frame rate (ADR 0003). Valid after a successful boot(). May change only as
  // a result of stepFrame() or loadState() (e.g. a PAL 50/60 switch), so it is itself
  // deterministic. The shell reads it after every stepFrame() and paces with integer
  // nanoseconds; it never enters game logic or stateHash() as wall-clock time.
  virtual FrameRate frameRate() const = 0;

  // Save media (ADR 0004). The generation starts at 0 after boot() and increments each
  // time the game commits a write to its save media. Writes complete after a fixed,
  // documented number of frames, so the generation changes on the same frame on every
  // peer.
  //
  // The image and the generation are game state and are part of SaveState:
  // loadState(s) restores both to the producer's values at s.frame, so a rewind can
  // lower the generation (ADR 0004 rule 9).
  //
  // The shell detects writes only across stepFrame(): it compares the generation just
  // before and just after each step and persists saveMedia() when they differ. A
  // loadState() is a resync point, not a write. The shell takes the restored generation
  // as its new baseline and persists nothing, so rewind and rollback never persist by
  // themselves or miss a later write (ADR 0004 rule 10). Ephemeral sessions (replays,
  // tests, netplay guests) never persist.
  virtual std::uint64_t saveMediaGeneration() const = 0;
  // Current image. Only meaningful to persist when the generation has changed.
  virtual SaveMedia saveMedia() const = 0;
};

}  // namespace mpt
