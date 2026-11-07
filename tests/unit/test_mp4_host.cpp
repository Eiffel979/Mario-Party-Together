// MP4's host side without a dump or the game module (qa follow-up to #49, so public CI covers it):
// Mp4Adapter's phase mapping and ruleset check on synthetic MptMp4Party values, an unbooted
// Mp4Runtime, and the host's ABI v1.1 check (mpt_mp4_api_has_v1_1) on synthetic function tables.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "mpt/core/content_id.h"
#include "mpt/core/ruleset.h"
#include "mpt/mp4/adapter.h"
#include "mpt/mp4/game_abi.h"
#include "mpt/mp4/runtime.h"
#include "mpt_mp4_party_flags.h"
#include "../support/adapter_checks.h"

namespace {

constexpr std::uint32_t kMode = MPT_MP4_PARTY_MODE;
constexpr std::uint32_t kSetup = MPT_MP4_PARTY_IN_SETUP;
constexpr std::uint32_t kInProgress = MPT_MP4_PARTY_IN_PROGRESS;
constexpr std::uint32_t kTurnsOver = MPT_MP4_PARTY_TURNS_OVER;
constexpr std::uint32_t kFinal = MPT_MP4_PARTY_RESULTS_FINAL;

// A party read as the module reports it: the flags and the running overlay's name.
MptMp4Party party(std::uint32_t flags, const char* overlay = "") {
  MptMp4Party p{};
  p.struct_size = sizeof p;
  p.overlay = overlay[0] != 0 ? 0 : -1;
  std::memcpy(p.overlay_name, overlay, std::min(std::strlen(overlay), sizeof p.overlay_name - 1));
  p.flags = flags;
  return p;
}

MptMp4StepResult fakeStatus(const MptMp4Game*) { return MPT_MP4_STEP_RUNNING; }

// An unbooted runtime for catalog() calls (the catalog needs no game).
mpt::mp4::Mp4Runtime& dummy_runtime() {
  static mpt::mp4::Mp4Runtime runtime;
  return runtime;
}

}  // namespace

TEST_CASE("Mp4Adapter phase_of maps the module's party flags onto PartyPhase") {
  using mpt::PartyPhase;
  using mpt::mp4::phase_of;
  // Outside party mode (boot, title, story, minigame modes) there is no party.
  CHECK(phase_of(party(0)) == PartyPhase::none);
  CHECK(phase_of(party(kInProgress, "w01Dll")) == PartyPhase::none);
  CHECK(phase_of(party(kMode)) == PartyPhase::none);
  CHECK(phase_of(party(kMode | kSetup, "mstoryDll")) == PartyPhase::setup);
  CHECK(phase_of(party(kMode | kInProgress, "w01Dll")) == PartyPhase::board);
  // The minigame overlays are m401Dll..m463Dll; the m3xx overlays are empty placeholders. phase_of is
  // the raw classifier: readParty must still pair the minigame phase with current_minigame (ADR 0005).
  CHECK(phase_of(party(kMode | kInProgress, "m401Dll")) == PartyPhase::minigame);
  CHECK(phase_of(party(kMode | kInProgress, "m463Dll")) == PartyPhase::minigame);
  CHECK(phase_of(party(kMode | kInProgress, "m300Dll")) == PartyPhase::board);
  CHECK(phase_of(party(kMode | kInProgress, "m4")) == PartyPhase::board);
  CHECK(phase_of(party(kMode | kInProgress)) == PartyPhase::board);
  CHECK(phase_of(party(kMode | kInProgress | kTurnsOver, "w01Dll")) == PartyPhase::results);
  CHECK(phase_of(party(kMode | kInProgress | kTurnsOver | kFinal)) == PartyPhase::finished);
  // Later stages win over earlier flags that are still set.
  CHECK(phase_of(party(kMode | kSetup | kInProgress, "w01Dll")) == PartyPhase::board);
  CHECK(phase_of(party(kMode | kSetup | kTurnsOver, "m401Dll")) == PartyPhase::results);
  CHECK(phase_of(party(kMode | kFinal, "m401Dll")) == PartyPhase::finished);
}

