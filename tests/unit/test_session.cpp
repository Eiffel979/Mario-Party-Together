// Session and Party Director v0 (P1-04, ADR 0005 and its P1-04 amendment), run against
// FakeRuntime + FakeAdapter and the whole-party script from P1-20b2.

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "../support/adapter_checks.h"
#include "../support/fake_adapter.h"
#include "../support/fake_party_script.h"
#include "../support/fake_runtime.h"
#include "mpt/core/party_director.h"
#include "mpt/core/session.h"

using namespace mpt;
using Kind = PartyEvent::Kind;

namespace {

constexpr std::uint64_t kPartyFrames = 500;  // the script's last advance is at frame 460

VerifiedDump fake_dump(TitleId title = TitleId::mp4) {
  VerifiedDump dump;
  dump.title = title;
  dump.region = "USA";
  dump.revision = "1.0";
  dump.sha256 = std::string(64, '0');
  return dump;
}

SessionConfig fake_config(std::uint64_t seed = 7) {
  SessionConfig config;
  config.dump = fake_dump();
  config.runtime.seed = seed;
  return config;
}

TitleRegistry fake_registry() {
  TitleRegistry registry;
  const Result added = registry.add(
      TitleId::mp4, [] { return std::make_unique<test::FakeRuntime>(); }, &test::FakeAdapter::create);
  REQUIRE(added.ok());
  return registry;
}

std::unique_ptr<Session> start_ok(const TitleRegistry& registry, const SessionConfig& config) {
  auto started = Session::start(registry, config);
  if (const Result* failure = std::get_if<Result>(&started)) FAIL(failure->error());
  return std::move(std::get<std::unique_ptr<Session>>(started));
}

std::string start_error(const TitleRegistry& registry, const SessionConfig& config) {
  auto started = Session::start(registry, config);
  const Result* failure = std::get_if<Result>(&started);
  REQUIRE_MESSAGE(failure != nullptr, "Session::start was expected to fail");
  return failure->error();
}

void run_to(Session& session, std::uint64_t end) {
  while (session.frame() < end) REQUIRE(session.step(test::fake_party_script(session.frame())).ok());
}

std::vector<PartyEvent> events_of(const Session& session) {
  const auto events = session.director().events();
  return {events.begin(), events.end()};
}

std::vector<PartyEvent> events_before(const std::vector<PartyEvent>& events, std::uint64_t frame) {
  std::vector<PartyEvent> out;
  for (const PartyEvent& e : events) {
    if (e.frame < frame) out.push_back(e);
  }
  return out;
}

ContentId id(const char* text) { return ContentId::parse(text).value(); }

// Forwards to a FakeRuntime but reports another title, so two titles can be live at once.
class RetitledRuntime final : public ITitleRuntime {
 public:
  explicit RetitledRuntime(TitleId title) : title_(title) {}
  test::FakeRuntime& inner() { return inner_; }

  TitleId id() const override { return title_; }
  Result boot(const VerifiedDump& dump, const RuntimeConfig& config) override { return inner_.boot(dump, config); }
  void stepFrame(const InputFrame& inputs) override { inner_.stepFrame(inputs); }
  FrameOutput video() override { return inner_.video(); }
  AudioChunk audio() override { return inner_.audio(); }
  SaveState saveState() override { return inner_.saveState(); }
  Result loadState(const SaveState& state) override { return inner_.loadState(state); }
  std::uint64_t stateHash() override { return inner_.stateHash(); }
  FrameRate frameRate() const override { return inner_.frameRate(); }
  std::uint64_t saveMediaGeneration() const override { return inner_.saveMediaGeneration(); }
  SaveMedia saveMedia() const override { return inner_.saveMedia(); }

 private:
  TitleId title_;
  test::FakeRuntime inner_;
};

}  // namespace

TEST_CASE("Session::start boots the title and takes the post-boot party as the baseline") {
  const TitleRegistry registry = fake_registry();
  auto session = start_ok(registry, fake_config());
  CHECK(session->frame() == 0);
  CHECK(session->runtime().id() == TitleId::mp4);
  CHECK(session->runtime().frameRate() == test::FakeRuntime::kRate60);
  CHECK(session->director().party() == PartySnapshot{});
  CHECK(session->director().events().empty());
  CHECK_FALSE(session->takeSaveMedia().has_value());
}

