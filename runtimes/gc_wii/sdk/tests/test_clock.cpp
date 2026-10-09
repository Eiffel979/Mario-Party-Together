// Virtual time: epoch from the seed, fixed quantum per query, frame-driven
// fields at 60000/1001 Hz, busy-waits that terminate (E10, ADR 0008 §2).
#include "harness.h"
#include "mpt/core/rng.h"
#include "mpt_gc/os.h"
#include "mpt_gc/vi.h"

using namespace mpt;
using namespace mpt::gc;
using namespace mpt::gc::test;

namespace {
constexpr std::int64_t kTicksPerDay = 86400ll * kTimerClockHz;
constexpr std::int64_t kNtscField = 675675;  // 40.5 MHz * 1001 / 60000, exact
}  // namespace

TEST_CASE("each time query advances virtual time by exactly one quantum") {
  std::vector<OSTime> t;
  OSTick tick0 = 0, tick1 = 0;
  Game g{[&] {
    for (int i = 0; i < 4; ++i) t.push_back(OSGetTime());
    tick0 = OSGetTick();
    tick1 = OSGetTick();
  }};
  auto sdk = boot(g);
  const std::int64_t epoch = sdk->epoch();
  CHECK(step(*sdk) == StepStatus::halted);
  REQUIRE(t.size() == 4);
  for (int i = 0; i < 4; ++i) CHECK(t[i] == epoch + (i + 1) * std::int64_t{kTimeQueryQuantumTicks});
  CHECK(tick1 - tick0 == kTimeQueryQuantumTicks);
  CHECK(static_cast<OSTick>(static_cast<std::uint64_t>(epoch + 5 * kTimeQueryQuantumTicks)) == tick0);
}

TEST_CASE("the epoch is a seeded, valid calendar date") {
  std::vector<std::int64_t> epochs;
  for (std::uint64_t seed : {0ull, 1ull, 2ull, 0xDEADBEEFull, ~0ull}) {
    Game g{[] {}};
    SdkConfig cfg;
    cfg.seed = seed;
    auto sdk = boot(g, cfg);
    const std::int64_t e = sdk->epoch();
    // Same derivation as the SDK, spelled out: SplitMix64 on seed ^ domain tag.
    DeterministicRng rng(seed ^ 0x4743'5254'4345'504Full);
    CHECK(e == 366 * kTicksPerDay + static_cast<std::int64_t>(rng.below(static_cast<std::uint64_t>((11323 - 366) * kTicksPerDay))));
    OSCalendarTime ct{};
    OSTicksToCalendarTime(e, &ct);
    CHECK(ct.year >= 2001);
    CHECK(ct.year <= 2030);
    epochs.push_back(e);
  }
  for (std::size_t i = 0; i < epochs.size(); ++i)
    for (std::size_t j = i + 1; j < epochs.size(); ++j) CHECK(epochs[i] != epochs[j]);
}

TEST_CASE("golden epoch for seed 0") {
  Game g{[] {}};
  auto sdk = boot(g);
  CHECK(sdk->epoch() == 0x0021'D2F1'7FEB'2D1Bll);
}

TEST_CASE("fields are frame-driven at 60000/1001 Hz") {
  Game g{[] {
    for (;;) VIWaitForRetrace();
  }};
  auto sdk = boot(g);
  const std::int64_t epoch = sdk->epoch();
  for (int n = 0; n <= 60000; ++n) {
    REQUIRE(step(*sdk) == StepStatus::running);
    if (n == 1 || n == 1000) CHECK(sdk->now() == epoch + n * kNtscField);
  }
  // 60000 fields = 1001 seconds exactly.
  CHECK(sdk->now() == epoch + 1001ll * kTimerClockHz);
  CHECK(sdk->field_rate() == std::pair<std::uint32_t, std::uint32_t>{60000, 1001});
}

TEST_CASE("PAL fields are 50 Hz") {
  Game g{[] {
    for (;;) VIWaitForRetrace();
  }};
  SdkConfig cfg;
  cfg.video = VideoStandard::pal50;
  auto sdk = boot(g, cfg);
  stepN(*sdk, 51);
  CHECK(sdk->now() == sdk->epoch() + kTimerClockHz);
  CHECK(sdk->field_rate() == std::pair<std::uint32_t, std::uint32_t>{50, 1});
}

TEST_CASE("the field clock is a floor: time spent in queries is kept") {
  Game g{[] {
    for (int i = 0; i < 2000; ++i) OSGetTick();  // 2000 * 10 us = 20 ms, more than a field
    for (;;) VIWaitForRetrace();
  }};
  auto sdk = boot(g);
  const std::int64_t epoch = sdk->epoch();
  REQUIRE(step(*sdk) == StepStatus::running);
  CHECK(sdk->now() == epoch + 2000 * std::int64_t{kTimeQueryQuantumTicks});
  REQUIRE(step(*sdk) == StepStatus::running);  // R_1 is earlier than now: time does not go back
  CHECK(sdk->now() == epoch + 2000 * std::int64_t{kTimeQueryQuantumTicks});
  REQUIRE(step(*sdk) == StepStatus::running);
  CHECK(sdk->now() == epoch + 2 * kNtscField);
}

TEST_CASE("a non-yielding busy-wait on the clock terminates") {
  int iterations = 0;
  Game g{[&] {
    const OSTime start = OSGetTime();
    while (OSGetTime() - start < OSMillisecondsToTicks(500)) ++iterations;
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::halted);
  // 500 ms = 20,250,000 ticks = 50,000 quanta.
  CHECK(iterations == 49999);
}

namespace {
volatile int g_alarm_flag = 0;
void setFlag(OSAlarm*, OSContext*) { g_alarm_flag = 1; }
}  // namespace

TEST_CASE("a busy-wait on an interrupt-set flag terminates") {
  int iterations = 0;
  Game g{[&] {
    static OSAlarm alarm;
    g_alarm_flag = 0;
    OSCreateAlarm(&alarm);
    OSSetAlarm(&alarm, OSMillisecondsToTicks(1), &setFlag);
    while (!g_alarm_flag) {
      OSGetTick();
      ++iterations;
    }
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(iterations == 100);  // 1 ms = 40,500 ticks = 100 quanta
}

TEST_CASE("an endless busy-wait trips the watchdog instead of hanging") {
  Game g{[] {
    for (;;) OSGetTick();
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::faulted);
  CHECK(sdk->fault().find("watchdog") != std::string::npos);
}

TEST_CASE("calendar conversion round-trips") {
  Game g{[] {}};
  auto sdk = boot(g);
  OSCalendarTime in{};
  in.year = 2004;
  in.mon = 1;  // February
  in.mday = 29;
  in.hour = 12;
  in.min = 34;
  in.sec = 56;
  in.msec = 789;
  in.usec = 500;
  const OSTime t = OSCalendarTimeToTicks(&in);
  OSCalendarTime out{};
  OSTicksToCalendarTime(t, &out);
  CHECK(out.year == 2004);
  CHECK(out.mon == 1);
  CHECK(out.mday == 29);
  CHECK(out.hour == 12);
  CHECK(out.min == 34);
  CHECK(out.sec == 56);
  CHECK(out.msec == 789);
  CHECK(out.usec == 500);
  CHECK(out.wday == 0);   // 2004-02-29 was a Sunday
  CHECK(out.yday == 59);
  OSTicksToCalendarTime(0, &out);
  CHECK(out.year == 2000);
  CHECK(out.wday == 6);  // Saturday
}
