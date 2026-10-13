#pragma once

// Audio output (P1-06 d). Each frame's AudioChunk goes into an SDL_AudioStream in the
// runtime's own format; SDL resamples to the device (ADR 0003 rule 4: the chunk's
// sample_rate is authoritative, audio is never paced from the frame rate). Output is
// presentation only and never feeds back into the session.

#include <cstdint>

#include <SDL3/SDL_audio.h>

#include "mpt/core/media.h"

namespace mpt::shell {

class AudioOut {
 public:
  AudioOut() = default;
  ~AudioOut();
  AudioOut(const AudioOut&) = delete;
  AudioOut& operator=(const AudioOut&) = delete;

  // Queues one frame's samples. Opens the device on first use and follows format
  // changes. If more than kMaxQueuedMs is already queued (the host fell behind or the
  // device is slow), the chunk is dropped to keep latency bounded.
  void push(const AudioChunk& chunk);

  // Stops output and drops what is queued (pause, stop).
  void pause();
  void resume();

  static constexpr std::uint32_t kMaxQueuedMs = 150;

 private:
  SDL_AudioStream* stream_ = nullptr;
  std::uint32_t rate_ = 0;
  std::uint8_t channels_ = 0;
  bool failed_ = false;
};

}  // namespace mpt::shell