TEST_CASE("Session::start reports why it cannot start") {
  const TitleRegistry registry = fake_registry();

  SUBCASE("unregistered title") {
    SessionConfig config = fake_config();
    config.dump.title = TitleId::mp1;
    CHECK(start_error(registry, config).find("not registered") != std::string::npos);
  }
  SUBCASE("runtime reports another title") {
    TitleRegistry mislabelled;
    REQUIRE(mislabelled
                .add(TitleId::mp1, [] { return std::make_unique<test::FakeRuntime>(); }, &test::FakeAdapter::create)
                .ok());
    SessionConfig config = fake_config();
    config.dump.title = TitleId::mp1;
    CHECK(start_error(mislabelled, config).find("reports mp4") != std::string::npos);
  }
  SUBCASE("null runtime") {
    TitleRegistry broken;
    REQUIRE(broken
                .add(TitleId::mp4, []() -> std::unique_ptr<ITitleRuntime> { return nullptr; },
                     &test::FakeAdapter::create)
                .ok());
    CHECK(start_error(broken, fake_config()).find("returned null") != std::string::npos);
  }
  SUBCASE("boot failure") {
    SessionConfig config = fake_config();
    config.runtime.save_media = SaveMedia{99, {}};
    CHECK(start_error(registry, config).find("boot failed") != std::string::npos);
  }
  SUBCASE("rejected ruleset") {
    SessionConfig config = fake_config();
    config.rules = ResolvedRuleset{};
    config.rules->turn_count = 10;
    CHECK(start_error(registry, config).find("ruleset rejected") != std::string::npos);
  }

  // Every failure above released the title again.
  CHECK(start_ok(registry, fake_config()) != nullptr);
}

TEST_CASE("Session::start accepts an empty ruleset") {
  const TitleRegistry registry = fake_registry();
  SessionConfig config = fake_config();
  config.rules = ResolvedRuleset{};
  config.rules->seed = 42;
  CHECK(start_ok(registry, config) != nullptr);
}

TEST_CASE("only one live Session per title (ADR 0002 rule 10)") {
  const TitleRegistry registry = fake_registry();
  auto first = start_ok(registry, fake_config());
  CHECK(start_error(registry, fake_config()).find("already running") != std::string::npos);

  SUBCASE("another title can run alongside") {
    TitleRegistry both = fake_registry();
    REQUIRE(both.add(
                    TitleId::mp1, [] { return std::make_unique<RetitledRuntime>(TitleId::mp1); },
                    [](ITitleRuntime& runtime) -> std::unique_ptr<ITitleAdapter> {
                      return std::make_unique<test::FakeAdapter>(dynamic_cast<RetitledRuntime&>(runtime).inner());
                    })
                .ok());
    SessionConfig config = fake_config();
    config.dump.title = TitleId::mp1;
    auto second = start_ok(both, config);
    CHECK(second->runtime().id() == TitleId::mp1);
  }
  SUBCASE("the title is free again once the session is gone") {
    first.reset();
    CHECK(start_ok(registry, fake_config()) != nullptr);
  }
}

TEST_CASE("Session::step numbers frames and refuses the wrong one") {
  const TitleRegistry registry = fake_registry();
  auto session = start_ok(registry, fake_config());
  const std::uint64_t hash_at_0 = session->runtime().stateHash();

  const Result early = session->step(test::fake_party_script(1));
  CHECK_FALSE(early.ok());
  CHECK(session->frame() == 0);
  CHECK(session->runtime().stateHash() == hash_at_0);

  run_to(*session, 3);
  CHECK(session->frame() == 3);
  CHECK_FALSE(session->step(test::fake_party_script(2)).ok());
  CHECK(session->frame() == 3);
}

TEST_CASE("Session steps exactly like driving the runtime directly") {
  const TitleRegistry registry = fake_registry();
  auto session = start_ok(registry, fake_config());
  test::FakeRuntime direct;
  REQUIRE(direct.boot(fake_dump(), fake_config().runtime).ok());
  for (std::uint64_t f = 0; f < kPartyFrames; ++f) {
    REQUIRE(session->step(test::fake_party_script(f)).ok());
    direct.stepFrame(test::fake_party_script(f));
    REQUIRE(session->runtime().stateHash() == direct.stateHash());
  }
}

