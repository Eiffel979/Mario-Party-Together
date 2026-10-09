#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "mpt/core/content_id.h"

namespace mpt {

// A ruleset after rules/ has validated it and resolved it against one title.
// Unset fields mean "keep the title's own behavior". rules/ owns the JSON format;
// this struct is only the contract adapters consume.
struct ResolvedRuleset {
  // Host-chosen; every peer gets the same value. Derived by the host from the session
  // seed through DeterministicRng, as is RuntimeConfig::seed (ADR 0005 Session).
  std::uint64_t seed = 0;
  std::optional<BoardId> board;
  std::optional<std::int32_t> turn_count;
  std::optional<std::int32_t> starting_coins;
  std::optional<std::int32_t> star_cost;
  std::optional<std::int32_t> starting_stars;
  std::optional<bool> bonus_stars;
  std::optional<std::uint32_t> minigame_time_limit_frames;
  std::vector<MinigameId> minigame_pool;  // empty = title default pool
};

}  // namespace mpt
