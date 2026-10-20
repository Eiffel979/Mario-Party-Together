#pragma once

// Forwards every ITitleRuntime method (contract v1) to a FakeRuntime, so tests can change
// one behaviour: the harness tests, and the conformance self-tests that check each
// conformance check catches the bug it is meant to catch.

#include <cstdint>

#include "fake_runtime.h"

namespace mpt::test {

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
  RuntimeHealth status() const override { return inner_.status(); }

  const FakeRuntime& inner() const { return inner_; }

 protected:
  FakeRuntime inner_;
};

}  // namespace mpt::test
