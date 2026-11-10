// The dump lane's verification path (P1-20d), proven with FakeRuntime and FakeAdapter in
// place of a real title: record a party, encode it as a fixture with its sidecar, then
// verify it the way mpt_dump_lane does for real dumps.

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "../dump_lane/dump_lane.h"
#include "../support/fake_adapter.h"
#include "../support/fake_party_script.h"
#include "../support/fake_runtime.h"
#include "../support/temp_dir.h"

using namespace mpt;

namespace {

constexpr std::uint64_t kFrames = 600;

TitleRegistry fake_registry() {
  TitleRegistry registry;
  REQUIRE(registry
              .add(TitleId::mp4, []() -> std::unique_ptr<ITitleRuntime> { return std::make_unique<test::FakeRuntime>(); },
                   test::FakeAdapter::create)
              .ok());
  return registry;
}

VerifiedDump fake_dump() {
  VerifiedDump dump;
  dump.title = TitleId::mp4;
  dump.region = "USA";
  dump.revision = "synthetic";
  dump.sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  return dump;
}

struct Fixture {
  std::vector<std::byte> bytes;
  std::string sidecar;
};

// Records a FakeRuntime party exactly as the shell will: Session plus ReplayRecorder.
Fixture record_fixture(const TitleRegistry& registry, const std::optional<SaveMedia>& media, bool embed) {
  SessionConfig config;
  config.dump = fake_dump();
  config.runtime.seed = 4242;
  config.runtime.save_media = media;
  auto header = make_replay_header(config, embed, 30);
  REQUIRE(std::holds_alternative<ReplayHeader>(header));
  ReplayRecorder recorder(std::get<ReplayHeader>(header));
  auto started = Session::start(registry, config);
  REQUIRE(std::holds_alternative<std::unique_ptr<Session>>(started));
  Session& session = *std::get<std::unique_ptr<Session>>(started);
  for (std::uint64_t f = 0; f < kFrames; ++f) {
    const InputFrame in = test::fake_party_script(f);
    REQUIRE(session.step(in).ok());
    REQUIRE(recorder.record(in, session).ok());
  }
  const Replay replay = recorder.finish();
  return Fixture{encode_replay(replay), test::format_trail(dump_lane::state_trail(replay.checkpoints))};
}

}  // namespace

TEST_CASE("dump lane: a recorded FakeRuntime fixture verifies") {
  const TitleRegistry registry = fake_registry();
  const Fixture fixture = record_fixture(registry, std::nullopt, false);
  CHECK(validate_replay_fixture(fixture.bytes).ok());
  const Result result = dump_lane::verify_fixture(fixture.bytes, fixture.sidecar, registry, fake_dump());
  CHECK_MESSAGE(result.ok(), result.error());
}

TEST_CASE("dump lane: a non-blank save image is supplied by hash, never embedded") {
  const TitleRegistry registry = fake_registry();
  const SaveMedia card = test::FakeRuntime::make_save_media(9);
  const Fixture fixture = record_fixture(registry, card, false);
  CHECK_FALSE(dump_lane::verify_fixture(fixture.bytes, fixture.sidecar, registry, fake_dump()).ok());  // no image
  const Result result = dump_lane::verify_fixture(fixture.bytes, fixture.sidecar, registry, fake_dump(), card);
  CHECK_MESSAGE(result.ok(), result.error());

  const Fixture embedded = record_fixture(registry, card, true);
  const Result rejected = dump_lane::verify_fixture(embedded.bytes, embedded.sidecar, registry, fake_dump());
  CHECK(rejected.error().find("fixture rule") != std::string::npos);
}

TEST_CASE("dump lane: a sidecar that does not match the replay is rejected") {
  const TitleRegistry registry = fake_registry();
  const Fixture fixture = record_fixture(registry, std::nullopt, false);
  std::string tampered = fixture.sidecar;
  const std::size_t last = tampered.rfind(' ');
  REQUIRE(last != std::string::npos);
  tampered[last + 1] = tampered[last + 1] == '0' ? '1' : '0';
  const Result result = dump_lane::verify_fixture(fixture.bytes, tampered, registry, fake_dump());
  CHECK(result.error().find("sidecar does not match") != std::string::npos);
  CHECK(result.error().find("600") != std::string::npos);
}