TEST_CASE("the Director turns the party script into the expected events") {
  const TitleRegistry registry = fake_registry();
  auto session = start_ok(registry, fake_config());
  run_to(*session, kPartyFrames);

  struct Expected {
    Kind kind;
    std::uint64_t frame;
    PartyPhase phase;  // of the event's snapshot
    std::int32_t turn;
  };
  const std::vector<Expected> expected = {
      {Kind::phase_changed, 10, PartyPhase::setup, 0},
      {Kind::phase_changed, 60, PartyPhase::board, 1},
      {Kind::turn_started, 60, PartyPhase::board, 1},
      {Kind::phase_changed, 110, PartyPhase::minigame, 1},
      {Kind::minigame_started, 110, PartyPhase::minigame, 1},
      {Kind::minigame_ended, 160, PartyPhase::board, 2},
      {Kind::phase_changed, 160, PartyPhase::board, 2},
      {Kind::turn_started, 160, PartyPhase::board, 2},
      {Kind::phase_changed, 210, PartyPhase::minigame, 2},
      {Kind::minigame_started, 210, PartyPhase::minigame, 2},
      {Kind::minigame_ended, 260, PartyPhase::board, 3},
      {Kind::phase_changed, 260, PartyPhase::board, 3},
      {Kind::turn_started, 260, PartyPhase::board, 3},
      {Kind::phase_changed, 310, PartyPhase::minigame, 3},
      {Kind::minigame_started, 310, PartyPhase::minigame, 3},
      {Kind::minigame_ended, 360, PartyPhase::results, 3},
      {Kind::phase_changed, 360, PartyPhase::results, 3},
      {Kind::phase_changed, 410, PartyPhase::finished, 3},
  };

  const std::vector<PartyEvent> events = events_of(*session);
  REQUIRE(events.size() == expected.size());
  const Catalog catalog = test::FakeAdapter(test::FakeRuntime{}).catalog();
  std::optional<MinigameId> running;
  for (std::size_t i = 0; i < events.size(); ++i) {
    CAPTURE(i);
    const PartyEvent& e = events[i];
    CHECK(e.kind == expected[i].kind);
    CHECK(e.frame == expected[i].frame);
    CHECK(e.snapshot.phase == expected[i].phase);
    CHECK(e.snapshot.turn == expected[i].turn);
    const std::string problem = test::check_snapshot(e.snapshot, catalog);
    CHECK_MESSAGE(problem.empty(), problem);
    if (e.kind == Kind::minigame_started) {
      REQUIRE(e.minigame.has_value());
      CHECK(e.minigame == e.snapshot.current_minigame);
      running = e.minigame;
    } else if (e.kind == Kind::minigame_ended) {
      CHECK(e.minigame == running);  // names the minigame that just ended
      CHECK_FALSE(e.snapshot.current_minigame.has_value());
    } else {
      CHECK_FALSE(e.minigame.has_value());
    }
  }

  CHECK(session->director().party().phase == PartyPhase::finished);
  CHECK(session->director().party().players[0].character.has_value());
}

TEST_CASE("Director events are deterministic across sessions") {
  const TitleRegistry registry = fake_registry();
  std::vector<PartyEvent> first;
  std::uint64_t first_hash = 0;
  {
    auto session = start_ok(registry, fake_config(1234));
    run_to(*session, kPartyFrames);
    first = events_of(*session);
    first_hash = session->runtime().stateHash();
  }
  auto session = start_ok(registry, fake_config(1234));
  run_to(*session, kPartyFrames);
  CHECK(events_of(*session) == first);
  CHECK(session->runtime().stateHash() == first_hash);
}

TEST_CASE("save-media writes are detected across step() and handed over once") {
  const TitleRegistry registry = fake_registry();
  auto session = start_ok(registry, fake_config());
  run_to(*session, test::kPartyScriptSaveFrame);
  CHECK_FALSE(session->takeSaveMedia().has_value());

  run_to(*session, test::kPartyScriptSaveFrame + 1);
  const std::optional<SaveMedia> written = session->takeSaveMedia();
  REQUIRE(written.has_value());
  CHECK(*written == test::FakeRuntime::make_save_media(1));
  CHECK_FALSE(session->takeSaveMedia().has_value());

  run_to(*session, kPartyFrames);
  CHECK_FALSE(session->takeSaveMedia().has_value());
}

