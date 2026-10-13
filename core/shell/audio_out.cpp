#include "audio_out.h"

namespace mpt::shell {

AudioOut::~AudioOut() {
  if (stream_) SDL_DestroyAudioStream(stream_);  // also closes the device it opened
}

void AudioOut::push(const AudioChunk& chunk) {
  if (failed_ || chunk.sample_rate == 0 || chunk.channels == 0 || chunk.samples.empty()) return;

  const SDL_AudioSpec spec{SDL_AUDIO_S16, chunk.channels, static_cast<int>(chunk.sample_rate)};
  if (!stream_) {
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_) {
      failed_ = true;  // no audio device: run silent
      return;
    }
    SDL_ResumeAudioStreamDevice(stream_);
  } else if (chunk.sample_rate != rate_ || chunk.channels != channels_) {
    SDL_SetAudioStreamFormat(stream_, &spec, nullptr);
  }
  rate_ = chunk.sample_rate;
  channels_ = chunk.channels;

  const std::uint64_t bytes_per_second = static_cast<std::uint64_t>(rate_) * channels_ * 2;
  const std::uint64_t max_queued = bytes_per_second * kMaxQueuedMs / 1000;
  const int queued = SDL_GetAudioStreamQueued(stream_);
  if (queued >= 0 && static_cast<std::uint64_t>(queued) > max_queued) return;

  SDL_PutAudioStreamData(stream_, chunk.samples.data(), static_cast<int>(chunk.samples.size_bytes()));
}

void AudioOut::pause() {
  if (!stream_) return;
  SDL_PauseAudioStreamDevice(stream_);
  SDL_ClearAudioStream(stream_);
}

void AudioOut::resume() {
  if (stream_) SDL_ResumeAudioStreamDevice(stream_);
}

}  // namespace mpt::shell
