// Runs the conformance suite (P1-20c) against FakeRuntime: once with FakeAdapter, a saved
// image and the whole-party script, and once with blank media, no adapter and the plain
// synthetic script, so the optional paths of the suite run too.

#include <memory>

#include "../support/fake_adapter.h"
#include "../support/fake_party_script.h"
#include "../support/fake_runtime.h"
#include "conformance_tests.h"

namespace {

std::unique_ptr<mpt::ITitleRuntime> make_fake() { return std::make_unique<mpt::test::FakeRuntime>(); }

mpt::conformance::Target fake_party_target() {
  using mpt::test::FakeRuntime;
  mpt::conformance::Target t;
  t.name = "FakeRuntime + FakeAdapter";
  t.runtime = make_fake;
  t.adapter = mpt::test::FakeAdapter::create;
  t.config.seed = 1234;
  t.config.save_media = FakeRuntime::make_save_media(5);
  t.inputs = mpt::test::fake_party_script;
  t.frames = 600;
  t.interval = 30;
  t.save_frame = 330;  // after the save at frame 300, inside the last minigame
  t.other_save_media = FakeRuntime::make_save_media(6);
  t.expect_save_write = true;
  t.expect_minigame_phase = true;
  return t;
}

mpt::conformance::Target fake_plain_target() {
  mpt::conformance::Target t;
  t.name = "FakeRuntime (blank media, no adapter)";
  t.runtime = make_fake;
  t.config.seed = 99;
  t.inputs = mpt::test::scripted_input;
  return t;
}

}  // namespace

MPT_CONFORMANCE_SUITE("FakeRuntime party", fake_party_target)
MPT_CONFORMANCE_SUITE("FakeRuntime plain", fake_plain_target)
