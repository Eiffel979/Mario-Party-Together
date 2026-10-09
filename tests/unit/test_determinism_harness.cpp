// Sequential determinism harness tests (P1-20b1, ADR 0002 rule 10 follow-up).
// The cross-process half is the determinism_cross_process CTest test.

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>

#include "../support/determinism_harness.h"
#include "../support/fake_runtime.h"

using namespace mpt;

namespace {

constexpr std::uint64_t kFrames = 600;
constexpr std::uint64_t kInterval = 60;

test::TrailSpec spec_with_seed(std::uint64_t seed) {
  test::TrailSpec spec;
  spec.config.seed = seed;
  spec.inputs = test::scripted_input;
  spec.frames = kFrames;
  spec.interval = kInterval;
  return spec;
}

std::unique_ptr<ITitleRuntime> make_fake() { return std::make_unique<test::FakeRuntime>(); }

// Forwards every ITitleRuntime method (contract v1) to a FakeRuntime, so tests can
// change one behaviour.
class DelegatingRuntime : public ITitleRuntime {
 public:
  TitleId id() const override { return inner_.id(); }
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

 protected:
  test::FakeRuntime inner_;
};

// A classic port bug: a process-global that boot() never resets and that feeds into the
// state hash. Each fresh process starts it at the same value, so only running instances
// one after another in one process exposes it.
std::uint64_t g_leaked_boot_count = 0;

class LeakyGlobalRuntime final : public DelegatingRuntime {
 public:
  Result boot(const VerifiedDump& dump, const RuntimeConfig& config) override {
    ++g_leaked_boot_count;
    return inner_.boot(dump, config);
  }
  std::uint64_t stateHash() override {
    StateHasher hasher;
    hasher.update_u64(inner_.stateHash());
    hasher.update_u64(g_leaked_boot_count);
    return hasher.digest();
  }
};

// Tracks how many instances are alive, to check the harness never overlaps two.
class CountingRuntime final : public DelegatingRuntime {
 public:
  static inline int live = 0;
  static inline int max_live = 0;

  CountingRuntime() {
    ++live;
    if (live > max_live) max_live = live;
  }
  ~CountingRuntime() override { --live; }
  CountingRuntime(const CountingRuntime&) = delete;
  CountingRuntime& operator=(const CountingRuntime&) = delete;
};

class FailingBootRuntime final : public DelegatingRuntime {
 public:
  Result boot(const VerifiedDump&, const RuntimeConfig&) override { return Result::failure("synthetic boot failure"); }
};

}  // namespace

TEST_CASE("FakeRuntime instances run one after another give identical trails") {
  const auto results = test::run_sequential(make_fake, spec_with_seed(1234), 3);
  REQUIRE(results.size() == 3);
  for (const test::TrailResult& result : results) {
    REQUIRE(result.status.ok());
    REQUIRE(result.trail.size() == kFrames / kInterval);
    CHECK(result.trail.front().frame == kInterval);
    CHECK(result.trail.back().frame == kFrames);
  }
  for (std::size_t i = 1; i < results.size(); ++i) {
    INFO(test::describe_divergence(results[0].trail, results[i].trail));
    CHECK_FALSE(test::first_divergence(results[0].trail, results[i].trail).has_value());
  }
}

TEST_CASE("the sequential harness records the same hashes as run_replay") {
  std::vector<std::uint64_t> replay_hashes;
  {
    test::FakeRuntime runtime;
    REQUIRE(runtime.boot({}, RuntimeConfig{.seed = 1234}).ok());
    replay_hashes = test::run_replay(runtime, test::scripted_input, kFrames, kInterval);
  }  // destroyed before the next instance exists (ADR 0002 rule 10)
  const test::TrailResult fresh = test::run_fresh_instance(make_fake, spec_with_seed(1234));
  REQUIRE(fresh.status.ok());
  REQUIRE(fresh.trail.size() == replay_hashes.size());
  for (std::size_t i = 0; i < replay_hashes.size(); ++i) CHECK(fresh.trail[i].hash == replay_hashes[i]);
}

