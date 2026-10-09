// FakeRuntime contract v1 behaviour (P1-20a): frame rate (ADR 0003), save media
// (ADR 0004) and the PartySnapshot v1 defaults (ADR 0005). The runtime-agnostic
// checks belong in the conformance suite (P1-20c); these pin the fake itself.

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "../support/determinism_harness.h"
#include "../support/fake_runtime.h"
#include "mpt/core/party.h"

using namespace mpt;
using test::FakeRuntime;

namespace {

constexpr std::uint64_t kFrames = 600;
constexpr std::uint64_t kInterval = 60;

// The shared script, plus port 0 pressing 'extra' on the listed frames.
test::InputSource with_presses(std::uint32_t extra, std::vector<std::uint64_t> frames) {
  return [extra, frames](std::uint64_t frame) {
    InputFrame in = test::scripted_input(frame);
    for (std::uint64_t f : frames) {
      if (f == frame) in.pads[0].buttons |= extra;
    }
    return in;
  };
}

RuntimeConfig config_with(std::uint64_t seed, std::optional<SaveMedia> media = std::nullopt) {
  RuntimeConfig config;
  config.seed = seed;
  config.save_media = std::move(media);
  return config;
}

test::TrailSpec spec_with(std::optional<SaveMedia> media, test::InputSource inputs = test::scripted_input) {
  test::TrailSpec spec;
  spec.config = config_with(1234, std::move(media));
  spec.inputs = std::move(inputs);
  spec.frames = kFrames;
  spec.interval = kInterval;
  return spec;
}

std::unique_ptr<ITitleRuntime> make_fake() { return std::make_unique<FakeRuntime>(); }

void step_range(FakeRuntime& runtime, const test::InputSource& inputs, std::uint64_t from, std::uint64_t to) {
  for (std::uint64_t f = from; f < to; ++f) runtime.stepFrame(inputs(f));
}

}  // namespace

TEST_CASE("the core contract is v1") { CHECK(kCoreContractVersion == 1); }

TEST_CASE("frame rate is valid after boot and survives a save state round trip") {
  FakeRuntime runtime;
  REQUIRE(runtime.boot({}, config_with(1)).ok());
  const FrameRate rate = runtime.frameRate();
  CHECK(rate.numerator > 0);
  CHECK(rate.denominator > 0);
  CHECK(rate == FrameRate{60, 1});

  step_range(runtime, test::scripted_input, 0, 30);
  const SaveState snapshot = runtime.saveState();
  FakeRuntime other;
  REQUIRE(other.boot({}, config_with(2)).ok());
  REQUIRE(other.loadState(snapshot).ok());
  CHECK(other.frameRate() == runtime.frameRate());
}

TEST_CASE("the test rate switch changes the rate only through stepFrame and is game state") {
  const test::InputSource inputs = with_presses(FakeRuntime::kRateToggleButton, {10, 20});
  FakeRuntime runtime;
  REQUIRE(runtime.boot({}, config_with(1)).ok());
  step_range(runtime, inputs, 0, 10);
  CHECK(runtime.frameRate() == FakeRuntime::kRate60);
  runtime.stepFrame(inputs(10));  // the press
  CHECK(runtime.frameRate() == FakeRuntime::kRate50);
  step_range(runtime, inputs, 11, 15);
  CHECK(runtime.frameRate() == FakeRuntime::kRate50);  // holding or releasing does not toggle

  const SaveState at_50 = runtime.saveState();
  step_range(runtime, inputs, 15, 21);
  CHECK(runtime.frameRate() == FakeRuntime::kRate60);  // second press at frame 20

  FakeRuntime resumed;
  REQUIRE(resumed.boot({}, config_with(1)).ok());
  REQUIRE(resumed.loadState(at_50).ok());
  CHECK(resumed.frameRate() == FakeRuntime::kRate50);

  // The rate is covered by the hash: a run left at 50 diverges from one that stays at 60.
  // (Two presses toggle back, so only a single press is used here.)
  const auto toggled = test::run_fresh_instance(
      make_fake, spec_with(std::nullopt, with_presses(FakeRuntime::kRateToggleButton, std::vector<std::uint64_t>(1, 10))));
  const auto plain = test::run_fresh_instance(make_fake, spec_with(std::nullopt));
  REQUIRE(toggled.status.ok());
  REQUIRE(plain.status.ok());
  CHECK(test::first_divergence(toggled.trail, plain.trail) == std::optional<std::size_t>(0));
}

