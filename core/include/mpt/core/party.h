#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "mpt/core/content_id.h"
#include "mpt/core/input.h"

namespace mpt {

// Canonical party state, owned by the Party Director. Adapters mirror their
// title's in-memory state into this and write Director decisions back.
struct PlayerState {
  std::optional<CharacterId> character;
  bool is_cpu = false;
  std::int32_t coins = 0;
  std::int32_t stars = 0;
  std::int32_t board_space = -1;  // title-defined space index, -1 if unknown
};

struct PartySnapshot {
  std::optional<BoardId> board;
  std::int32_t turn = 0;        // 1-based current turn, 0 before the party starts
  std::int32_t max_turns = 0;
  std::array<PlayerState, kMaxPlayers> players{};
};

struct MinigameSetup {
  std::array<std::optional<CharacterId>, kMaxPlayers> characters{};
  std::array<std::uint8_t, kMaxPlayers> teams{};  // team index per player (e.g. 1v3, 2v2)
  std::optional<std::uint32_t> time_limit_frames;
  std::uint64_t seed = 0;
};

struct MinigameResult {
  MinigameId minigame;
  std::array<std::uint8_t, kMaxPlayers> ranking{};    // 1 = first place
  std::array<std::int32_t, kMaxPlayers> coin_delta{};
};

}  // namespace mpt
