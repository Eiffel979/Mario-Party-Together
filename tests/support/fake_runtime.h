#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

#include "mpt/core/party.h"
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
// - Party progression (P1-20b2): see kAdvanceButton. FakeAdapter mirrors it.
// - SaveState format_version 3: every State field as a little-endian u64, in a fixed
//   order, so the bytes are the same on every platform (no memcpy of the struct).
class FakeRuntime final : public ITitleRuntime {
 public:
  static constexpr std::uint32_t kStateFormat = 3;
  static constexpr std::uint32_t kSaveMediaFormat = 1;
  static constexpr std::size_t kSaveMediaBytes = 8;
  static constexpr FrameRate kRate60{60, 1};
  static constexpr FrameRate kRate50{50, 1};
  static constexpr std::uint32_t kSaveButton = kButtonStart;      // on port 0
  static constexpr std::uint32_t kRateToggleButton = kButtonSelect;  // on port 0

  // Party progression (P1-20b2), read by FakeAdapter. A press of kAdvanceButton on port 0
  // moves the party one step: none -> setup -> board (turn 1) -> minigame -> board
  // (turn 2) -> ... -> minigame on the last turn -> results -> finished. Finished stays
  // finished until the next boot(). Entering a minigame picks it with the game RNG and
  // moves every player 1-6 spaces; leaving one pays every player 0-10 coins; leaving
  // results gives a star to every player tied for the most coins.
  static constexpr std::uint32_t kAdvanceButton = kButtonZ;  // on port 0
  static constexpr std::uint32_t kMaxTurns = 3;
  static constexpr std::int32_t kStartingCoins = 10;
  static constexpr std::string_view kBoardName = "fake_board";
  static constexpr std::array<std::string_view, 3> kMinigameNames = {"fake_mash", "fake_dodge", "fake_race"};
  static constexpr std::array<std::string_view, kMaxPlayers> kCharacterNames = {"fake_red", "fake_blue", "fake_green",
                                                                                 "fake_yellow"};

  struct Party {
    PartyPhase phase = PartyPhase::none;
    std::uint32_t turn = 0;      // 1-based once on the board, 0 before
    std::uint32_t minigame = 0;  // 1-based index into kMinigameNames while phase == minigame, else 0
    std::array<std::int32_t, kMaxPlayers> coins{};
    std::array<std::int32_t, kMaxPlayers> stars{};
    std::array<std::int32_t, kMaxPlayers> board_space = {-1, -1, -1, -1};  // -1 before the board

    friend bool operator==(const Party&, const Party&) = default;
  };

  // The current party state. Cheap: a reference into State.
  const Party& party() const { return state_.party; }

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
    if (pressed & kAdvanceButton) advance_party();

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
    Party party;
  };

  // State as a fixed sequence of u64 words: the SaveState layout and the hash input.
  static constexpr std::size_t kStateWords = 2 + kMaxPlayers + 5 + 3 + 3 * kMaxPlayers;

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
    w[i++] = static_cast<std::uint64_t>(s.party.phase);
    w[i++] = s.party.turn;
    w[i++] = s.party.minigame;
    for (std::int32_t v : s.party.coins) w[i++] = from_i32(v);
    for (std::int32_t v : s.party.stars) w[i++] = from_i32(v);
    for (std::int32_t v : s.party.board_space) w[i++] = from_i32(v);
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
    const std::uint64_t phase = w[i++];
    const std::uint64_t turn = w[i++];
    const std::uint64_t minigame = w[i++];
    if (phase > static_cast<std::uint64_t>(PartyPhase::finished) || turn > kMaxTurns ||
        minigame > kMinigameNames.size() || (minigame != 0) != (phase == static_cast<std::uint64_t>(PartyPhase::minigame))) {
      return std::nullopt;
    }
    s.party.phase = static_cast<PartyPhase>(phase);
    s.party.turn = static_cast<std::uint32_t>(turn);
    s.party.minigame = static_cast<std::uint32_t>(minigame);
    for (std::int32_t& v : s.party.coins) if (!to_i32(w[i++], v)) return std::nullopt;
    for (std::int32_t& v : s.party.stars) if (!to_i32(w[i++], v)) return std::nullopt;
    for (std::int32_t& v : s.party.board_space) if (!to_i32(w[i++], v)) return std::nullopt;
    return s;
  }

  void advance_party() {
    Party& current = state_.party;
    DeterministicRng rng(state_.rng_state);
    switch (current.phase) {
      case PartyPhase::none:
        current = Party{};
        current.phase = PartyPhase::setup;
        break;
      case PartyPhase::setup:
        current.phase = PartyPhase::board;
        current.turn = 1;
        current.coins.fill(kStartingCoins);
        current.board_space.fill(0);
        break;
      case PartyPhase::board:
        current.phase = PartyPhase::minigame;
        current.minigame = 1 + static_cast<std::uint32_t>(rng.below(kMinigameNames.size()));
        for (std::int32_t& space : current.board_space) space += 1 + static_cast<std::int32_t>(rng.below(6));
        break;
      case PartyPhase::minigame:
        current.minigame = 0;
        for (std::int32_t& coins : current.coins) coins += static_cast<std::int32_t>(rng.below(11));
        if (current.turn >= kMaxTurns) {
          current.phase = PartyPhase::results;
        } else {
          current.phase = PartyPhase::board;
          ++current.turn;
        }
        break;
      case PartyPhase::results: {
        std::int32_t most = current.coins[0];
        for (std::int32_t coins : current.coins) most = coins > most ? coins : most;
        for (std::size_t p = 0; p < kMaxPlayers; ++p) {
          if (current.coins[p] == most) ++current.stars[p];
        }
        current.phase = PartyPhase::finished;
        break;
      }
      case PartyPhase::finished:
        break;
    }
    state_.rng_state = rng.next();
  }

  static std::uint64_t from_i32(std::int32_t v) { return static_cast<std::uint64_t>(static_cast<std::int64_t>(v)); }
  static bool to_i32(std::uint64_t w, std::int32_t& out) {
    const auto v = static_cast<std::int64_t>(w);
    if (v < INT32_MIN || v > INT32_MAX) return false;
    out = static_cast<std::int32_t>(v);
    return true;
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
