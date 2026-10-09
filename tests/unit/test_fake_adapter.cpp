// FakeAdapter and FakeRuntime party progression (P1-20b2). The invariant checks come
// from adapter_checks.h, which the conformance suite (P1-20c) reuses.

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../support/adapter_checks.h"
#include "../support/fake_adapter.h"
#include "../support/fake_party_script.h"
#include "../support/fake_runtime.h"
#include "mpt/core/title_registry.h"

using namespace mpt;
using test::FakeAdapter;
using test::FakeRuntime;

namespace {

constexpr std::uint64_t kPartyFrames = 600;

// Runs the party script, checking every adapter invariant after every frame, and
// returns the snapshot after each frame.
std::vector<PartySnapshot> run_party(std::uint64_t seed, std::uint64_t frames = kPartyFrames) {
  FakeRuntime runtime;
  RuntimeConfig config;
  config.seed = seed;
  REQUIRE(runtime.boot({}, config).ok());
  FakeAdapter adapter(runtime);
  const Catalog catalog = adapter.catalog();
  std::vector<PartySnapshot> out;
  for (std::uint64_t f = 0; f < frames; ++f) {
    runtime.stepFrame(test::fake_party_script(f));
    const std::string pure = test::check_read_party_is_pure(runtime, adapter);
    const PartySnapshot snapshot = adapter.readParty();
    const std::string valid = test::check_snapshot(snapshot, catalog);
    if (!pure.empty() || !valid.empty()) {
      FAIL("frame " << f << ": " << pure << valid);
    }
    out.push_back(snapshot);
  }
  return out;
}

std::vector<PartyPhase> phase_changes(const std::vector<PartySnapshot>& snapshots) {
  std::vector<PartyPhase> phases{PartyPhase::none};
  for (const PartySnapshot& s : snapshots) {
    if (s.phase != phases.back()) phases.push_back(s.phase);
  }
  return phases;
}

}  // namespace

TEST_CASE("FakeAdapter's catalog is valid and synthetic") {
  FakeRuntime runtime;
  FakeAdapter adapter(runtime);
  const Catalog catalog = adapter.catalog();
  const std::string problem = test::check_catalog(catalog, runtime.id());
  CHECK_MESSAGE(problem.empty(), problem);
  CHECK(catalog.boards.size() == 1);
  CHECK(catalog.minigames.size() == FakeRuntime::kMinigameNames.size());
  CHECK(catalog.characters.size() == kMaxPlayers);
  for (const CatalogEntry& entry : catalog.minigames) CHECK(entry.id.name().starts_with("fake_"));
}

TEST_CASE("FakeAdapter::create only accepts a FakeRuntime") {
  FakeRuntime runtime;
  CHECK(FakeAdapter::create(runtime) != nullptr);
  TitleRegistry registry;
  REQUIRE(registry.add(TitleId::mp4, [] { return std::make_unique<FakeRuntime>(); }, FakeAdapter::create).ok());
  const std::unique_ptr<ITitleRuntime> made = registry.createRuntime(TitleId::mp4);
  REQUIRE(made);
  CHECK(registry.createAdapter(TitleId::mp4, *made) != nullptr);
}

TEST_CASE("before any party, readParty reports no party") {
  FakeRuntime runtime;
  RuntimeConfig config;
  REQUIRE(runtime.boot({}, config).ok());
  FakeAdapter adapter(runtime);
  CHECK(adapter.readParty() == PartySnapshot{});
}