TEST_CASE("dump lane: a different game behaviour is caught at the first checkpoint") {
  // Same inputs, different runtime: FakeRuntime played with another seed stands in for
  // a build whose hashes drifted. The fixture's checkpoints come from the original run.
  const TitleRegistry registry = fake_registry();
  const Fixture fixture = record_fixture(registry, std::nullopt, false);
  auto decoded = decode_replay(fixture.bytes);
  REQUIRE(std::holds_alternative<Replay>(decoded));
  Replay drifted = std::get<Replay>(decoded);
  drifted.header.seed += 1;
  const std::vector<std::byte> bytes = encode_replay(drifted);
  const Result result = dump_lane::verify_fixture(bytes, fixture.sidecar, registry, fake_dump());
  CHECK(result.error().find("state hash differs at frame 30") != std::string::npos);
}

TEST_CASE("dump lane: the wrong dump is refused") {
  const TitleRegistry registry = fake_registry();
  const Fixture fixture = record_fixture(registry, std::nullopt, false);
  VerifiedDump other = fake_dump();
  other.sha256[0] = 'f';
  CHECK_FALSE(dump_lane::verify_fixture(fixture.bytes, fixture.sidecar, registry, other).ok());
  CHECK_FALSE(dump_lane::verify_fixture(fixture.bytes, fixture.sidecar, TitleRegistry{}, fake_dump()).ok());
}

TEST_CASE("dump lane: fixtures are found with their sidecars, sorted") {
  const std::filesystem::path dir = test::make_temp_dir("mpt_dump_lane_find_test");
  for (const char* name : {"b.mptr", "a.mptr", "a.trail.txt", "notes.txt"}) std::ofstream(dir / name) << "x";
  const auto found = dump_lane::find_fixtures(dir);
  std::filesystem::remove_all(dir);
  REQUIRE(found.size() == 2);
  CHECK(found[0].name == "a");
  CHECK(found[0].trail.filename() == "a.trail.txt");
  CHECK(found[1].name == "b");
  CHECK(dump_lane::find_fixtures(dir).empty());  // a missing directory is not an error
}

// M4-01: the --party autopilot is a pure function of its inputs, and the watchdog fires only after
// `limit` frames without a party change.
TEST_CASE("dump lane: the party change tracker counts frames since a change and is the watchdog") {
  dump_lane::PartyChangeTracker tracker(100);
  const dump_lane::PartyKey board{PartyPhase::board, "w01Dll", 3};
  tracker.observe(0, board);
  CHECK(tracker.sinceChange() == 0);
  tracker.observe(99, board);
  CHECK(tracker.sinceChange() == 99);
  CHECK_FALSE(tracker.stuck());
  tracker.observe(100, board);
  CHECK(tracker.stuck());
  // Any of phase, overlay or turn resets it.
  tracker.observe(101, {PartyPhase::board, "w01Dll", 4});
  CHECK(tracker.changedAt() == 101);
  CHECK_FALSE(tracker.stuck());
  tracker.observe(102, {PartyPhase::board, "instDll", 4});
  CHECK(tracker.changedAt() == 102);
  tracker.observe(103, {PartyPhase::minigame, "instDll", 4});
  CHECK(tracker.changedAt() == 103);
  CHECK(tracker.key().phase == PartyPhase::minigame);
}

