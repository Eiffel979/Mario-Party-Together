#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "mpt/core/rng.h"
#include "mpt/core/title_runtime.h"

namespace mpt::test {

// A tiny deterministic "game" used to exercise the runtime contract (v1) and the
// replay harness without any real title. Its state is a few counters that mix
// player input with a seeded RNG every frame.
//
// Contract v1 behaviour, so tests and the conformance suite (P1-20c) have something
// real to check:
// - Frame rate (ADR 0003): {60, 1} after boot(). Pressing Select on port 0 (a press,
//   not a hold) toggles it between {60, 1} and {50, 1} during that stepFrame(), the
//   way a game switches PAL 50/60. The rate is game state: it is in State, saveState()
//   and stateHash().
// - Save media (ADR 0004): the image is a counter, kSaveMediaBytes little-endian bytes
//   with format_version kSaveMediaFormat. config.save_media == nullopt means a blank,
//   formatted card (counter 0). Any other version or size makes boot() fail. Pressing
//   Start on port 0 commits a write during that stepFrame(), with zero frames of
//   latency: the counter and saveMediaGeneration() both go up by one. The generation
//   is 0 after every boot(). Image and generation are in State, saveState() and
//   stateHash().
// - SaveState format_version 2: every State field as a little-endian u64, in a fixed
//   order, so the bytes are the same on every platform (no memcpy of the struct).
class FakeRuntime final : public ITitleRuntime {
 public:
  static constexpr std::uint32_t kStateFormat = 2;
  static constexpr std::uint32_t kSaveMediaFormat = 1;
  static constexpr std::size_t kSaveMediaBytes = 8;
  static constexpr FrameRate kRate60{60, 1};
  static constexpr FrameRate kRate50{50, 1};
  static constexpr std::uint32_t kSaveButton = kButtonStart;      // on port 0
  static constexpr std::uint32_t kRateToggleButton = kButtonSelect;  // on port 0

  // A save image in this runtime's format holding 'counter'.
  static SaveMedia make_save_media(std::uint64_t counter) {
    SaveMedia media;
    media.format_version = kSaveMediaFormat;
    media.data.resize(kSaveMediaBytes);
    put_u64(media.data, 0, counter);
    return media;
  }

  TitleId id() const override { return TitleId::mp4; }

  Result boot(const VerifiedDump&, const RuntimeConfig& config) override {
    std::uint64_t counter = 0;
    if (config.save_media) {
      const SaveMedia& media = *config.save_media;
      if (media.format_version != kSaveMediaFormat || media.data.size() != kSaveMediaBytes) {
        return Result::failure("unsupported save media for FakeRuntime");
      }
      counter = get_u64(media.data, 0);
    }
    state_ = State{};
    state_.rng_state = config.seed;
    state_.save_counter = counter;
    return Result::success();
  }

  void stepFrame(const InputFrame& inputs) override {
    DeterministicRng rng(state_.rng_state);
    for (std::size_t p = 0; p < kMaxPlayers; ++p) {
      const PadState& pad = inputs.pads[p];
      state_.score[p] += static_cast<std::int64_t>(pad.buttons & kButtonA ? rng.below(10) : 0);
      // -32768 is treated exactly as -32767 (pad conventions in core/input.h).
      const int stick_x = pad.stick_x < -32767 ? -32767 : pad.stick_x;
      state_.score[p] += stick_x / 1024;
    }
    state_.rng_state = rng.next();

    // Port 0 presses (released last frame, held now) drive the v1 test hooks.
    const std::uint32_t held = inputs.pads[0].buttons;
    const std::uint32_t pressed = held & ~state_.port0_buttons;
    state_.port0_buttons = held;
    if (pressed & kRateToggleButton) {
      state_.rate = state_.rate == kRate60 ? kRate50 : kRate60;
    }
    if (pressed & kSaveButton) {
      ++state_.save_counter;
      ++state_.save_generation;
    }

    state_.frame = inputs.frame + 1;
    std::memset(pixels_.data(), static_cast<int>(state_.frame & 0xff), pixels_.size() * sizeof(std::uint32_t));
  }

  FrameOutput video() override {
    return FrameOutput{FrameOutput::Kind::cpu_rgba8, kWidth, kHeight, pixels_, 0};
  }
  AudioChunk audio() override { return AudioChunk{48000, 2, {}}; }

