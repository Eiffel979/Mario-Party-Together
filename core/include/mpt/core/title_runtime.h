#pragma once

#include <cstdint>

#include "mpt/core/dump.h"
#include "mpt/core/input.h"
#include "mpt/core/media.h"
#include "mpt/core/result.h"
#include "mpt/core/state.h"
#include "mpt/core/title_id.h"

namespace mpt {

// Contract version for ITitleRuntime/ITitleAdapter. Bump on any change (ADR required).
inline constexpr std::uint32_t kCoreContractVersion = 0;

struct RuntimeConfig {
  std::uint64_t seed = 0;            // replaces every RNG seed source the title has
  std::uint32_t internal_scale = 1;  // render resolution multiplier
  bool widescreen = false;
};

// Runs one title's game code. However the title is implemented (static recomp,
// decomp port, embedded core), the shell drives it only through this interface.
//
// Determinism contract: given the same VerifiedDump, RuntimeConfig and sequence
// of InputFrames, stepFrame() must produce the same stateHash() on every platform.
// Implementations must not read wall-clock time, unseeded RNGs or thread timing.
class ITitleRuntime {
 public:
  virtual ~ITitleRuntime() = default;

  virtual TitleId id() const = 0;
  virtual Result boot(const VerifiedDump& dump, const RuntimeConfig& config) = 0;

  // Advances exactly one frame. inputs.frame equals the number of frames stepped so far.
  virtual void stepFrame(const InputFrame& inputs) = 0;

  // Output of the last stepFrame(). Valid until the next stepFrame() call.
  virtual FrameOutput video() = 0;
  virtual AudioChunk audio() = 0;

  virtual SaveState saveState() = 0;
  virtual Result loadState(const SaveState& state) = 0;

  // Hash of all game state that affects future frames (not video or audio buffers).
  virtual std::uint64_t stateHash() = 0;
};

}  // namespace mpt
