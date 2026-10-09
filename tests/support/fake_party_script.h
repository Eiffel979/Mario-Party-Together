#pragma once

// Input script that drives FakeRuntime through a whole party (P1-20b2), on top of the
// shared synthetic script. Port 0 presses FakeRuntime::kAdvanceButton every 50 frames
// from frame 10, ten times: nine presses take the party from none to finished, and the
// tenth lands on finished and must change nothing. Port 0 also saves at frame 300 and
// toggles the frame rate at frames 200 and 450, so one run covers every part of State.
// Used by the golden trail, the FakeAdapter tests and Session/Director tests (P1-04).

#include <cstdint>

#include "determinism_harness.h"
#include "fake_runtime.h"

namespace mpt::test {

inline constexpr std::uint64_t kPartyScriptFirstAdvance = 10;
inline constexpr std::uint64_t kPartyScriptAdvanceEvery = 50;
inline constexpr std::uint64_t kPartyScriptAdvances = 10;
inline constexpr std::uint64_t kPartyScriptSaveFrame = 300;
inline constexpr std::uint64_t kPartyScriptRateFrames[] = {200, 450};

inline InputFrame fake_party_script(std::uint64_t frame) {
  InputFrame in = scripted_input(frame);
  std::uint32_t& buttons = in.pads[0].buttons;
  if (frame >= kPartyScriptFirstAdvance) {
    const std::uint64_t since = frame - kPartyScriptFirstAdvance;
    if (since % kPartyScriptAdvanceEvery == 0 && since / kPartyScriptAdvanceEvery < kPartyScriptAdvances) {
      buttons |= FakeRuntime::kAdvanceButton;
    }
  }
  if (frame == kPartyScriptSaveFrame) buttons |= FakeRuntime::kSaveButton;
  for (std::uint64_t f : kPartyScriptRateFrames) {
    if (frame == f) buttons |= FakeRuntime::kRateToggleButton;
  }
  return in;
}

}  // namespace mpt::test
