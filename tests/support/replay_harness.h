#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "mpt/core/title_runtime.h"

namespace mpt::test {

// Produces the input for a given frame. Replays are just a recorded version of this.
using InputSource = std::function<InputFrame(std::uint64_t frame)>;

// Steps `runtime` for `frames` frames and returns stateHash() after every `interval` frames.
inline std::vector<std::uint64_t> run_replay(ITitleRuntime& runtime, const InputSource& inputs, std::uint64_t frames,
                                             std::uint64_t interval) {
  std::vector<std::uint64_t> hashes;
  for (std::uint64_t f = 0; f < frames; ++f) {
    runtime.stepFrame(inputs(f));
    if ((f + 1) % interval == 0) hashes.push_back(runtime.stateHash());
  }
  return hashes;
}

}  // namespace mpt::test
