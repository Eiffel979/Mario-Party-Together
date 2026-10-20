// Runtime status (ADR 0011): a halt or fault reaches Session::step as a failed Result,
// the stopping frame still counts and is recorded, and loadState can revive a runtime.

#include <doctest/doctest.h>

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "../shell/fake_title.h"
#include "../support/delegating_runtime.h"
#include "../support/fake_adapter.h"
#include "../support/fake_party_script.h"
#include "frame_loop.h"
#include "mpt/core/replay.h"
#include "mpt/core/session.h"

using namespace mpt;

namespace {

// FakeRuntime that stops once `stop_at` frames have been stepped. The status is derived
// from the frame count, so it is game state: loadState restores it.
class StoppingRuntime final : public test::DelegatingRuntime {
 public:
  static inline std::uint64_t stop_at = 10;
  static inline RuntimeStatus how = RuntimeStatus::faulted;
  static inline int steps = 0;

  void stepFrame(const InputFrame& inputs) override {
    ++steps;
    inner_.stepFrame(inputs);
    frame_ = inputs.frame + 1;
  }
  Result loadState(const SaveState& state) override {
    Result loaded = inner_.loadState(state);
    if (loaded) frame_ = state.frame;
    return loaded;
  }
  Result boot(const VerifiedDump& dump, const RuntimeConfig& config) override {
    frame_ = 0;
    return inner_.boot(dump, config);
  }
  RuntimeHealth status() const override {
    if (frame_ < stop_at) return {};
    return RuntimeHealth{how, how == RuntimeStatus::halted ? "main loop returned" : "watchdog"};
  }

 private:
  std::uint64_t frame_ = 0;
};

TitleRegistry stopping_registry() {
  TitleRegistry registry;
  REQUIRE(registry
              .add(TitleId::mp4, [] { return std::make_unique<StoppingRuntime>(); },
                   [](ITitleRuntime& runtime) -> std::unique_ptr<ITitleAdapter> {
                     return std::make_unique<test::FakeAdapter>(dynamic_cast<StoppingRuntime&>(runtime).inner());
                   })
              .ok());
  return registry;
}

SessionConfig config() {
  SessionConfig c;
  c.dump = test::fake_shell_dump();
  c.runtime.seed = 5;
  return c;
}

std::unique_ptr<Session> start_ok(const TitleRegistry& registry) {
  auto started = Session::start(registry, config());
  if (const Result* failure = std::get_if<Result>(&started)) FAIL(failure->error());
  return std::move(std::get<std::unique_ptr<Session>>(started));
}

bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

}  // namespace

TEST_CASE("runtimes that never stop report running (the default)") {
  test::FakeRuntime fake;
  CHECK(fake.status() == RuntimeHealth{});
  REQUIRE(fake.boot(test::fake_shell_dump(), RuntimeConfig{}).ok());
  fake.stepFrame(test::fake_party_script(0));
  CHECK(fake.status().status == RuntimeStatus::running);
}

TEST_CASE("Session::step reports a fault, counts that frame, and steps no further") {
  StoppingRuntime::stop_at = 10;
  StoppingRuntime::how = RuntimeStatus::faulted;
  StoppingRuntime::steps = 0;
  const TitleRegistry registry = stopping_registry();
  auto session = start_ok(registry);

  for (std::uint64_t f = 0; f < 9; ++f) REQUIRE(session->step(test::fake_party_script(f)).ok());
  const SaveState before_fault = session->runtime().saveState();

  const Result faulted = session->step(test::fake_party_script(9));
  CHECK_FALSE(faulted.ok());
  CHECK(contains(faulted.error(), "runtime faulted at frame 9: watchdog"));
  CHECK(session->frame() == 10);  // the frame that faulted still counts
  CHECK(StoppingRuntime::steps == 10);
  CHECK(session->runtime().status().status == RuntimeStatus::faulted);

  const Result again = session->step(test::fake_party_script(10));
  CHECK(contains(again.error(), "runtime faulted before frame 10"));
  CHECK(StoppingRuntime::steps == 10);  // not stepped
  CHECK(session->frame() == 10);

  SUBCASE("loadState of a running state revives the runtime") {
    REQUIRE(session->loadState(before_fault).ok());
    CHECK(session->runtime().status().status == RuntimeStatus::running);
    StoppingRuntime::stop_at = 100;
    CHECK(session->step(test::fake_party_script(9)).ok());
    CHECK(session->frame() == 10);
  }
}

TEST_CASE("a halt is reported as a halt") {
  StoppingRuntime::stop_at = 3;
  StoppingRuntime::how = RuntimeStatus::halted;
  const TitleRegistry registry = stopping_registry();
  auto session = start_ok(registry);
  REQUIRE(session->step(test::fake_party_script(0)).ok());
  REQUIRE(session->step(test::fake_party_script(1)).ok());
  CHECK(contains(session->step(test::fake_party_script(2)).error(), "runtime halted at frame 2: main loop returned"));
}

TEST_CASE("Session::start refuses a runtime that is not running after boot") {
  StoppingRuntime::stop_at = 0;
  StoppingRuntime::how = RuntimeStatus::faulted;
  const TitleRegistry registry = stopping_registry();
  auto started = Session::start(registry, config());
  REQUIRE(std::holds_alternative<Result>(started));
  CHECK(contains(std::get<Result>(started).error(), "runtime faulted after boot"));
}

TEST_CASE("a recorded fault is in the replay, and verifying it reports the fault") {
  StoppingRuntime::stop_at = 70;
  StoppingRuntime::how = RuntimeStatus::faulted;
  const TitleRegistry registry = stopping_registry();

  auto header = make_replay_header(config(), false);
  REQUIRE(std::holds_alternative<ReplayHeader>(header));
  ReplayRecorder recorder(std::get<ReplayHeader>(header));
  auto session = start_ok(registry);
  shell::FrameLoopHooks hooks;
  hooks.sample = [](std::uint64_t f) -> std::optional<InputFrame> { return test::fake_party_script(f); };
  shell::FrameLoop loop(*session, std::move(hooks), &recorder);
  shell::FrameLoop::Tick tick = shell::FrameLoop::Tick::stepped;
  while (tick == shell::FrameLoop::Tick::stepped) tick = loop.tick(false);
  CHECK(tick == shell::FrameLoop::Tick::failed);
  CHECK(contains(loop.failure()->error(), "runtime faulted at frame 69"));
  CHECK(loop.frames_stepped() == 70);

  const Replay replay = recorder.finish();
  CHECK(replay.frame_count == 70);  // the faulting frame's inputs are kept
  CHECK(replay.checkpoints.size() == 1);  // frame 60

  session.reset();
  auto playback = start_ok(registry);
  const ReplayVerification v = verify_replay(*playback, replay);
  CHECK_FALSE(v.status.ok());
  CHECK(contains(v.status.error(), "runtime faulted at frame 69"));
  CHECK(v.frames_played == 69);
}
