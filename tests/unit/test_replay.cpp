// Replay library (P1-05, ADR 0006 and its P1-05 amendment): format round trips, strict
// parsing of corrupt input, the committed-fixture rule, a cross-platform golden encoding,
// and recording/verifying a FakeRuntime party through Session.

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "../support/fake_adapter.h"
#include "../support/fake_party_script.h"
#include "../support/fake_runtime.h"
#include "mpt/core/replay.h"
#include "mpt/core/session.h"

#ifndef MPT_GOLDEN_DIR
#error "MPT_GOLDEN_DIR must point at tests/golden"
#endif

using namespace mpt;

namespace {

constexpr std::uint64_t kPartyFrames = 500;

Replay decode_ok(std::span<const std::byte> bytes) {
  auto decoded = decode_replay(bytes);
  if (const Result* failure = std::get_if<Result>(&decoded)) FAIL(failure->error());
  return std::get<Replay>(std::move(decoded));
}

std::string decode_error(std::span<const std::byte> bytes) {
  auto decoded = decode_replay(bytes);
  const Result* failure = std::get_if<Result>(&decoded);
  REQUIRE_MESSAGE(failure != nullptr, "decode_replay was expected to fail");
  return failure->error();
}

bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

Sha256Digest counting_digest(std::uint8_t start) {
  Sha256Digest d{};
  for (std::size_t i = 0; i < d.size(); ++i) d[i] = static_cast<std::uint8_t>(start + i);
  return d;
}

// A small replay that exercises every field, built only from constants.
Replay synthetic_replay(bool embed_media) {
  ReplayHeader header;
  header.title = TitleId::mp4;
  header.dump_sha256 = counting_digest(0x10);
  header.seed = 0x0123456789abcdefull;
  if (embed_media) {
    SaveMedia media;
    media.format_version = 1;
    for (std::uint8_t i = 0; i < 8; ++i) media.data.push_back(static_cast<std::byte>(0xa0 + i));
    header.save_media_sha256 = save_media_digest(media);
    header.embedded_save_media = media;
  }
  header.checkpoint_interval = 4;

  ReplayWriter writer(header);
  for (std::uint64_t f = 0; f < 10; ++f) {
    InputFrame in;
    in.frame = f;
    in.pads[0].connected = 1;
    in.pads[1].connected = 1;
    if (f >= 3 && f < 6) in.pads[0].buttons = kButtonA | kButtonStart;
    if (f == 7) {
      PadState& p = in.pads[1];
      p.stick_x = -32767;
      p.stick_y = 12345;
      p.c_stick_x = -2;
      p.c_stick_y = 32767;
      p.trigger_l = 200;
      p.trigger_r = 1;
      p.reserved = 0;
      p.accel = {-1, 2, -3};
      p.gyro = {4, -5, 6};
      p.pointer_x = -32768;
      p.pointer_y = 7;
    }
    REQUIRE(writer.append(in).ok());
    if ((f + 1) % 4 == 0) {
      REQUIRE(writer.addCheckpoint(ReplayCheckpoint{f + 1, 0x1111111111111111ull * (f + 1), 0xfeedface00000000ull + f})
                  .ok());
    }
  }
  return writer.finish();
}

std::string to_hex_lines(const std::vector<std::byte>& bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    const auto b = std::to_integer<unsigned>(bytes[i]);
    out.push_back(kHex[b >> 4]);
    out.push_back(kHex[b & 0xf]);
    out.push_back((i % 32 == 31 || i + 1 == bytes.size()) ? '\n' : ' ');
  }
  return out;
}

std::vector<std::byte> from_hex_lines(const std::string& text) {
  std::vector<std::byte> out;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream words(line);
    std::string word;
    while (words >> word) out.push_back(static_cast<std::byte>(std::stoul(word, nullptr, 16)));
  }
  return out;
}

std::uint64_t read_le(const std::vector<std::byte>& b, std::size_t at, int n) {
  std::uint64_t v = 0;
  for (int i = 0; i < n; ++i) v |= std::to_integer<std::uint64_t>(b[at + static_cast<std::size_t>(i)]) << (8 * i);
  return v;
}

VerifiedDump fake_dump() {
  VerifiedDump dump;
  dump.title = TitleId::mp4;
  dump.region = "USA";
  dump.revision = "1.0";
  dump.sha256 = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
  return dump;
}