  SaveState saveState() override {
    SaveState out;
    out.title = TitleId::mp4;
    out.format_version = kStateFormat;
    out.frame = state_.frame;
    out.data.resize(kStateWords * 8);
    const std::array<std::uint64_t, kStateWords> words = to_words(state_);
    for (std::size_t i = 0; i < kStateWords; ++i) put_u64(out.data, i * 8, words[i]);
    return out;
  }

  Result loadState(const SaveState& state) override {
    if (state.title != TitleId::mp4 || state.format_version != kStateFormat || state.data.size() != kStateWords * 8) {
      return Result::failure("incompatible save state");
    }
    std::array<std::uint64_t, kStateWords> words{};
    for (std::size_t i = 0; i < kStateWords; ++i) words[i] = get_u64(state.data, i * 8);
    const std::optional<State> loaded = from_words(words);
    if (!loaded || loaded->frame != state.frame) return Result::failure("corrupt save state");
    state_ = *loaded;
    return Result::success();
  }

  std::uint64_t stateHash() override {
    StateHasher hasher;
    for (std::uint64_t word : to_words(state_)) hasher.update_u64(word);
    return hasher.digest();
  }

  FrameRate frameRate() const override { return state_.rate; }
  std::uint64_t saveMediaGeneration() const override { return state_.save_generation; }
  SaveMedia saveMedia() const override { return make_save_media(state_.save_counter); }

 private:
  static constexpr std::uint32_t kWidth = 8;
  static constexpr std::uint32_t kHeight = 8;

  struct State {
    std::uint64_t frame = 0;
    std::uint64_t rng_state = 0;
    std::int64_t score[kMaxPlayers] = {};
    FrameRate rate = kRate60;
    std::uint32_t port0_buttons = 0;  // last frame's buttons, for press detection
    std::uint64_t save_counter = 0;   // the save image's contents
    std::uint64_t save_generation = 0;
  };

  // State as a fixed sequence of u64 words: the SaveState layout and the hash input.
  static constexpr std::size_t kStateWords = 2 + kMaxPlayers + 5;

  static std::array<std::uint64_t, kStateWords> to_words(const State& s) {
    std::array<std::uint64_t, kStateWords> w{};
    std::size_t i = 0;
    w[i++] = s.frame;
    w[i++] = s.rng_state;
    for (std::int64_t score : s.score) w[i++] = static_cast<std::uint64_t>(score);
    w[i++] = s.rate.numerator;
    w[i++] = s.rate.denominator;
    w[i++] = s.port0_buttons;
    w[i++] = s.save_counter;
    w[i++] = s.save_generation;
    return w;
  }

  static std::optional<State> from_words(const std::array<std::uint64_t, kStateWords>& w) {
    State s;
    std::size_t i = 0;
    s.frame = w[i++];
    s.rng_state = w[i++];
    for (std::int64_t& score : s.score) score = static_cast<std::int64_t>(w[i++]);
    const std::uint64_t numerator = w[i++];
    const std::uint64_t denominator = w[i++];
    const std::uint64_t buttons = w[i++];
    if (numerator == 0 || denominator == 0 || numerator > UINT32_MAX || denominator > UINT32_MAX ||
        buttons > UINT32_MAX) {
      return std::nullopt;
    }
    s.rate = FrameRate{static_cast<std::uint32_t>(numerator), static_cast<std::uint32_t>(denominator)};
    s.port0_buttons = static_cast<std::uint32_t>(buttons);
    s.save_counter = w[i++];
    s.save_generation = w[i++];
    return s;
  }

  static void put_u64(std::vector<std::byte>& out, std::size_t at, std::uint64_t value) {
    for (std::size_t b = 0; b < 8; ++b) out[at + b] = static_cast<std::byte>((value >> (8 * b)) & 0xff);
  }
  static std::uint64_t get_u64(const std::vector<std::byte>& in, std::size_t at) {
    std::uint64_t value = 0;
    for (std::size_t b = 0; b < 8; ++b) value |= std::to_integer<std::uint64_t>(in[at + b]) << (8 * b);
    return value;
  }

  State state_;
  std::vector<std::uint32_t> pixels_ = std::vector<std::uint32_t>(kWidth * kHeight);
};

}  // namespace mpt::test
