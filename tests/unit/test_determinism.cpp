#include <doctest/doctest.h>

#include "../support/determinism_harness.h"
#include "../support/fake_runtime.h"
#include "../support/replay_harness.h"

using namespace mpt;

namespace {

// Synthetic input script, shared with the sequential and cross-process harness.
using test::scripted_input;

constexpr std::uint64_t kFrames = 600;
constexpr std::uint64_t kInterval = 60;

}  // namespace

TEST_CASE("identical inputs and seed give identical state hashes") {
  test::FakeRuntime a;
  test::FakeRuntime b;
  REQUIRE(a.boot({}, RuntimeConfig{.seed = 1234}).ok());
  REQUIRE(b.boot({}, RuntimeConfig{.seed = 1234}).ok());
  CHECK(test::run_replay(a, scripted_input, kFrames, kInterval) ==
        test::run_replay(b, scripted_input, kFrames, kInterval));
}

TEST_CASE("a different seed diverges, so the hash actually covers RNG state") {
  test::FakeRuntime a;
  test::FakeRuntime b;
  REQUIRE(a.boot({}, RuntimeConfig{.seed = 1}).ok());
  REQUIRE(b.boot({}, RuntimeConfig{.seed = 2}).ok());
  CHECK(test::run_replay(a, scripted_input, kFrames, kInterval) !=
        test::run_replay(b, scripted_input, kFrames, kInterval));
}

TEST_CASE("save state, then load and continue, matches an uninterrupted run") {
  test::FakeRuntime straight;
  REQUIRE(straight.boot({}, RuntimeConfig{.seed = 99}).ok());
  for (std::uint64_t f = 0; f < kFrames; ++f) straight.stepFrame(scripted_input(f));

  test::FakeRuntime first;
  REQUIRE(first.boot({}, RuntimeConfig{.seed = 99}).ok());
  for (std::uint64_t f = 0; f < kFrames / 2; ++f) first.stepFrame(scripted_input(f));
  const SaveState snapshot = first.saveState();
  CHECK(snapshot.frame == kFrames / 2);

  test::FakeRuntime resumed;
  REQUIRE(resumed.boot({}, RuntimeConfig{.seed = 0}).ok());
  REQUIRE(resumed.loadState(snapshot).ok());
  for (std::uint64_t f = kFrames / 2; f < kFrames; ++f) resumed.stepFrame(scripted_input(f));

  CHECK(resumed.stateHash() == straight.stateHash());
}