TitleRegistry fake_registry() {
  TitleRegistry registry;
  REQUIRE(registry.add(TitleId::mp4, [] { return std::make_unique<test::FakeRuntime>(); }, &test::FakeAdapter::create)
              .ok());
  return registry;
}

std::unique_ptr<Session> start_ok(const TitleRegistry& registry, const SessionConfig& config) {
  auto started = Session::start(registry, config);
  if (const Result* failure = std::get_if<Result>(&started)) FAIL(failure->error());
  return std::move(std::get<std::unique_ptr<Session>>(started));
}

template <typename T>
T value_or_fail(std::variant<T, Result> v) {
  if (const Result* failure = std::get_if<Result>(&v)) FAIL(failure->error());
  return std::get<T>(std::move(v));
}

// Records a whole FakeRuntime party through Session and ReplayRecorder.
Replay record_party(const TitleRegistry& registry, const SessionConfig& config, bool embed) {
  ReplayRecorder recorder(value_or_fail(make_replay_header(config, embed)));
  auto session = start_ok(registry, config);
  for (std::uint64_t f = 0; f < kPartyFrames; ++f) {
    const InputFrame in = test::fake_party_script(f);
    REQUIRE(session->step(in).ok());
    REQUIRE(recorder.record(in, *session).ok());
  }
  return recorder.finish();
}

SessionConfig party_config(std::optional<SaveMedia> media = std::nullopt) {
  SessionConfig config;
  config.dump = fake_dump();
  config.runtime.seed = 99;
  config.runtime.save_media = std::move(media);
  return config;
}

}  // namespace