TEST_CASE("blank and supplied save media at boot") {
  FakeRuntime runtime;
  REQUIRE(runtime.boot({}, config_with(1)).ok());
  CHECK(runtime.saveMediaGeneration() == 0);
  CHECK(runtime.saveMedia() == FakeRuntime::make_save_media(0));
  CHECK(runtime.saveMedia().format_version == FakeRuntime::kSaveMediaFormat);

  REQUIRE(runtime.boot({}, config_with(1, FakeRuntime::make_save_media(7))).ok());
  CHECK(runtime.saveMediaGeneration() == 0);
  CHECK(runtime.saveMedia() == FakeRuntime::make_save_media(7));
}

TEST_CASE("boot rejects save media in an unknown format") {
  SaveMedia wrong_version = FakeRuntime::make_save_media(1);
  wrong_version.format_version = FakeRuntime::kSaveMediaFormat + 1;
  SaveMedia wrong_size = FakeRuntime::make_save_media(1);
  wrong_size.data.push_back(std::byte{0});
  SaveMedia empty;

  FakeRuntime runtime;
  CHECK_FALSE(runtime.boot({}, config_with(1, wrong_version)).ok());
  CHECK_FALSE(runtime.boot({}, config_with(1, wrong_size)).ok());
  CHECK_FALSE(runtime.boot({}, config_with(1, empty)).ok());
}

TEST_CASE("a save press commits a write on that frame, and boot resets the generation") {
  const test::InputSource inputs = with_presses(FakeRuntime::kSaveButton, {5, 6, 9});
  FakeRuntime runtime;
  REQUIRE(runtime.boot({}, config_with(1, FakeRuntime::make_save_media(3))).ok());
  step_range(runtime, inputs, 0, 5);
  CHECK(runtime.saveMediaGeneration() == 0);
  runtime.stepFrame(inputs(5));  // press
  CHECK(runtime.saveMediaGeneration() == 1);
  CHECK(runtime.saveMedia() == FakeRuntime::make_save_media(4));
  runtime.stepFrame(inputs(6));  // still held: no second write
  CHECK(runtime.saveMediaGeneration() == 1);
  step_range(runtime, inputs, 7, 10);  // released at 7, pressed again at 9
  CHECK(runtime.saveMediaGeneration() == 2);
  CHECK(runtime.saveMedia() == FakeRuntime::make_save_media(5));

  REQUIRE(runtime.boot({}, config_with(1, runtime.saveMedia())).ok());
  CHECK(runtime.saveMediaGeneration() == 0);
  CHECK(runtime.saveMedia() == FakeRuntime::make_save_media(5));
}

TEST_CASE("the generation changes on the same frame in every run") {
  const test::InputSource inputs = with_presses(FakeRuntime::kSaveButton, {17, 250, 251, 400});
  auto generations = [&inputs]() {
    FakeRuntime runtime;
    REQUIRE(runtime.boot({}, config_with(99)).ok());
    std::vector<std::uint64_t> out;
    for (std::uint64_t f = 0; f < kFrames; ++f) {
      runtime.stepFrame(inputs(f));
      out.push_back(runtime.saveMediaGeneration());
    }
    return out;
  };
  const std::vector<std::uint64_t> first = generations();
  CHECK(first == generations());
  CHECK(first[16] == 0);
  CHECK(first[17] == 1);
  CHECK(first[251] == 2);  // 250 press, 251 held
  CHECK(first.back() == 3);
}

