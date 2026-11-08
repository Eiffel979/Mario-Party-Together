#pragma once

// Test-only shell registration (P1-06): FakeRuntime + FakeAdapter as "mp4", with a
// synthetic VerifiedDump so no real dump is ever needed. Used by mpt_shell_fake and by
// the replay generator for the shell smoke tests.

#include <memory>

#include "../support/fake_adapter.h"
#include "../support/fake_party_script.h"
#include "../support/fake_runtime.h"
#include "titles.h"

namespace mpt::test {

inline VerifiedDump fake_shell_dump() {
  VerifiedDump dump;
  dump.title = TitleId::mp4;
  dump.region = "USA";
  dump.revision = "1.0";
  dump.sha256 = "f00df00df00df00df00df00df00df00df00df00df00df00df00df00df00df00d";
  return dump;
}

inline shell::ShellTitle fake_shell_title() {
  shell::ShellTitle title;
  title.id = TitleId::mp4;
  title.runtime = [] { return std::make_unique<FakeRuntime>(); };
  title.adapter = &FakeAdapter::create;
  title.synthetic_dump = fake_shell_dump();
  return title;
}

// M4-06: a FakeRuntime that plays the whole-party script (fake_party_script.h) on its
// own, whatever the shell samples, so a headless *live* session with no devices reaches
// `finished`. The shell still records the inputs it sampled, and the replay verifies on
// this same registration, because the script is a pure function of the frame number.
// Test-only: real runtimes never ignore their inputs.
class ScriptedPartyRuntime final : public ITitleRuntime {
 public:
  const FakeRuntime& inner() const { return inner_; }

  TitleId id() const override { return inner_.id(); }
  Result boot(const VerifiedDump& dump, const RuntimeConfig& config) override { return inner_.boot(dump, config); }
  void stepFrame(const InputFrame& inputs) override {
    InputFrame scripted = fake_party_script(inputs.frame);
    scripted.frame = inputs.frame;
    inner_.stepFrame(scripted);
  }
  FrameOutput video() override { return inner_.video(); }
  AudioChunk audio() override { return inner_.audio(); }
  SaveState saveState() override { return inner_.saveState(); }
  Result loadState(const SaveState& state) override { return inner_.loadState(state); }
  std::uint64_t stateHash() override { return inner_.stateHash(); }
  FrameRate frameRate() const override { return inner_.frameRate(); }
  std::uint64_t saveMediaGeneration() const override { return inner_.saveMediaGeneration(); }
  SaveMedia saveMedia() const override { return inner_.saveMedia(); }
  RuntimeHealth status() const override { return inner_.status(); }

 private:
  FakeRuntime inner_;
};

inline shell::ShellTitle fake_party_shell_title() {
  shell::ShellTitle title = fake_shell_title();
  title.runtime = [] { return std::make_unique<ScriptedPartyRuntime>(); };
  title.adapter = [](ITitleRuntime& runtime) -> std::unique_ptr<ITitleAdapter> {
    const auto* scripted = dynamic_cast<const ScriptedPartyRuntime*>(&runtime);
    if (!scripted) return nullptr;
    return std::make_unique<FakeAdapter>(scripted->inner());
  };
  return title;
}

}  // namespace mpt::test
