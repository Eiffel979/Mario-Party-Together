#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "mpt/core/content_id.h"
#include "mpt/core/input.h"

namespace mpt {

// Where a party is (ADR 0005). Adapters fill it as well as the title allows. A title
// that cannot tell `results` from `finished` must report `finished` only once the
// results are final. Values are stable: append only, never reorder.
enum class PartyPhase : std::uint8_t {
  none,      // not in a party (menus, title screen)
  setup,     // party settings, character select
  board,     // board turns
  minigame,  // a minigame is running
  results,   // end-of-party bonus stars and results
  finished,  // the party is over and its results are final
};

// Canonical party state, owned by the Party Director. Adapters mirror their
// title's in-memory state into this and write Director decisions back.
struct PlayerState {
  std::optional<CharacterId> character{};
  bool is_cpu = false;
  std::int32_t coins = 0;
  std::int32_t stars = 0;
  std::int32_t board_space = -1;  // title-defined space index, -1 if unknown

  friend bool operator==(const PlayerState&, const PlayerState&) = default;
};

struct PartySnapshot {
  std::optional<BoardId> board{};
  std::int32_t turn = 0;        // 1-based current turn, 0 before the party starts
  std::int32_t max_turns = 0;
  std::array<PlayerState, kMaxPlayers> players{};
  PartyPhase phase = PartyPhase::none;
  // Set exactly when phase == minigame, nullopt in every other phase (ADR 0005).
  std::optional<MinigameId> current_minigame{};

  friend bool operator==(const PartySnapshot&, const PartySnapshot&) = default;
};

struct MinigameSetup {
  std::array<std::optional<CharacterId>, kMaxPlayers> characters{};
  std::array<std::uint8_t, kMaxPlayers> teams{};  // team index per player (e.g. 1v3, 2v2)
  std::optional<std::uint32_t> time_limit_frames{};
  std::uint64_t seed = 0;
};

struct MinigameResult {
  MinigameId minigame;
  std::array<std::uint8_t, kMaxPlayers> ranking{};    // 1 = first place
  std::array<std::int32_t, kMaxPlayers> coin_delta{};
};

}  // namespace mpt
