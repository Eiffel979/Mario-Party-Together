// Golden cross-platform hash trail for the contract-v1 FakeRuntime (P1-20b2).
//
// tests/golden/fake_runtime_v1_party.trail.txt was recorded on Windows. The Windows and
// Linux CI jobs both run this test, so a hash that depends on the platform, compiler or
// standard library fails on one of them. On a mismatch the message names the first
// diverging checkpoint and prints both trails.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "../support/determinism_harness.h"
#include "../support/fake_party_script.h"
#include "../support/fake_runtime.h"

#ifndef MPT_GOLDEN_DIR
#error "MPT_GOLDEN_DIR must point at tests/golden"
#endif

using namespace mpt;

TEST_CASE("FakeRuntime matches the committed cross-platform golden trail") {
  const std::filesystem::path path = std::filesystem::path(MPT_GOLDEN_DIR) / "fake_runtime_v1_party.trail.txt";
  std::ifstream file(path, std::ios::binary);
  REQUIRE_MESSAGE(file.good(), "cannot read " << path.string());
  std::stringstream text;
  text << file.rdbuf();
  const std::optional<test::HashTrail> golden = test::parse_trail(text.str());
  REQUIRE_MESSAGE(golden.has_value(), "malformed golden trail " << path.string());
  REQUIRE(golden->size() == 20);

  // Must match the header of the golden file.
  test::TrailSpec spec;
  spec.config.seed = 1234;
  spec.config.save_media = test::FakeRuntime::make_save_media(5);
  spec.inputs = test::fake_party_script;
  spec.frames = 600;
  spec.interval = 30;
  const test::TrailResult live =
      test::run_fresh_instance([] { return std::make_unique<test::FakeRuntime>(); }, spec);
  REQUIRE(live.status.ok());

  if (test::first_divergence(*golden, live.trail)) {
    FAIL_CHECK("FakeRuntime does not match the golden trail: "
               << test::describe_divergence(*golden, live.trail) << " (golden vs this build)\n"
               << "--- golden (" << path.string() << ")\n"
               << test::format_trail(*golden) << "--- this build\n"
               << test::format_trail(live.trail)
               << "If this platform differs, the hash depends on the platform. If FakeRuntime changed on "
                  "purpose, regenerate the file with mpt_determinism_child --seed 1234 --save-counter 5 "
                  "--party-script --frames 600 --interval 30 and review the diff.");
  }
}
