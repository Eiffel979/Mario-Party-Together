// P1-17: the dump lane's full-party rules (dump_lane.h, check_full_party) on synthetic parties. A
// PartyDirector watches a made-up snapshot sequence, as Session does on a real one, and the rules must
// accept a whole party and reject each way one can go wrong. No game data: the catalog is synthetic.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../dump_lane/dump_lane.h"

using namespace mpt;

namespace {

ContentId id(const char* text) { return *ContentId::parse(text); }

Catalog synthetic_catalog() {
  Catalog c;
  c.boards = {{id("mp4.board.test_board"), "Test Board"}, {id("mp4.board.other_board"), "Other Board"}};
  c.minigames = {{id("mp4.minigame.test_a"), "Test A"},
                 {id("mp4.minigame.test_b"), "Test B"},
                 {id("mp4.minigame.test_extra"), "Test Extra"}};
  c.characters = {{id("mp4.character.test_1"), "One"}, {id("mp4.character.test_2"), "Two"},
                  {id("mp4.character.test_3"), "Three"}, {id("mp4.character.test_4"), "Four"}};
  return c;
}

dump_lane::FullPartyExpectation expectation(std::int32_t turns) {
  dump_lane::FullPartyExpectation e;
  e.title = TitleId::mp4;
  e.max_turns = turns;
  e.board = id("mp4.board.test_board");
  e.party_eligible = [](const MinigameId& m) { return m.str() != "mp4.minigame.test_extra"; };
  return e;
}

// A party's snapshots, one per "frame": menus, setup, `turns` board turns each ending in a minigame,
// then results and finished.
struct PartyFilm {
  std::vector<PartySnapshot> frames;
  PartySnapshot base;

  explicit PartyFilm(std::int32_t turns) {
    const char* const chars[] = {"mp4.character.test_1", "mp4.character.test_2", "mp4.character.test_3",
                                 "mp4.character.test_4"};
    for (std::size_t i = 0; i < kMaxPlayers; ++i) {
      base.players[i].character = id(chars[i]);
      base.players[i].is_cpu = i != 0;
    }
    frames.push_back(PartySnapshot{});  // the title screen
    PartySnapshot s = base;
    s.phase = PartyPhase::setup;
    frames.push_back(s);
    s.phase = PartyPhase::board;
    s.board = id("mp4.board.test_board");
    s.max_turns = turns;
    for (std::int32_t t = 1; t <= turns; ++t) {
      s.phase = PartyPhase::board;
      s.current_minigame.reset();
      s.turn = t;
      frames.push_back(s);
      s.phase = PartyPhase::minigame;
      s.current_minigame = id(t % 2 == 0 ? "mp4.minigame.test_b" : "mp4.minigame.test_a");
      frames.push_back(s);
      s.phase = PartyPhase::board;
      s.current_minigame.reset();
      s.players[0].coins += 10;
      frames.push_back(s);
    }
    s.phase = PartyPhase::results;
    frames.push_back(s);
    s.phase = PartyPhase::finished;
    s.players[0].stars = 2;
    frames.push_back(s);
  }

  std::vector<std::string> check(const dump_lane::FullPartyExpectation& e) const {
    PartyDirector director;
    director.resync(0, PartySnapshot{});
    for (std::size_t f = 0; f < frames.size(); ++f) director.observe(f * 100 + 100, frames[f]);
    return dump_lane::check_full_party(director.events(), director.party(), synthetic_catalog(), e);
  }
};

bool mentions(const std::vector<std::string>& problems, const std::string& text) {
  for (const std::string& p : problems)
    if (p.find(text) != std::string::npos) return true;
  return false;
}

// Applies `edit` to the frames of a 3-turn party and returns the problems the rules find.
std::vector<std::string> mutated(const std::function<void(std::vector<PartySnapshot>&)>& edit) {
  PartyFilm film(3);
  edit(film.frames);
  return film.check(expectation(3));
}

// The frame index of the first snapshot in `phase` on `turn`.
std::size_t frame_of(const std::vector<PartySnapshot>& frames, PartyPhase phase, std::int32_t turn) {
  for (std::size_t i = 0; i < frames.size(); ++i)
    if (frames[i].phase == phase && frames[i].turn == turn) return i;
  FAIL_CHECK("no such frame");
  return 0;
}

}  // namespace

TEST_CASE("check_full_party accepts a whole party") {
  const PartyFilm film(20);
  const std::vector<std::string> problems = film.check(expectation(20));
  CHECK(problems.empty());
  for (const std::string& p : problems) MESSAGE(p);
}

TEST_CASE("check_full_party knows the phase steps of a party") {
  CHECK(dump_lane::party_phase_step_allowed(PartyPhase::none, PartyPhase::setup));
  CHECK(dump_lane::party_phase_step_allowed(PartyPhase::board, PartyPhase::minigame));
  CHECK(dump_lane::party_phase_step_allowed(PartyPhase::minigame, PartyPhase::board));
  CHECK(dump_lane::party_phase_step_allowed(PartyPhase::board, PartyPhase::results));
  CHECK(dump_lane::party_phase_step_allowed(PartyPhase::results, PartyPhase::finished));
  CHECK_FALSE(dump_lane::party_phase_step_allowed(PartyPhase::results, PartyPhase::board));
  CHECK_FALSE(dump_lane::party_phase_step_allowed(PartyPhase::finished, PartyPhase::board));
  CHECK_FALSE(dump_lane::party_phase_step_allowed(PartyPhase::finished, PartyPhase::none));
  CHECK_FALSE(dump_lane::party_phase_step_allowed(PartyPhase::minigame, PartyPhase::results));
  CHECK_FALSE(dump_lane::party_phase_step_allowed(PartyPhase::none, PartyPhase::board));
}