TEST_CASE("SHA-256 matches the FIPS 180-4 test vectors") {
  CHECK(to_hex(Sha256::of({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  const std::string abc = "abc";
  CHECK(to_hex(Sha256::of(std::as_bytes(std::span(abc.data(), abc.size())))) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(parse_sha256_hex(to_hex(counting_digest(3))) == counting_digest(3));
  CHECK_FALSE(parse_sha256_hex("abc").has_value());
  CHECK_FALSE(parse_sha256_hex(std::string(64, 'g')).has_value());
}

TEST_CASE("the writer run-length encodes and the reader restores every frame") {
  const Replay replay = synthetic_replay(false);
  CHECK(replay.frame_count == 10);
  // frames 0-2 idle, 3-5 A+Start, 6 idle, 7 analog, 8-9 idle
  REQUIRE(replay.runs.size() == 5);
  CHECK(replay.runs[0].length == 3);
  CHECK(replay.runs[1].length == 3);
  CHECK(replay.runs[2].length == 1);
  CHECK(replay.runs[3].length == 1);
  CHECK(replay.runs[4].length == 2);
  CHECK(replay.checkpoints.size() == 2);
  CHECK(replay.header.core_contract_version == kCoreContractVersion);

  ReplayReader reader(replay);
  InputFrame in;
  std::uint64_t n = 0;
  while (reader.next(in)) {
    CHECK(in.frame == n);
    CHECK((in.pads[0].buttons != 0) == (n >= 3 && n < 6));
    ++n;
  }
  CHECK(n == 10);
}

TEST_CASE("encode and decode round trip, with and without embedded save media") {
  for (bool embed : {false, true}) {
    CAPTURE(embed);
    const Replay replay = synthetic_replay(embed);
    const std::vector<std::byte> bytes = encode_replay(replay);
    CHECK(decode_ok(bytes) == replay);
    CHECK(encode_replay(decode_ok(bytes)) == bytes);
    CHECK(validate_replay(bytes).ok());
    CHECK(validate_replay_fixture(bytes).ok() == !embed);
  }
}

TEST_CASE("the header layout matches ADR 0006 byte for byte") {
  const std::vector<std::byte> b = encode_replay(synthetic_replay(true));
  CHECK(read_le(b, 0, 4) == 0x5254504dull);  // "MPTR"
  CHECK(read_le(b, 4, 4) == kReplayFormatVersion);
  CHECK(read_le(b, 8, 4) == kCoreContractVersion);
  CHECK(read_le(b, 12, 1) == static_cast<std::uint64_t>(TitleId::mp4));
  CHECK(read_le(b, 13, 1) == 0x10);  // dump_sha256 starts
  CHECK(read_le(b, 44, 1) == 0x2f);  // ... and ends
  CHECK(read_le(b, 45, 8) == 0x0123456789abcdefull);
  // 53..84 save_media_sha256, then the embedded image: u32 length, u32 version, data
  CHECK(read_le(b, 85, 4) == 12);
  CHECK(read_le(b, 89, 4) == 1);
  CHECK(read_le(b, 93, 1) == 0xa0);
  const std::size_t after_media = 89 + 12;
  CHECK(read_le(b, after_media, 4) == 0);        // ruleset length
  CHECK(read_le(b, after_media + 4, 8) == 10);   // frame_count
  CHECK(read_le(b, after_media + 12, 4) == 4);   // checkpoint_interval
  CHECK(read_le(b, after_media + 16, 4) == 3);   // first run length
  // Each run is 4 + 4 x 32 bytes; 5 runs, then the checkpoint count and 2 x 24 bytes.
  const std::size_t checkpoints_at = after_media + 16 + 5 * 132;
  CHECK(read_le(b, checkpoints_at, 4) == 2);
  CHECK(read_le(b, checkpoints_at + 4, 8) == 4);
  CHECK(b.size() == checkpoints_at + 4 + 2 * 24);
}

TEST_CASE("the encoding matches the committed golden on every platform") {
  const std::filesystem::path path = std::filesystem::path(MPT_GOLDEN_DIR) / "replay_v0_synthetic.mptr.hex";
  const std::vector<std::byte> actual = encode_replay(synthetic_replay(true));
  std::ifstream file(path, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  const std::vector<std::byte> golden = from_hex_lines(text.str());
  if (golden != actual) {
    const std::filesystem::path out = std::filesystem::temp_directory_path() / "replay_v0_synthetic.mptr.hex.actual";
    std::ofstream(out, std::ios::binary) << to_hex_lines(actual);
    FAIL("encoding differs from " << path.string() << "; actual bytes written to " << out.string());
  }
  CHECK(validate_replay(golden).ok());
}

TEST_CASE("every truncation and any trailing byte is rejected") {
  const std::vector<std::byte> bytes = encode_replay(synthetic_replay(true));
  for (std::size_t n = 0; n < bytes.size(); ++n) {
    CAPTURE(n);
    CHECK_FALSE(validate_replay(std::span(bytes).first(n)).ok());
  }
  std::vector<std::byte> longer = bytes;
  longer.push_back(std::byte{0});
  CHECK(contains(decode_error(longer), "1 trailing bytes"));
}

TEST_CASE("corrupt fields fail with a reason") {
  const std::vector<std::byte> good = encode_replay(synthetic_replay(true));
  const auto patched = [&](std::size_t at, std::uint64_t value, int n) {
    std::vector<std::byte> b = good;
    for (int i = 0; i < n; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<std::byte>((value >> (8 * i)) & 0xff);
    return b;
  };
  const std::size_t after_media = 89 + 12;
  const std::size_t checkpoints_at = after_media + 16 + 5 * 132;

  CHECK(contains(decode_error(patched(0, 'X', 1)), "not an .mptr"));
  CHECK(contains(decode_error(patched(4, 1, 4)), "format_version"));
  CHECK(contains(decode_error(patched(8, kCoreContractVersion + 1, 4)), "core contract"));
  CHECK(contains(decode_error(patched(12, 200, 1)), "unknown title"));
  CHECK(contains(decode_error(patched(93, 0, 1)), "does not match its digest"));
  CHECK(contains(decode_error(patched(after_media, 1, 4)), "ruleset"));
  CHECK(contains(decode_error(patched(after_media + 4, 9, 8)), "exceed frame_count"));
  CHECK(contains(decode_error(patched(after_media + 4, 11, 8)), "ends inside"));
  CHECK(contains(decode_error(patched(after_media + 12, 0, 4)), "checkpoint_interval is 0"));
  CHECK(contains(decode_error(patched(after_media + 16, 0, 4)), "length 0"));
  CHECK(contains(decode_error(patched(checkpoints_at + 4, 5, 8)), "multiple"));
  CHECK(contains(decode_error(patched(checkpoints_at + 4 + 24, 4, 8)), "does not increase"));
  CHECK(contains(decode_error(patched(checkpoints_at, 0xffffffffull, 4)), "ends inside"));

  // Embedded media in a replay that claims blank media.
  std::vector<std::byte> blank_hash = good;
  for (std::size_t i = 53; i < 85; ++i) blank_hash[i] = std::byte{0};
  CHECK(contains(decode_error(blank_hash), "blank media"));
}

TEST_CASE("arbitrary byte flips never crash the decoder") {
  const std::vector<std::byte> good = encode_replay(synthetic_replay(true));
  DeterministicRng rng(2026);
  for (int trial = 0; trial < 2000; ++trial) {
    std::vector<std::byte> b = good;
    const int flips = 1 + static_cast<int>(rng.below(4));
    for (int i = 0; i < flips; ++i) {
      b[rng.below(b.size())] ^= static_cast<std::byte>(1 + rng.below(255));
    }
    auto decoded = decode_replay(b);  // must return, either way
    if (const Replay* r = std::get_if<Replay>(&decoded)) CHECK(encode_replay(*r) == b);
  }
}

TEST_CASE("the writer refuses out-of-order frames and checkpoints") {
  ReplayWriter writer(ReplayHeader{});
  InputFrame in;
  in.frame = 1;
  CHECK_FALSE(writer.append(in).ok());
  in.frame = 0;
  REQUIRE(writer.append(in).ok());
  CHECK_FALSE(writer.addCheckpoint(ReplayCheckpoint{60, 0, 0}).ok());  // past the last frame
  CHECK_FALSE(writer.addCheckpoint(ReplayCheckpoint{0, 0, 0}).ok());
}

TEST_CASE("the writer refuses an unusable header") {
  InputFrame in;
  SUBCASE("checkpoint interval 0") {
    ReplayHeader header;
    header.checkpoint_interval = 0;
    ReplayWriter writer(header);
    CHECK(contains(writer.append(in).error(), "interval 0"));
    CHECK(contains(writer.addCheckpoint(ReplayCheckpoint{1, 0, 0}).error(), "interval 0"));
  }
  SUBCASE("embedded image that does not match its digest") {
    ReplayHeader header;
    header.embedded_save_media = test::FakeRuntime::make_save_media(1);
    ReplayWriter writer(header);  // save_media_sha256 left all zero
    CHECK(contains(writer.append(in).error(), "does not match"));
  }
}

TEST_CASE("verify_replay refuses a replay with checkpoint interval 0") {
  const TitleRegistry registry = fake_registry();
  Replay replay;
  replay.header.checkpoint_interval = 0;
  auto session = start_ok(registry, party_config());
  CHECK(contains(verify_replay(*session, replay).status.error(), "interval 0"));
}
TEST_CASE("a recorded party verifies on a fresh session") {
  const TitleRegistry registry = fake_registry();
  const SessionConfig config = party_config();
  const Replay replay = record_party(registry, config, false);
  REQUIRE(replay.frame_count == kPartyFrames);
  REQUIRE(replay.checkpoints.size() == kPartyFrames / kDefaultCheckpointInterval);
  CHECK(validate_replay_fixture(encode_replay(replay)).ok());

  const Replay loaded = decode_ok(encode_replay(replay));
  const SessionConfig playback = value_or_fail(session_config_for(loaded.header, fake_dump()));
  CHECK_FALSE(playback.runtime.save_media.has_value());
  CHECK(playback.runtime.seed == 99);
  auto session = start_ok(registry, playback);
  const ReplayVerification v = verify_replay(*session, loaded);
  CHECK_MESSAGE(v.status.ok(), v.status.error());
  CHECK(v.frames_played == kPartyFrames);
  CHECK(v.trail == loaded.checkpoints);
  CHECK(session->director().party().phase == PartyPhase::finished);
}

TEST_CASE("verification reports the first differing checkpoint") {
  const TitleRegistry registry = fake_registry();
  const Replay recorded = record_party(registry, party_config(), false);

  SUBCASE("state hash") {
    Replay tampered = recorded;
    tampered.checkpoints[3].state_hash ^= 1;
    auto session = start_ok(registry, value_or_fail(session_config_for(tampered.header, fake_dump())));
    const ReplayVerification v = verify_replay(*session, tampered);
    CHECK_FALSE(v.status.ok());
    CHECK(v.first_mismatch == 3u);
    CHECK(contains(v.status.error(), "state hash differs at frame 240"));
  }
  SUBCASE("party digest") {
    Replay tampered = recorded;
    tampered.checkpoints[2].party_digest ^= 1;
    auto session = start_ok(registry, value_or_fail(session_config_for(tampered.header, fake_dump())));
    const ReplayVerification v = verify_replay(*session, tampered);
    CHECK(v.first_mismatch == 2u);
    CHECK(contains(v.status.error(), "party digest"));
  }
  SUBCASE("inputs") {
    Replay tampered = recorded;
    tampered.runs[1].pads[0].buttons ^= kButtonA;
    auto session = start_ok(registry, value_or_fail(session_config_for(tampered.header, fake_dump())));
    CHECK(verify_replay(*session, tampered).first_mismatch.has_value());
  }
  SUBCASE("a session not at frame 0") {
    auto session = start_ok(registry, party_config());
    REQUIRE(session->step(test::fake_party_script(0)).ok());
    CHECK_FALSE(verify_replay(*session, recorded).status.ok());
  }
}

TEST_CASE("initial save media maps through the header (ADR 0006 amendment, point 2)") {
  const TitleRegistry registry = fake_registry();
  const SaveMedia card = test::FakeRuntime::make_save_media(5);
  const SessionConfig config = party_config(card);

  SUBCASE("embedded: self-contained, but not a valid fixture") {
    const Replay replay = record_party(registry, config, true);
    CHECK(replay.header.save_media_sha256 == save_media_digest(card));
    const std::vector<std::byte> bytes = encode_replay(replay);
    CHECK(validate_replay(bytes).ok());
    CHECK(contains(validate_replay_fixture(bytes).error(), "embedded save media"));

    const SessionConfig playback = value_or_fail(session_config_for(decode_ok(bytes).header, fake_dump()));
    CHECK(playback.runtime.save_media == card);
    auto session = start_ok(registry, playback);
    CHECK(verify_replay(*session, replay).status.ok());
  }
  SUBCASE("not embedded: the player supplies the image, matched by digest") {
    const Replay replay = record_party(registry, config, false);
    CHECK_FALSE(replay.header.embedded_save_media.has_value());
    CHECK(validate_replay_fixture(encode_replay(replay)).ok());

    CHECK(contains(std::get<Result>(session_config_for(replay.header, fake_dump())).error(), "none was supplied"));
    CHECK(contains(
        std::get<Result>(session_config_for(replay.header, fake_dump(), test::FakeRuntime::make_save_media(6))).error(),
        "does not match"));
    const SessionConfig playback = value_or_fail(session_config_for(replay.header, fake_dump(), card));
    auto session = start_ok(registry, playback);
    CHECK(verify_replay(*session, replay).status.ok());
  }
  SUBCASE("blank media ignores a supplied image") {
    const Replay replay = record_party(registry, party_config(), false);
    CHECK(replay.header.blank_save_media());
    const SessionConfig playback = value_or_fail(session_config_for(replay.header, fake_dump(), card));
    CHECK_FALSE(playback.runtime.save_media.has_value());
  }
}

TEST_CASE("headers and playback configs check the dump") {
  SessionConfig config = party_config();
  config.dump.sha256 = "not hex";
  CHECK_FALSE(std::holds_alternative<ReplayHeader>(make_replay_header(config, false)));
  config = party_config();
  config.rules = ResolvedRuleset{};
  CHECK(contains(std::get<Result>(make_replay_header(config, false)).error(), "ruleset"));

  const ReplayHeader header = value_or_fail(make_replay_header(party_config(), false));
  VerifiedDump other = fake_dump();
  other.sha256[0] = 'f';
  CHECK(contains(std::get<Result>(session_config_for(header, other)).error(), "recorded with dump"));
  other = fake_dump();
  other.title = TitleId::mp5;
  CHECK(contains(std::get<Result>(session_config_for(header, other)).error(), "recorded for mp4"));
}

TEST_CASE("party_events_digest covers events and snapshots") {
  CHECK(party_events_digest({}) != 0);
  PartyEvent e;
  e.kind = PartyEvent::Kind::phase_changed;
  e.frame = 10;
  e.snapshot.phase = PartyPhase::setup;
  std::vector<PartyEvent> events{e};
  const std::uint64_t base = party_events_digest(events);
  events[0].snapshot.players[2].coins = 1;
  CHECK(party_events_digest(events) != base);
  events[0] = e;
  events[0].frame = 11;
  CHECK(party_events_digest(events) != base);
  events[0] = e;
  CHECK(party_events_digest(events) == base);
}
