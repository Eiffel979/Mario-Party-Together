// The MP4 catalog (docs/titles/mp4.md section 12): content IDs, display names and the game values
// they map from. Generated from that table once and kept in step with it by hand; the unit test
// checks the counts and that every ID is unique.
#include "catalog.h"

#include <cctype>
#include <string>

namespace mpt::mp4 {

// GWSystem.board (BOARD_ID_* in the game's board/main.h): 0-5 the party boards, 6 the tutorial (not
// catalogued), 7 and 8 the Extra Room board modes.
const std::array<BoardEntry, 8> kBoards = {{
    {0, "mp4.board.toads_midway_madness", "Toad's Midway Madness"},
    {1, "mp4.board.goombas_greedy_gala", "Goomba's Greedy Gala"},
    {2, "mp4.board.shy_guys_jungle_jam", "Shy Guy's Jungle Jam"},
    {3, "mp4.board.boos_haunted_bash", "Boo's Haunted Bash"},
    {4, "mp4.board.koopas_seaside_soiree", "Koopa's Seaside Soiree"},
    {5, "mp4.board.bowsers_gnarly_party", "Bowser's Gnarly Party"},
    {7, "mp4.board.mega_board_mayhem", "Mega Board Mayhem"},
    {8, "mp4.board.mini_board_mad_dash", "Mini Board Mad Dash"},
}};

// The minigame overlays m401Dll..m463Dll by number, with the game's category (mgInfoTbl type).
const std::array<MinigameEntry, 61> kMinigames = {{
    {401, 0, "mp4.minigame.manta_rings", "Manta Rings"},
    {402, 0, "mp4.minigame.slime_time", "Slime Time"},
    {403, 0, "mp4.minigame.booksquirm", "Booksquirm"},
    {404, 4, "mp4.minigame.trace_race", "Trace Race"},
    {405, 0, "mp4.minigame.mario_medley", "Mario Medley"},
    {406, 0, "mp4.minigame.avalanche", "Avalanche!"},
    {407, 0, "mp4.minigame.domination", "Domination"},
    {408, 0, "mp4.minigame.paratrooper_plunge", "Paratrooper Plunge"},
    {409, 0, "mp4.minigame.toads_quick_draw", "Toad's Quick Draw"},
    {410, 0, "mp4.minigame.three_throw", "Three Throw"},
    {411, 0, "mp4.minigame.photo_finish", "Photo Finish"},
    {412, 0, "mp4.minigame.mr_blizzards_brigade", "Mr. Blizzard's Brigade"},
    {413, 0, "mp4.minigame.bob_omb_breakers", "Bob-omb Breakers"},
    {414, 0, "mp4.minigame.long_claw_of_the_law", "Long Claw of the Law"},
    {415, 0, "mp4.minigame.stamp_out", "Stamp Out!"},
    {416, 1, "mp4.minigame.candlelight_flight", "Candlelight Flight"},
    {417, 1, "mp4.minigame.makin_waves", "Makin' Waves"},
    {418, 1, "mp4.minigame.hide_and_go_boom", "Hide and Go BOOM!"},
    {419, 1, "mp4.minigame.tree_stomp", "Tree Stomp"},
    {420, 1, "mp4.minigame.fish_n_drips", "Fish n' Drips"},
    {421, 1, "mp4.minigame.hop_or_pop", "Hop or Pop"},
    {422, 1, "mp4.minigame.money_belts", "Money Belts"},
    {423, 1, "mp4.minigame.goooooooal", "GOOOOOOOAL!!"},
    {424, 1, "mp4.minigame.blame_it_on_the_crane", "Blame it on the Crane"},
    {425, 2, "mp4.minigame.the_great_deflate", "The Great Deflate"},
    {426, 2, "mp4.minigame.revers_a_bomb", "Revers-a-Bomb"},
    {427, 2, "mp4.minigame.right_oar_left", "Right Oar Left?"},
    {428, 2, "mp4.minigame.cliffhangers", "Cliffhangers"},
    {429, 2, "mp4.minigame.team_treasure_trek", "Team Treasure Trek"},
    {430, 2, "mp4.minigame.pair_a_sailing", "Pair-a-sailing"},
    {431, 2, "mp4.minigame.order_up", "Order Up"},
    {432, 2, "mp4.minigame.dungeon_duos", "Dungeon Duos"},
    {433, 7, "mp4.minigame.beach_volley_folly", "Beach Volley Folly"},
    {434, 2, "mp4.minigame.cheep_cheep_sweep", "Cheep Cheep Sweep"},
    {435, 3, "mp4.minigame.darts_of_doom", "Darts of Doom"},
    {436, 3, "mp4.minigame.fruits_of_doom", "Fruits of Doom"},
    {437, 3, "mp4.minigame.balloon_of_doom", "Balloon of Doom"},
    {438, 4, "mp4.minigame.chain_chomp_fever", "Chain Chomp Fever"},
    {439, 4, "mp4.minigame.paths_of_peril", "Paths of Peril"},
    {440, 4, "mp4.minigame.bowsers_bigger_blast", "Bowser's Bigger Blast"},
    {441, 4, "mp4.minigame.butterfly_blitz", "Butterfly Blitz"},
    {442, 7, "mp4.minigame.barrel_baron", "Barrel Baron"},
    {443, 0, "mp4.minigame.mario_speedwagons", "Mario Speedwagons"},
    {444, 5, "mp4.minigame.reversal_of_fortune", "Reversal of Fortune"},
    {445, 6, "mp4.minigame.bowser_bop", "Bowser Bop"},
    {446, 6, "mp4.minigame.mystic_match_em", "Mystic Match 'Em"},
    {447, 6, "mp4.minigame.archaeologuess", "Archaeologuess"},
    {448, 6, "mp4.minigame.goombas_chip_flip", "Goomba's Chip Flip"},
    {449, 6, "mp4.minigame.kareening_koopa", "Kareening Koopa"},
    {450, 8, "mp4.minigame.the_final_battle", "The Final Battle!"},
    {451, 7, "mp4.minigame.jigsaw_jitters", "Jigsaw Jitters"},
    {453, 7, "mp4.minigame.challenge_booksquirm", "Challenge Booksquirm"},
    {455, 4, "mp4.minigame.rumble_fishing", "Rumble Fishing"},
    {456, 0, "mp4.minigame.take_a_breather", "Take a Breather"},
    {457, 8, "mp4.minigame.bowser_wrestling", "Bowser Wrestling"},
    {458, 8, "mp4.minigame.panels_of_doom", "Panels of Doom"},
    {459, 7, "mp4.minigame.mushroom_medic", "Mushroom Medic"},
    {460, 7, "mp4.minigame.doors_of_doom", "Doors of Doom"},
    {461, 7, "mp4.minigame.bob_omb_x_ing", "Bob-omb X-ing"},
    {462, 7, "mp4.minigame.goomba_stomp", "Goomba Stomp"},
    {463, 7, "mp4.minigame.panel_panic", "Panel Panic"},
}};

// GWPlayerCfg.character: the game's character order (CHARNO_*, 0-7).
const std::array<CharacterEntry, 8> kCharacters = {{
    {0, "mp4.character.mario", "Mario"},
    {1, "mp4.character.luigi", "Luigi"},
    {2, "mp4.character.peach", "Peach"},
    {3, "mp4.character.yoshi", "Yoshi"},
    {4, "mp4.character.wario", "Wario"},
    {5, "mp4.character.donkey_kong", "Donkey Kong"},
    {6, "mp4.character.daisy", "Daisy"},
    {7, "mp4.character.waluigi", "Waluigi"},
}};

namespace {

ContentId id_of(const char* text) {
  std::optional<ContentId> id = ContentId::parse(text);
  return *id;  // the tables hold valid IDs (the unit test parses every one)
}

}  // namespace

Catalog mp4_catalog() {
  Catalog c;
  for (const BoardEntry& b : kBoards) c.boards.push_back({id_of(b.id), b.name});
  for (const MinigameEntry& m : kMinigames) c.minigames.push_back({id_of(m.id), m.name});
  for (const CharacterEntry& ch : kCharacters) c.characters.push_back({id_of(ch.id), ch.name});
  return c;
}

std::optional<BoardId> board_for(std::int32_t gw_board) {
  for (const BoardEntry& b : kBoards)
    if (b.value == gw_board) return id_of(b.id);
  return std::nullopt;
}

std::optional<CharacterId> character_for(std::int32_t character) {
  for (const CharacterEntry& ch : kCharacters)
    if (ch.value == character) return id_of(ch.id);
  return std::nullopt;
}

std::optional<MinigameId> minigame_for_overlay(const char* overlay_name) {
  if (overlay_name == nullptr) return std::nullopt;
  const std::string n = overlay_name;
  auto digit = [](char c) { return c >= '0' && c <= '9'; };
  if (n.size() != 7 || (n[0] != 'm' && n[0] != 'M') || !digit(n[1]) || !digit(n[2]) || !digit(n[3])) return std::nullopt;
  std::string tail = n.substr(4);
  for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (tail != "dll") return std::nullopt;
  const std::int32_t number = (n[1] - '0') * 100 + (n[2] - '0') * 10 + (n[3] - '0');
  for (const MinigameEntry& m : kMinigames)
    if (m.number == number) return id_of(m.id);
  return std::nullopt;
}

}  // namespace mpt::mp4