TEST_CASE("dump lane: the party autopilot presses by overlay, phase and frames since a change") {
  const dump_lane::PartyScript script{};
  auto pad = [&](std::uint64_t frame, PartyPhase phase, const char* overlay, std::uint64_t since) {
    return dump_lane::party_autopilot(script, frame, {phase, overlay, 0}, since);
  };
  // Connected on every frame, silent during the boot logos.
  CHECK(pad(0, PartyPhase::none, "bootDll", 0).connected == 1);
  for (std::uint64_t f = 0; f < script.boot_wait; ++f) CHECK(pad(f, PartyPhase::none, "bootDll", f).buttons == 0);
  // START on the title and the instruction screen, A pulses elsewhere.
  CHECK(pad(1000, PartyPhase::none, "bootDll", 60).buttons == kButtonStart);
  CHECK(pad(20000, PartyPhase::board, "instDll", 120).buttons == kButtonStart);
  CHECK(pad(20000, PartyPhase::board, "instDll", 30).buttons == 0);
  CHECK(pad(20000, PartyPhase::board, "w01Dll", 30).buttons == kButtonA);
  CHECK(pad(20000, PartyPhase::board, "w01Dll", 34).buttons == 0);
  CHECK(pad(20000, PartyPhase::board, "w01Dll", 10).buttons == 0);  // not in the first 30 frames
  // mstory3Dll: A on the bonus stars, START once finished (A there opens the records view).
  CHECK(pad(250000, PartyPhase::results, "mstory3Dll", 60).buttons == kButtonA);
  CHECK(pad(250850, PartyPhase::finished, "mstory3Dll", 60).buttons == kButtonStart);
  // A minigame: the stick held down for 40 of every 120 frames, then A mashed.
  CHECK(pad(24000, PartyPhase::minigame, "m444dll", 5).stick_y == -32767);
  CHECK(pad(24000, PartyPhase::minigame, "m444dll", 5).buttons == 0);
  CHECK(pad(24040, PartyPhase::minigame, "m444dll", 45).buttons == kButtonA);
  CHECK(pad(24040, PartyPhase::minigame, "m444dll", 45).stick_y == 0);
  CHECK(pad(24042, PartyPhase::minigame, "m444dll", 47).buttons == 0);
  // The same inputs give the same pad.
  CHECK(pad(31337, PartyPhase::minigame, "m401Dll", 7) == pad(31337, PartyPhase::minigame, "m401Dll", 7));
}

TEST_CASE("dump lane: the party autopilot moves mentDll's board wheel only in its window") {
  CHECK(dump_lane::board_wheel_moves(0) == 0);  // the default
  CHECK(dump_lane::board_wheel_moves(1) == -2);
  CHECK(dump_lane::board_wheel_moves(2) == -1);
  CHECK(dump_lane::board_wheel_moves(3) == 1);
  CHECK(dump_lane::board_wheel_moves(5) == 3);
  dump_lane::PartyScript script{};
  script.board = 3;
  script.wheel_from = 500;
  script.wheel_to = 700;
  auto pad = [&](std::uint64_t since) {
    return dump_lane::party_autopilot(script, 5000 + since, {PartyPhase::none, "mentDll", 0}, since);
  };
  CHECK(pad(480).buttons == kButtonA);  // before the window: the A pulses
  // The stick, not the d-pad: Party Board's HuPadBtn masks the d-pad out, and the wheel reads |x| >= 50.
  CHECK(pad(500).buttons == 0);
  CHECK(pad(500).stick_x == 32767);
  CHECK(pad(504).stick_x == 0);
  CHECK(pad(530).stick_x == 0);  // one move only
  CHECK(pad(690).buttons == 0);  // no A inside the window
  CHECK(pad(720).buttons == kButtonA);
  // A stall: B pulses too, between the A pulses, only after kPartyStallFrames.
  {
    const dump_lane::PartyScript none{};
    auto board = [&](std::uint64_t since) {
      return dump_lane::party_autopilot(none, 90000 + since, {PartyPhase::board, "w01Dll", 3}, since);
    };
    CHECK(board(dump_lane::kPartyStallFrames - 15).buttons == 0);
    CHECK(board(dump_lane::kPartyStallFrames + 45).buttons == kButtonB);
    CHECK(board(dump_lane::kPartyStallFrames + 60).buttons == kButtonA);
    CHECK(board(dump_lane::kPartyStallFrames + 50).buttons == 0);
  }
  script.board = 1;  // two moves left
  CHECK(pad(500).stick_x == -32767);
  CHECK(pad(530).stick_x == 0);
  CHECK(pad(500 + dump_lane::kBoardWheelPulse).stick_x == -32767);
  CHECK(pad(500 + 2 * dump_lane::kBoardWheelPulse).stick_x == 0);
}