TEST_CASE("Mp4Adapter on an unbooted Mp4Runtime: no party, the full catalog, only the empty ruleset") {
  mpt::mp4::Mp4Runtime runtime;  // never booted, so no game module is loaded
  mpt::mp4::Mp4Adapter adapter(runtime);
  const mpt::PartySnapshot snap = adapter.readParty();
  CHECK(snap.phase == mpt::PartyPhase::none);
  CHECK_FALSE(snap.current_minigame.has_value());
  // The catalog is static (docs/titles/mp4.md section 12): it needs no booted game.
  const mpt::Catalog catalog = adapter.catalog();
  CHECK(catalog.boards.size() == 8);
  CHECK(catalog.minigames.size() == 61);
  CHECK(catalog.characters.size() == 8);
  CHECK(mpt::test::check_catalog(catalog, mpt::TitleId::mp4).empty());
  CHECK(runtime.stateHash() == 0);
  CHECK(runtime.status().status == mpt::RuntimeStatus::running);

  mpt::ResolvedRuleset empty;
  empty.seed = 42;  // always set by the host; never a rule
  CHECK(adapter.applyRuleset(empty).ok());

  const auto rejects = [&](auto&& set) {
    mpt::ResolvedRuleset rules;
    rules.seed = 42;
    set(rules);
    return !adapter.applyRuleset(rules).ok();
  };
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.board = mpt::ContentId::parse("mp4.board.test_board"); }));
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.turn_count = 20; }));
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.starting_coins = 10; }));
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.star_cost = 20; }));
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.starting_stars = 0; }));  // set, even to zero
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.bonus_stars = false; }));
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.minigame_time_limit_frames = 3600; }));
  CHECK(rejects([](mpt::ResolvedRuleset& r) {
    r.minigame_pool.push_back(*mpt::ContentId::parse("mp4.minigame.test_minigame"));
  }));
}

TEST_CASE("mpt_mp4_api_has_v1_1 accepts only a v1.1 table with status") {
  MptMp4Api api{};
  api.abi_version = MPT_MP4_ABI_VERSION;
  api.struct_size = MPT_MP4_API_SIZE_V1_1;
  api.status = &fakeStatus;
  CHECK(mpt_mp4_api_has_v1_1(&api));
  api.struct_size = MPT_MP4_API_SIZE_V1_2;  // a later minor version still has it
  CHECK(mpt_mp4_api_has_v1_1(&api));

  // A v1.0 table ends before status: whatever lies past struct_size must not count.
  api.struct_size = MPT_MP4_API_SIZE_V1_0;
  CHECK_FALSE(mpt_mp4_api_has_v1_1(&api));
  api.struct_size = MPT_MP4_API_SIZE_V1_1 - 1;
  CHECK_FALSE(mpt_mp4_api_has_v1_1(&api));
  api.struct_size = MPT_MP4_API_SIZE_V1_1;
  api.status = nullptr;  // entries added in a minor version may be NULL
  CHECK_FALSE(mpt_mp4_api_has_v1_1(&api));
  api.status = &fakeStatus;
  api.abi_version = MPT_MP4_ABI_VERSION + 1;  // another major version
  CHECK_FALSE(mpt_mp4_api_has_v1_1(&api));
  CHECK_FALSE(mpt_mp4_api_has_v1_1(nullptr));

  // The appends sit where the size constants say (ABI v1.1 to v1.4).
  CHECK(offsetof(MptMp4Api, status) == MPT_MP4_API_SIZE_V1_0);
  CHECK(offsetof(MptMp4Api, debug_perturb) == MPT_MP4_API_SIZE_V1_1);
  CHECK(offsetof(MptMp4Api, debug_deep_hash) == MPT_MP4_API_SIZE_V1_2);
  CHECK(offsetof(MptMp4Api, memory_use) == MPT_MP4_API_SIZE_V1_3);
  CHECK(sizeof(MptMp4Api) == MPT_MP4_API_SIZE_V1_4);
}

