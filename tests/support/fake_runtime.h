#pragma once

#include <cstring>
#include <vector>

#include "mpt/core/rng.h"
#include "mpt/core/title_runtime.h"

namespace mpt::test {

// A tiny deterministic "game" used to exercise the runtime contract and the
// replay harness without any real title. Its state is a few counters that mix
// player input with a seeded RNG every frame.
class FakeRuntime final : public ITitleRuntime {
 public:
  TitleId id() const override { return TitleId::mp4; }

  Result boot(const VerifiedDump&, const RuntimeConfig& config) override {
    state_ = State{};
    state_.rng_state = config.seed;
    return Result::success();
  }

  void stepFrame(const InputFrame& inputs) override {
    DeterministicRng rng(state_.rng_state);
    for (std::size_t p = 0; p < kMaxPlayers; ++p) {
      const PadState& pad = inputs.pads[p];
      state_.score[p] += static_cast<std::int64_t>(pad.buttons & kButtonA ? rng.below(10) : 0);
      state_.score[p] += pad.stick_x / 1024;
    }
    state_.rng_state = rng.next();
    state_.frame = inputs.frame + 1;
    std::memset(pixels_.data(), static_cast<int>(state_.frame & 0xff), pixels_.size() * sizeof(std::uint32_t));
  }

  FrameOutput video() override {
    return FrameOutput{FrameOutput::Kind::cpu_rgba8, kWidth, kHeight, pixels_, 0};
  }
  AudioChunk audio() override { return AudioChunk{48000, 2, {}}; }

  SaveState saveState() override {
    SaveState out{TitleId::mp4, 1, state_.frame, std::vector<std::byte>(sizeof(State))};
    std::memcpy(out.data.data(), &state_, sizeof(State));
    return out;
  }

  Result loadState(const SaveState& state) override {
    if (state.title != TitleId::mp4 || state.format_version != 1 || state.data.size() != sizeof(State)) {
      return Result::failure("incompatible save state");
    }
    std::memcpy(&state_, state.data.data(), sizeof(State));
    return Result::success();
  }

  std::uint64_t stateHash() override {
    StateHasher hasher;
    hasher.update_u64(state_.frame);
    hasher.update_u64(state_.rng_state);
    for (std::int64_t s : state_.score) hasher.update_u64(static_cast<std::uint64_t>(s));
    return hasher.digest();
  }

  // Contract v1 compile stubs (P1-03 hand-off). P1-20a replaces these with the real
  // v1 behaviour: a counter save image that is part of State and stateHash(), and
  // generation tracking (ADR 0004 Consequences).
  FrameRate frameRate() const override { return FrameRate{60, 1}; }
  std::uint64_t saveMediaGeneration() const override { return 0; }
  SaveMedia saveMedia() const override { return SaveMedia{}; }

 private:
  static constexpr std::uint32_t kWidth = 8;
  static constexpr std::uint32_t kHeight = 8;

  struct State {
    std::uint64_t frame = 0;
    std::uint64_t rng_state = 0;
    std::int64_t score[kMaxPlayers] = {};
  };

  State state_;
  std::vector<std::uint32_t> pixels_ = std::vector<std::uint32_t>(kWidth * kHeight);
};

}  // namespace mpt::test
