// The MP4 catalog tables (catalog.cpp, docs/titles/mp4.md section 12) and their lookups.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "mpt/core/content_id.h"
#include "mpt/core/title_adapter.h"
#include "mpt/mp4/adapter.h"

namespace mpt::mp4 {

struct BoardEntry {
  std::int32_t value;  // GWSystem.board
  const char* id;
  const char* name;
};
struct MinigameEntry {
  std::int32_t number;    // the overlay mNNNDll
  std::int32_t category;  // mgInfoTbl type: 0 4-player, 1 1-vs-3, 2 2-vs-2, 3 Bowser, 4 Battle, 5 Fortune,
                          // 6 Item, 7 Extra Room, 8 Story
  const char* id;
  const char* name;
};
struct CharacterEntry {
  std::int32_t value;  // GWPlayerCfg.character
  const char* id;
  const char* name;
};

extern const std::array<BoardEntry, 8> kBoards;
extern const std::array<MinigameEntry, 61> kMinigames;
extern const std::array<CharacterEntry, 8> kCharacters;

// Every board, minigame and character, in the tables' order.
Catalog mp4_catalog();

// board_for, character_for and minigame_for_overlay are declared in mpt/mp4/adapter.h. An overlay name
// is "m" and three digits, then "dll" in any case ("m401Dll", "m407dll").

}  // namespace mpt::mp4