TEST_CASE("the initial save image is a determinism input") {
  const auto blank_a = test::run_sequential(make_fake, spec_with(std::nullopt), 2);
  const auto image_a = test::run_sequential(make_fake, spec_with(FakeRuntime::make_save_media(5)), 2);
  const auto image_b = test::run_fresh_instance(make_fake, spec_with(FakeRuntime::make_save_media(6)));
  for (const auto& r : blank_a) REQUIRE(r.status.ok());
  for (const auto& r : image_a) REQUIRE(r.status.ok());
  REQUIRE(image_b.status.ok());

  CHECK_FALSE(test::first_divergence(blank_a[0].trail, blank_a[1].trail).has_value());
  CHECK_FALSE(test::first_divergence(image_a[0].trail, image_a[1].trail).has_value());
  CHECK(test::first_divergence(blank_a[0].trail, image_a[0].trail) == std::optional<std::size_t>(0));
  CHECK(test::first_divergence(image_a[0].trail, image_b.trail) == std::optional<std::size_t>(0));
  // Blank media and an explicit counter-0 image are the same card.
  const auto zero = test::run_fresh_instance(make_fake, spec_with(FakeRuntime::make_save_media(0)));
  REQUIRE(zero.status.ok());
  CHECK_FALSE(test::first_divergence(blank_a[0].trail, zero.trail).has_value());
}

TEST_CASE("save media and generation survive saveState and loadState") {
  const test::InputSource inputs = with_presses(FakeRuntime::kSaveButton, {3, 8, 40});
  FakeRuntime straight;
  REQUIRE(straight.boot({}, config_with(5, FakeRuntime::make_save_media(10))).ok());
  step_range(straight, inputs, 0, kFrames);

  FakeRuntime first;
  REQUIRE(first.boot({}, config_with(5, FakeRuntime::make_save_media(10))).ok());
  step_range(first, inputs, 0, 20);
  const SaveState snapshot = first.saveState();
  CHECK(snapshot.format_version == FakeRuntime::kStateFormat);
  CHECK(snapshot.format_version == 2);

  FakeRuntime resumed;
  REQUIRE(resumed.boot({}, config_with(0)).ok());  // different seed and blank media
  REQUIRE(resumed.loadState(snapshot).ok());
  CHECK(resumed.stateHash() == first.stateHash());
  CHECK(resumed.saveMedia() == first.saveMedia());
  CHECK(resumed.saveMedia() == FakeRuntime::make_save_media(12));
  CHECK(resumed.saveMediaGeneration() == 2);
  CHECK(resumed.frameRate() == first.frameRate());

  step_range(resumed, inputs, 20, kFrames);
  CHECK(resumed.stateHash() == straight.stateHash());
  CHECK(resumed.saveMediaGeneration() == straight.saveMediaGeneration());
  CHECK(resumed.saveMedia() == straight.saveMedia());
}

TEST_CASE("loadState rejects old, foreign or corrupt states and keeps the current one") {
  FakeRuntime source;
  REQUIRE(source.boot({}, config_with(1)).ok());
  step_range(source, test::scripted_input, 0, 10);
  const SaveState good = source.saveState();

  FakeRuntime target;
  REQUIRE(target.boot({}, config_with(2)).ok());
  step_range(target, test::scripted_input, 0, 3);
  const std::uint64_t before = target.stateHash();

  SaveState v1 = good;
  v1.format_version = 1;  // the contract v0 layout
  SaveState foreign = good;
  foreign.title = TitleId::mp1;
  SaveState short_data = good;
  short_data.data.pop_back();
  SaveState zero_rate = good;
  for (std::size_t b = 0; b < 8; ++b) zero_rate.data[6 * 8 + b] = std::byte{0};  // rate numerator
  SaveState wrong_frame = good;
  wrong_frame.frame = good.frame + 1;

  for (const SaveState* bad : {&v1, &foreign, &short_data, &zero_rate, &wrong_frame}) {
    CHECK_FALSE(target.loadState(*bad).ok());
    CHECK(target.stateHash() == before);
  }
  CHECK(target.loadState(good).ok());
  CHECK(target.stateHash() == source.stateHash());
}

TEST_CASE("PartySnapshot v1 fields default to no party") {
  const PartySnapshot snapshot;
  CHECK(snapshot.phase == PartyPhase::none);
  CHECK_FALSE(snapshot.current_minigame.has_value());
  CHECK(snapshot == PartySnapshot{});

  PartySnapshot in_minigame;
  in_minigame.phase = PartyPhase::minigame;
  CHECK_FALSE(in_minigame == snapshot);
  PartySnapshot with_id;
  with_id.current_minigame = ContentId::parse("mp4.minigame.synthetic_test");
  REQUIRE(with_id.current_minigame.has_value());
  CHECK_FALSE(with_id == snapshot);
  CHECK(with_id == with_id);
}