TEST_CASE("a different seed gives a different trail") {
  const test::TrailResult a = test::run_fresh_instance(make_fake, spec_with_seed(1));
  const test::TrailResult b = test::run_fresh_instance(make_fake, spec_with_seed(2));
  REQUIRE(a.status.ok());
  REQUIRE(b.status.ok());
  CHECK(test::first_divergence(a.trail, b.trail) == std::optional<std::size_t>(0));
}

TEST_CASE("run_sequential never has two instances alive at once") {
  CountingRuntime::live = 0;
  CountingRuntime::max_live = 0;
  const auto results =
      test::run_sequential([] { return std::make_unique<CountingRuntime>(); }, spec_with_seed(7), 4);
  REQUIRE(results.size() == 4);
  CHECK(CountingRuntime::max_live == 1);
  CHECK(CountingRuntime::live == 0);
}

TEST_CASE("sequential runs expose state leaked between instances") {
  const auto results =
      test::run_sequential([] { return std::make_unique<LeakyGlobalRuntime>(); }, spec_with_seed(1234), 2);
  REQUIRE(results.size() == 2);
  REQUIRE(results[0].status.ok());
  REQUIRE(results[1].status.ok());
  CHECK(test::first_divergence(results[0].trail, results[1].trail).has_value());
}

TEST_CASE("boot failures and null factories are reported, not compared") {
  const test::TrailResult failed =
      test::run_fresh_instance([] { return std::make_unique<FailingBootRuntime>(); }, spec_with_seed(1));
  CHECK_FALSE(failed.status.ok());
  CHECK(failed.trail.empty());

  const test::TrailResult null_factory =
      test::run_fresh_instance([]() -> std::unique_ptr<ITitleRuntime> { return nullptr; }, spec_with_seed(1));
  CHECK_FALSE(null_factory.status.ok());
}

TEST_CASE("first_divergence finds the first differing checkpoint") {
  const test::HashTrail base = {{60, 1}, {120, 2}, {180, 3}};
  CHECK_FALSE(test::first_divergence(base, base).has_value());
  CHECK(test::first_divergence(base, test::HashTrail{{60, 1}, {120, 9}, {180, 3}}) == std::optional<std::size_t>(1));
  CHECK(test::first_divergence(base, test::HashTrail{{60, 1}, {121, 2}, {180, 3}}) == std::optional<std::size_t>(1));
  CHECK(test::first_divergence(base, test::HashTrail{{60, 1}}) == std::optional<std::size_t>(1));
  CHECK(test::first_divergence(test::HashTrail{}, base) == std::optional<std::size_t>(0));
}

TEST_CASE("trail text round-trips and matches the sidecar layout") {
  const test::HashTrail trail = {{60, 0x0123456789abcdefull}, {120, 0}, {180, 0xffffffffffffffffull}};
  const std::string text = test::format_trail(trail);
  CHECK(text == "60 0123456789abcdef\n120 0000000000000000\n180 ffffffffffffffff\n");
  CHECK(test::parse_trail(text) == std::optional<test::HashTrail>(trail));

  const std::string annotated = "# header\r\n\r\n60 0123456789abcdef\r\n120 0000000000000000\r\n180 ffffffffffffffff";
  CHECK(test::parse_trail(annotated) == std::optional<test::HashTrail>(trail));
  CHECK(test::parse_trail("") == std::optional<test::HashTrail>(test::HashTrail{}));
}

TEST_CASE("parse_trail rejects malformed text") {
  const char* bad[] = {
      "60 0123456789ABCDEF\n",           // uppercase hex
      "60 0123456789abcde\n",            // 15 digits
      "60 0123456789abcdef0\n",          // 17 digits
      "600123456789abcdef\n",            // no separator
      " 0123456789abcdef\n",             // no frame
      "x60 0123456789abcdef\n",          // non-numeric frame
      "60 0123456789abcdef extra\n",     // trailing text
      "120 0000000000000000\n60 0000000000000000\n",  // frames go backwards
      "60 0000000000000000\n60 0000000000000000\n",   // repeated frame
  };
  for (const char* text : bad) {
    INFO(text);
    CHECK_FALSE(test::parse_trail(text).has_value());
  }
}