TEST_CASE("check_full_party rejects each way a party can go wrong") {
  SUBCASE("back to the board after the results") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) {
      PartySnapshot back = f.back();
      back.phase = PartyPhase::board;
      f.push_back(back);
    });
    CHECK(mentions(p, "finished -> board"));
    CHECK(mentions(p, "the events end in phase board"));
  }
  SUBCASE("a turn skipped") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) {
      for (PartySnapshot& s : f)
        if (s.turn >= 2) s.turn += 1;
      for (PartySnapshot& s : f) s.turn = std::min(s.turn, 3);
    });
    CHECK(mentions(p, "turn 3 started after turn 1"));
  }
  SUBCASE("the party stops before its last turn") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) { f.resize(frame_of(f, PartyPhase::board, 3)); });
    CHECK(mentions(p, "the last turn started was 2, expected 3"));
    CHECK(mentions(p, "final: phase board"));
  }
  SUBCASE("a minigame outside the party-eligible list") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) {
      f[frame_of(f, PartyPhase::minigame, 2)].current_minigame = id("mp4.minigame.test_extra");
    });
    CHECK(mentions(p, "mp4.minigame.test_extra is not party-eligible"));
  }
  SUBCASE("a minigame the catalog does not list") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) {
      f[frame_of(f, PartyPhase::minigame, 1)].current_minigame = id("mp4.minigame.test_missing");
    });
    CHECK(mentions(p, "test_missing"));
  }
  SUBCASE("another title's minigame") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) {
      f[frame_of(f, PartyPhase::minigame, 1)].current_minigame = id("mp5.minigame.test_a");
    });
    CHECK(mentions(p, "mp5.minigame.test_a is not a minigame of this title"));
  }
  SUBCASE("a minigame straight into the results") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) { f.erase(f.begin() + static_cast<std::ptrdiff_t>(f.size()) - 3); });
    CHECK(mentions(p, "minigame -> results"));
  }
  SUBCASE("fewer minigames than turns") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) {
      const std::size_t m = frame_of(f, PartyPhase::minigame, 2);
      f.erase(f.begin() + static_cast<std::ptrdiff_t>(m));
    });
    CHECK(mentions(p, "2 minigames for 3 turns"));
  }
  SUBCASE("one turn without a minigame while another has two") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) {
      const std::size_t m = frame_of(f, PartyPhase::minigame, 2);
      f.erase(f.begin() + static_cast<std::ptrdiff_t>(m));
      // Turn 3 gains a second minigame, so the total still equals the turn count.
      const std::size_t after = frame_of(f, PartyPhase::minigame, 3) + 1;
      PartySnapshot extra = f[after - 1];
      PartySnapshot back = f[after];
      f.insert(f.begin() + static_cast<std::ptrdiff_t>(after) + 1, extra);
      f.insert(f.begin() + static_cast<std::ptrdiff_t>(after) + 2, back);
    });
    CHECK(mentions(p, "turn 2 has no minigame"));
    CHECK_FALSE(mentions(p, "minigames for 3 turns"));
  }
  SUBCASE("a turn going backwards") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) {
      for (std::size_t i = frame_of(f, PartyPhase::board, 3); i < f.size(); ++i) f[i].turn = 1;
    });
    CHECK(mentions(p, "turn 1 started after turn 2"));
  }
  SUBCASE("a negative final coin count") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) { f.back().players[1].coins = -5; });
    CHECK(mentions(p, "final: player 2 has negative coins or stars"));
  }
  SUBCASE("a negative final star count") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) { f.back().players[3].stars = -1; });
    CHECK(mentions(p, "final: player 4 has negative coins or stars"));
  }
  SUBCASE("player 1 as a CPU") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) {
      for (PartySnapshot& s : f) s.players[0].is_cpu = true;
    });
    CHECK(mentions(p, "final: player 1 is a CPU"));
  }
  SUBCASE("another board") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) {
      for (PartySnapshot& s : f)
        if (s.board) s.board = id("mp4.board.other_board");
    });
    CHECK(mentions(p, "expected mp4.board.test_board"));
    CHECK(mentions(p, "final: not on mp4.board.test_board"));
  }
  SUBCASE("another turn count") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) {
      for (PartySnapshot& s : f)
        if (s.max_turns != 0) s.max_turns = 4;
    });
    CHECK(mentions(p, "max_turns 4, expected 3"));
  }
  SUBCASE("the wrong seats") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) {
      for (PartySnapshot& s : f) s.players[2].is_cpu = false;
    });
    CHECK(mentions(p, "final: player 3 is human"));
  }
  SUBCASE("a player without a character") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) { f.back().players[3].character.reset(); });
    CHECK(mentions(p, "final: player 4 has no character"));
  }
  SUBCASE("current_minigame outside the minigame phase") {
    const auto p = mutated([](std::vector<PartySnapshot>& f) {
      f[frame_of(f, PartyPhase::board, 2)].current_minigame = id("mp4.minigame.test_a");
    });
    CHECK(mentions(p, "current_minigame is set outside the minigame phase"));
  }
  SUBCASE("no events at all") {
    PartyDirector director;
    const auto p = dump_lane::check_full_party(director.events(), director.party(), synthetic_catalog(), expectation(3));
    CHECK(mentions(p, "the events end in phase none"));
    CHECK(mentions(p, "the last turn started was 0, expected 3"));
  }
}