// P1-17: content IDs for the game's values, and readParty's whole snapshot from a raw read.
TEST_CASE("Mp4Adapter maps boards, characters and minigame overlays to catalogued content IDs") {
  using namespace mpt::mp4;
  CHECK(board_for(0)->str() == "mp4.board.toads_midway_madness");
  CHECK(board_for(5)->str() == "mp4.board.bowsers_gnarly_party");
  CHECK_FALSE(board_for(6).has_value());  // the tutorial board
  CHECK(board_for(7)->str() == "mp4.board.mega_board_mayhem");
  CHECK(board_for(8)->str() == "mp4.board.mini_board_mad_dash");
  CHECK_FALSE(board_for(9).has_value());
  CHECK(character_for(0)->str() == "mp4.character.mario");
  CHECK(character_for(5)->str() == "mp4.character.donkey_kong");
  CHECK(character_for(7)->str() == "mp4.character.waluigi");
  CHECK_FALSE(character_for(-1).has_value());
  CHECK_FALSE(character_for(8).has_value());
  CHECK(minigame_for_overlay("m401Dll")->str() == "mp4.minigame.manta_rings");
  CHECK(minigame_for_overlay("m407dll")->str() == "mp4.minigame.domination");  // lowercase in the table
  CHECK(minigame_for_overlay("m463Dll")->str() == "mp4.minigame.panel_panic");
  CHECK_FALSE(minigame_for_overlay("m452Dll").has_value());  // points back at m450, no row
  CHECK_FALSE(minigame_for_overlay("m300Dll").has_value());
  CHECK_FALSE(minigame_for_overlay("w01Dll").has_value());
  CHECK_FALSE(minigame_for_overlay("m401").has_value());
  CHECK_FALSE(minigame_for_overlay(nullptr).has_value());
}

TEST_CASE("Mp4Adapter snapshot_of pairs the minigame phase with current_minigame (ADR 0005)") {
  using namespace mpt::mp4;
  const mpt::Catalog catalog = mpt::mp4::Mp4Adapter(dummy_runtime()).catalog();
  auto with = [](MptMp4Party p) {
    p.board = 2;
    p.turn = 3;
    p.max_turns = 20;
    for (int i = 0; i < 4; ++i) p.players[i] = {i, i > 0, 10 * i, i, 5 + i};
    return p;
  };
  const mpt::PartySnapshot board = snapshot_of(with(party(kMode | kInProgress, "w03Dll")));
  CHECK(board.phase == mpt::PartyPhase::board);
  CHECK(board.board->str() == "mp4.board.shy_guys_jungle_jam");
  CHECK(board.players[1].character->str() == "mp4.character.luigi");
  CHECK(board.players[1].is_cpu);
  CHECK(board.players[3].coins == 30);
  CHECK(board.turn == 3);
  CHECK(mpt::test::check_snapshot(board, catalog).empty());

  const mpt::PartySnapshot mg = snapshot_of(with(party(kMode | kInProgress, "m425Dll")));
  CHECK(mg.phase == mpt::PartyPhase::minigame);
  CHECK(mg.current_minigame->str() == "mp4.minigame.the_great_deflate");
  CHECK(mpt::test::check_snapshot(mg, catalog).empty());

  // An m4NN overlay the catalog does not list stays in the board phase.
  const mpt::PartySnapshot odd = snapshot_of(with(party(kMode | kInProgress, "m452Dll")));
  CHECK(odd.phase == mpt::PartyPhase::board);
  CHECK_FALSE(odd.current_minigame.has_value());

  // Setup: no board yet, even though GWSystem.board holds a value.
  const mpt::PartySnapshot setup = snapshot_of(with(party(kMode | kSetup, "mentDll")));
  CHECK(setup.phase == mpt::PartyPhase::setup);
  CHECK_FALSE(setup.board.has_value());

  // After the last turn the game's turn is max_turn + 1; the snapshot keeps it within max_turns.
  MptMp4Party over = with(party(kMode | kInProgress | kTurnsOver, "w03Dll"));
  over.turn = 21;
  const mpt::PartySnapshot results = snapshot_of(over);
  CHECK(results.phase == mpt::PartyPhase::results);
  CHECK(results.turn == 20);
  CHECK(results.board.has_value());
  CHECK(mpt::test::check_snapshot(results, catalog).empty());

  // No party: an empty snapshot.
  CHECK(snapshot_of(with(party(0))) == mpt::PartySnapshot{});
}