TEST_CASE("the party script walks through every phase in order") {
  const std::vector<PartySnapshot> snapshots = run_party(1234);
  const std::vector<PartyPhase> expected = {PartyPhase::none,     PartyPhase::setup,    PartyPhase::board,
                                                PartyPhase::minigame, PartyPhase::board,    PartyPhase::minigame,
                                                PartyPhase::board,    PartyPhase::minigame, PartyPhase::results,
                                                PartyPhase::finished};
  CHECK(phase_changes(snapshots) == expected);

  // Phases change on the frame of the press, and the tenth press (on finished) is ignored.
  CHECK(snapshots[9].phase == PartyPhase::none);
  CHECK(snapshots[10].phase == PartyPhase::setup);
  CHECK(snapshots[60].phase == PartyPhase::board);
  CHECK(snapshots[110].phase == PartyPhase::minigame);
  CHECK(snapshots[360].phase == PartyPhase::results);
  CHECK(snapshots[410].phase == PartyPhase::finished);
  CHECK(snapshots[460] == snapshots[459]);

  // Turns count up on the board and stay at the last turn afterwards.
  CHECK(snapshots[60].turn == 1);
  CHECK(snapshots[160].turn == 2);
  CHECK(snapshots[260].turn == 3);
  CHECK(snapshots[599].turn == 3);
  CHECK(snapshots[599].max_turns == static_cast<std::int32_t>(FakeRuntime::kMaxTurns));

  // Characters are set once setup ends; coins start, then only grow; a star is awarded.
  CHECK_FALSE(snapshots[10].players[0].character.has_value());
  CHECK(snapshots[60].players[0].character.has_value());
  CHECK(snapshots[60].players[2].coins == FakeRuntime::kStartingCoins);
  std::int32_t stars = 0;
  for (const PlayerState& p : snapshots[599].players) stars += p.stars;
  CHECK(stars >= 1);
}

TEST_CASE("party progression is deterministic and seed dependent") {
  const std::vector<PartySnapshot> a = run_party(77);
  const std::vector<PartySnapshot> b = run_party(77);
  CHECK(a == b);

  bool any_seed_differs = false;
  for (std::uint64_t seed = 1; seed < 20 && !any_seed_differs; ++seed) {
    any_seed_differs = run_party(seed).back() != a.back();
  }
  CHECK(any_seed_differs);
}

TEST_CASE("party state survives saveState and loadState") {
  FakeRuntime first;
  RuntimeConfig config;
  config.seed = 5;
  REQUIRE(first.boot({}, config).ok());
  for (std::uint64_t f = 0; f < 120; ++f) first.stepFrame(test::fake_party_script(f));  // in a minigame
  FakeAdapter first_adapter(first);
  const PartySnapshot at_save = first_adapter.readParty();
  REQUIRE(at_save.phase == PartyPhase::minigame);
  const SaveState snapshot = first.saveState();

  FakeRuntime resumed;
  RuntimeConfig other;
  REQUIRE(resumed.boot({}, other).ok());
  REQUIRE(resumed.loadState(snapshot).ok());
  FakeAdapter resumed_adapter(resumed);
  CHECK(resumed_adapter.readParty() == at_save);
  for (std::uint64_t f = 120; f < kPartyFrames; ++f) {
    first.stepFrame(test::fake_party_script(f));
    resumed.stepFrame(test::fake_party_script(f));
  }
  CHECK(resumed.stateHash() == first.stateHash());
  CHECK(resumed_adapter.readParty() == first_adapter.readParty());
}

TEST_CASE("loadState rejects a party with an inconsistent minigame") {
  FakeRuntime runtime;
  RuntimeConfig config;
  REQUIRE(runtime.boot({}, config).ok());
  for (std::uint64_t f = 0; f < 70; ++f) runtime.stepFrame(test::fake_party_script(f));  // on the board
  SaveState state = runtime.saveState();
  const std::size_t minigame_word = 2 + kMaxPlayers + 5 + 2;  // after frame, rng, scores, v1 words, phase, turn
  state.data[minigame_word * 8] = std::byte{1};
  CHECK_FALSE(runtime.loadState(state).ok());
}

TEST_CASE("Phase 1 adapter calls: empty rulesets only, no writes, no launches") {
  FakeRuntime runtime;
  RuntimeConfig config;
  REQUIRE(runtime.boot({}, config).ok());
  FakeAdapter adapter(runtime);

  ResolvedRuleset empty;
  empty.seed = 42;
  CHECK(adapter.applyRuleset(empty).ok());
  ResolvedRuleset turns;
  turns.turn_count = 10;
  CHECK_FALSE(adapter.applyRuleset(turns).ok());
  ResolvedRuleset pool;
  pool.minigame_pool.push_back(adapter.catalog().minigames.front().id);
  CHECK_FALSE(adapter.applyRuleset(pool).ok());

  const std::uint64_t before = runtime.stateHash();
  PartySnapshot changed;
  changed.turn = 9;
  adapter.writeParty(changed);
  CHECK(runtime.stateHash() == before);
  CHECK(adapter.readParty() == PartySnapshot{});

  CHECK_FALSE(adapter.launchMinigame(adapter.catalog().minigames.front().id, MinigameSetup{}).ok());
  CHECK_FALSE(adapter.pollMinigameResult().has_value());
}