TEST_CASE("loadState rewinds and resyncs without counting as a write (ADR 0004 rules 9-10)") {
  const TitleRegistry registry = fake_registry();

  auto straight = start_ok(registry, fake_config());
  run_to(*straight, kPartyFrames);
  const std::vector<PartyEvent> all_events = events_of(*straight);
  const std::uint64_t final_hash = straight->runtime().stateHash();
  straight.reset();

  auto session = start_ok(registry, fake_config());
  constexpr std::uint64_t kRewindTo = 250;  // mid-party, before the save at frame 300
  run_to(*session, kRewindTo);
  const SaveState checkpoint = session->runtime().saveState();
  REQUIRE(checkpoint.frame == kRewindTo);

  SUBCASE("rewind after the write was taken") {
    run_to(*session, kPartyFrames);
    REQUIRE(session->takeSaveMedia().has_value());
    REQUIRE(session->runtime().saveMediaGeneration() == 1);

    REQUIRE(session->loadState(checkpoint).ok());
    CHECK(session->frame() == kRewindTo);
    CHECK(session->runtime().saveMediaGeneration() == 0);  // a rewind lowers it
    CHECK_FALSE(session->takeSaveMedia().has_value());      // ... and that is not a write
    CHECK(events_of(*session) == events_before(all_events, kRewindTo));

    // Re-simulating writes again, and that write is detected against the restored baseline.
    run_to(*session, kPartyFrames);
    const std::optional<SaveMedia> rewritten = session->takeSaveMedia();
    REQUIRE(rewritten.has_value());
    CHECK(*rewritten == test::FakeRuntime::make_save_media(1));
    CHECK(events_of(*session) == all_events);
    CHECK(session->runtime().stateHash() == final_hash);
  }

  SUBCASE("a write not yet taken survives the rewind as written") {
    run_to(*session, test::kPartyScriptSaveFrame + 10);
    REQUIRE(session->loadState(checkpoint).ok());
    const std::optional<SaveMedia> pending = session->takeSaveMedia();
    REQUIRE(pending.has_value());
    CHECK(*pending == test::FakeRuntime::make_save_media(1));
  }

  SUBCASE("a failed load changes nothing in the session") {
    run_to(*session, 300);
    const std::vector<PartyEvent> before = events_of(*session);
    SaveState bad = checkpoint;
    bad.format_version = 0;
    CHECK_FALSE(session->loadState(bad).ok());
    CHECK(session->frame() == 300);
    CHECK(events_of(*session) == before);
  }
}

TEST_CASE("a forward load leaves a gap in the events (ADR 0005 amendment 5)") {
  const TitleRegistry registry = fake_registry();
  SaveState late;
  {
    auto source = start_ok(registry, fake_config());
    run_to(*source, 400);  // in results, before finished at 410
    late = source->runtime().saveState();
  }
  auto session = start_ok(registry, fake_config());
  REQUIRE(session->loadState(late).ok());
  CHECK(session->frame() == 400);
  CHECK(session->director().events().empty());
  CHECK(session->director().party().phase == PartyPhase::results);

  run_to(*session, kPartyFrames);
  const std::vector<PartyEvent> events = events_of(*session);
  REQUIRE(events.size() == 1);
  CHECK(events[0].kind == Kind::phase_changed);
  CHECK(events[0].frame == 410);
  CHECK(events[0].snapshot.phase == PartyPhase::finished);
}

TEST_CASE("PartyDirector diffs snapshots in the documented order") {
  PartyDirector director;
  PartySnapshot board;
  board.phase = PartyPhase::board;
  board.turn = 1;
  director.resync(0, board);

  SUBCASE("an unchanged snapshot emits nothing") {
    director.observe(0, board);
    CHECK(director.events().empty());
  }

  SUBCASE("back-to-back minigames: ended(A) then started(B), no phase change") {
    PartySnapshot a = board;
    a.phase = PartyPhase::minigame;
    a.current_minigame = id("mp4.minigame.fake_mash");
    PartySnapshot b = a;
    b.current_minigame = id("mp4.minigame.fake_dodge");
    director.observe(5, a);
    director.observe(6, b);
    const auto events = director.events();
    REQUIRE(events.size() == 4);
    CHECK(events[0].kind == Kind::phase_changed);
    CHECK(events[1].kind == Kind::minigame_started);
    CHECK(events[2].kind == Kind::minigame_ended);
    CHECK(events[2].minigame == a.current_minigame);
    CHECK(events[3].kind == Kind::minigame_started);
    CHECK(events[3].minigame == b.current_minigame);
    CHECK(events[3].frame == 6);
  }

  SUBCASE("other fields change the mirror without an event") {
    PartySnapshot richer = board;
    richer.players[2].coins = 99;
    director.observe(3, richer);
    CHECK(director.events().empty());
    CHECK(director.party() == richer);
  }

  SUBCASE("resync drops the events at and after its frame") {
    PartySnapshot next = board;
    for (std::int32_t turn = 2; turn <= 4; ++turn) {
      next.turn = turn;
      director.observe(static_cast<std::uint64_t>(turn * 10), next);  // frames 20, 30, 40
    }
    REQUIRE(director.events().size() == 3);
    director.resync(30, board);
    REQUIRE(director.events().size() == 1);
    CHECK(director.events()[0].frame == 20);
    CHECK(director.party() == board);
  }
}