// M4-01: the end of a 20-turn party as the dump lane saw it (seed 1, Toad's Midway Madness; frames in
// docs/titles/mp4.md section 21). The game never moves GWSystem.turn past max_turn on a party board:
// the board comes back from the last minigame, kills itself and returns to mstory3Dll.
TEST_CASE("mpt_mp4_party_flags: the end of a party board's last turn") {
  auto facts = [](std::int32_t turn, bool board, MptMp4Heading heading, bool setup = false) {
    MptMp4PartyFacts f{};
    f.party = 1;
    f.turn = turn;
    f.max_turn = 20;
    f.setup_running = setup;
    f.board_in_history = board;
    f.heading = heading;
    return f;
  };
  auto flags = [](const MptMp4PartyFacts& f) { return mpt_mp4_party_flags(&f); };
  // Setup, after BoardSaveInit: turn 1/20 in mentDll.
  CHECK(flags(facts(1, false, MPT_MP4_HEADING_SETUP, true)) == (kMode | kSetup));
  // The last turn on the board and in its minigame: the board is in the history.
  CHECK(flags(facts(20, true, MPT_MP4_HEADING_OTHER)) == (kMode | kInProgress));
  // The board has returned (omcurovl is DLL_NONE, heading to mstory3Dll event 0): turns over.
  CHECK(flags(facts(20, false, MPT_MP4_HEADING_BONUS_STARS)) == (kMode | kTurnsOver));
  // The final ranking (mstory3Dll event 1), and whatever follows it: results final.
  CHECK(flags(facts(20, false, MPT_MP4_HEADING_FINAL_RESULTS)) == (kMode | kTurnsOver | kFinal));
  CHECK(flags(facts(20, false, MPT_MP4_HEADING_OTHER)) == (kMode | kTurnsOver | kFinal));
  // A new party's setup in mentDll, before BoardSaveInit resets the turn: setup, not finished.
  CHECK(flags(facts(20, false, MPT_MP4_HEADING_SETUP, true)) == (kMode | kSetup));
  // Turns 1 to 19 off the board (minigame results, the instruction screen) are never over.
  CHECK(flags(facts(19, false, MPT_MP4_HEADING_OTHER)) == kMode);
  // The Extra Room boards move the turn past max_turn on the board (BoardTurnNext).
  CHECK(flags(facts(21, true, MPT_MP4_HEADING_OTHER)) == (kMode | kInProgress | kTurnsOver));
  CHECK(flags(facts(21, false, MPT_MP4_HEADING_OTHER)) == (kMode | kTurnsOver | kFinal));
  // Not a party (story mode) or no turn count yet: no end flags.
  MptMp4PartyFacts story = facts(20, false, MPT_MP4_HEADING_FINAL_RESULTS);
  story.party = 0;
  CHECK(flags(story) == 0u);
  MptMp4PartyFacts unset = facts(0, false, MPT_MP4_HEADING_OTHER);
  unset.max_turn = 0;
  CHECK(flags(unset) == kMode);
}

TEST_CASE("Mp4Adapter snapshot_of on the raw end-of-party sequence") {
  using namespace mpt::mp4;
  const mpt::Catalog catalog = mpt::mp4::Mp4Adapter(dummy_runtime()).catalog();
  // The raw reads in frame order: (flags, overlay), all at turn 20/20 on board 0.
  struct Step {
    std::uint32_t flags;
    const char* overlay;
    mpt::PartyPhase phase;
  };
  const Step steps[] = {
      {kMode | kInProgress, "m428Dll", mpt::PartyPhase::minigame},  // the last minigame
      {kMode | kInProgress, "resultDll", mpt::PartyPhase::board},   // its results
      {kMode | kInProgress, "w01Dll", mpt::PartyPhase::board},      // the board, three frames
      {kMode | kTurnsOver, "", mpt::PartyPhase::results},           // between overlays
      {kMode | kTurnsOver, "mstory3Dll", mpt::PartyPhase::results}, // bonus stars
      {kMode | kTurnsOver | kFinal, "", mpt::PartyPhase::finished}, // heading to event 1
      {kMode | kTurnsOver | kFinal, "mstory3Dll", mpt::PartyPhase::finished},  // the final ranking
  };
  for (const Step& s : steps) {
    MptMp4Party raw = party(s.flags, s.overlay);
    raw.board = 0;
    raw.turn = 20;
    raw.max_turns = 20;
    for (int i = 0; i < 4; ++i) raw.players[i] = {i, i > 0, 10 + i, i, 3 * i};
    const mpt::PartySnapshot snap = snapshot_of(raw);
    INFO(s.overlay);
    CHECK(snap.phase == s.phase);
    CHECK(snap.turn == 20);
    CHECK(snap.max_turns == 20);
    CHECK(snap.board->str() == "mp4.board.toads_midway_madness");
    CHECK(snap.current_minigame.has_value() == (s.phase == mpt::PartyPhase::minigame));
    CHECK(mpt::test::check_snapshot(snap, catalog).empty());
  }
}
