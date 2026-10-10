// Test harness: runs a synthetic "game" (a C++ lambda) as the SDK's root
// coroutine. Synthetic data only.
//
// Inside game code use CHECK, never REQUIRE: REQUIRE throws, and exceptions
// must not unwind across a coroutine boundary.
#pragma once

#include <doctest/doctest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mpt/gc/sdk.h"

namespace mpt::gc::test {

struct Game {
  std::function<void()> body;
};

inline void gameMain(void* arg) { static_cast<Game*>(arg)->body(); }

inline std::unique_ptr<Sdk> boot(Game& game, SdkConfig config = {}) {
  std::unique_ptr<Sdk> sdk;
  const Result r = Sdk::create(config, &gameMain, &game, sdk);
  REQUIRE_MESSAGE(r.ok(), r.error());
  return sdk;
}

inline StepStatus step(Sdk& sdk, InputFrame input = {}) {
  input.frame = sdk.frame();
  return sdk.stepFrame(input);
}

inline void stepN(Sdk& sdk, int n) {
  for (int i = 0; i < n; ++i) REQUIRE(step(sdk) == StepStatus::running);
}

inline PadState connectedPad(std::uint32_t buttons = 0) {
  PadState p;
  p.connected = 1;
  p.buttons = buttons;
  return p;
}

// Event log written by game code.
struct Log {
  std::vector<std::string> lines;
  void add(std::string s) { lines.push_back(std::move(s)); }
  std::string joined() const {
    std::string out;
    for (const auto& l : lines) out += (out.empty() ? "" : " ") + l;
    return out;
  }
};

}  // namespace mpt::gc::test
