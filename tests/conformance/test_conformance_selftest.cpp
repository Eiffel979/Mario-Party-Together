// Self-tests for the conformance suite: each runtime or adapter below breaks one rule,
// and the matching check must report it. Without these, a check that never fails would
// look like a passing runtime.

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "../support/delegating_runtime.h"
#include "../support/fake_adapter.h"
#include "../support/fake_party_script.h"
#include "../support/fake_runtime.h"
#include "conformance.h"

using namespace mpt;
using test::DelegatingRuntime;
using test::FakeRuntime;

namespace {

template <typename Runtime>
conformance::Target target_with(conformance::AdapterFactory adapter = {}) {
  conformance::Target t;
  t.name = "broken";
  t.runtime = []() -> std::unique_ptr<ITitleRuntime> { return std::make_unique<Runtime>(); };
  t.adapter = std::move(adapter);
  t.config.seed = 1234;
  t.config.save_media = FakeRuntime::make_save_media(5);
  t.inputs = test::fake_party_script;
  t.frames = 600;
  t.interval = 30;
  t.save_frame = 330;
  t.expect_save_write = true;
  return t;
}

bool mentions(const conformance::Problems& problems, const std::string& text) {
  for (const std::string& p : problems) {
    if (p.find(text) != std::string::npos) return true;
  }
  return false;
}

class BadRateRuntime final : public DelegatingRuntime {
 public:
  FrameRate frameRate() const override { return FrameRate{60, 0}; }
};

class OffByOneSaveFrameRuntime final : public DelegatingRuntime {
 public:
  SaveState saveState() override {
    SaveState state = inner_.saveState();
    ++state.frame;
    return state;
  }
};

class ScaleLeaksRuntime final : public DelegatingRuntime {
 public:
  Result boot(const VerifiedDump& dump, const RuntimeConfig& config) override {
    scale_ = config.internal_scale;
    return inner_.boot(dump, config);
  }
  std::uint64_t stateHash() override { return inner_.stateHash() ^ scale_; }

 private:
  std::uint64_t scale_ = 0;
};

class ForgetsGenerationOnLoadRuntime final : public DelegatingRuntime {
 public:
  Result loadState(const SaveState& state) override {
    loaded_ = true;
    return inner_.loadState(state);
  }
  std::uint64_t saveMediaGeneration() const override { return loaded_ ? 0 : inner_.saveMediaGeneration(); }

 private:
  bool loaded_ = false;
};

// Like a game that reads the raw stick value instead of folding -32768 first.
class NoStickFoldRuntime final : public DelegatingRuntime {
 public:
  void stepFrame(const InputFrame& inputs) override {
    for (const PadState& pad : inputs.pads) {
      if (pad.stick_x == -32768) ++raw_minimum_;
    }
    inner_.stepFrame(inputs);
  }
  std::uint64_t stateHash() override {
    StateHasher hasher;
    hasher.update_u64(inner_.stateHash());
    hasher.update_u64(raw_minimum_);
    return hasher.digest();
  }

 private:
  std::uint64_t raw_minimum_ = 0;
};

class GpuTextureRuntime final : public DelegatingRuntime {
 public:
  FrameOutput video() override {
    FrameOutput out;
    out.kind = FrameOutput::Kind::gpu_texture;
    out.gpu_texture = 1;
    return out;
  }
};

// Reuses its frame buffer for scratch work in a read call, invalidating the view early.
class UnstableViewRuntime final : public DelegatingRuntime {
 public:
  FrameOutput video() override {
    FrameOutput out;
    out.kind = FrameOutput::Kind::cpu_rgba8;
    out.width = 2;
    out.height = 1;
    out.pixels = buffer_;
    return out;
  }
  std::uint64_t stateHash() override {
    ++buffer_[0];
    return inner_.stateHash();
  }

 private:
  std::array<std::uint32_t, 2> buffer_{};
};

// Leaves current_minigame set after the minigame ends.
class StaleMinigameAdapter final : public ITitleAdapter {
 public:
  explicit StaleMinigameAdapter(const FakeRuntime& runtime) : inner_(runtime) {}
  Catalog catalog() override { return inner_.catalog(); }
  Result applyRuleset(const ResolvedRuleset& rules) override { return inner_.applyRuleset(rules); }
  PartySnapshot readParty() override {
    PartySnapshot snapshot = inner_.readParty();
    if (snapshot.current_minigame) last_ = snapshot.current_minigame;
    if (snapshot.phase == PartyPhase::board && last_) snapshot.current_minigame = last_;
    return snapshot;
  }
  void writeParty(const PartySnapshot& party) override { inner_.writeParty(party); }
  Result launchMinigame(const MinigameId& id, const MinigameSetup& setup) override {
    return inner_.launchMinigame(id, setup);
  }
  std::optional<MinigameResult> pollMinigameResult() override { return inner_.pollMinigameResult(); }

 private:
  test::FakeAdapter inner_;
  std::optional<MinigameId> last_;
};

}  // namespace

TEST_CASE("conformance self-test: an invalid frame rate is caught") {
  CHECK(mentions(conformance::check_frame_rate(target_with<BadRateRuntime>()), "invalid frame rate"));
}

TEST_CASE("conformance self-test: a wrong SaveState::frame is caught") {
  CHECK(mentions(conformance::check_frame_numbering(target_with<OffByOneSaveFrameRuntime>()), "SaveState::frame"));
}

TEST_CASE("conformance self-test: a presentation option in the hash is caught") {
  CHECK(mentions(conformance::check_presentation_options(target_with<ScaleLeaksRuntime>()), "internal_scale"));
}

TEST_CASE("conformance self-test: a generation not restored by loadState is caught") {
  CHECK(mentions(conformance::check_save_load(target_with<ForgetsGenerationOnLoadRuntime>()), "ADR 0004 rule 9"));
}

TEST_CASE("conformance self-test: an unfolded -32768 stick is caught") {
  CHECK(mentions(conformance::check_stick_folding(target_with<NoStickFoldRuntime>()), "-32768"));
}

TEST_CASE("conformance self-test: a GPU texture frame is caught") {
  CHECK(mentions(conformance::check_views(target_with<GpuTextureRuntime>()), "GPU texture"));
}

TEST_CASE("conformance self-test: a view invalidated by a read call is caught") {
  CHECK(mentions(conformance::check_views(target_with<UnstableViewRuntime>()), "rule 6"));
}

TEST_CASE("conformance self-test: a stale current_minigame is caught") {
  conformance::Target t = target_with<FakeRuntime>([](ITitleRuntime& runtime) -> std::unique_ptr<ITitleAdapter> {
    return std::make_unique<StaleMinigameAdapter>(dynamic_cast<const FakeRuntime&>(runtime));
  });
  CHECK(mentions(conformance::check_adapter(t), "current_minigame is set outside the minigame phase"));
}

TEST_CASE("conformance self-test: a missing save or minigame phase is caught when expected") {
  conformance::Target t = target_with<FakeRuntime>(test::FakeAdapter::create);
  t.inputs = test::scripted_input;  // never saves, never starts a party
  t.expect_minigame_phase = true;
  CHECK(mentions(conformance::check_save_media(t), "expected to commit a save"));
  CHECK(mentions(conformance::check_adapter(t), "expected to reach the minigame phase"));
}

TEST_CASE("conformance self-test: the checks pass for the unmodified FakeRuntime") {
  const conformance::Target t = target_with<DelegatingRuntime>();
  CHECK(conformance::check_frame_rate(t).empty());
  CHECK(conformance::check_frame_numbering(t).empty());
  CHECK(conformance::check_save_load(t).empty());
  CHECK(conformance::check_views(t).empty());
  CHECK(conformance::check_presentation_options(t).empty());
  CHECK(conformance::check_save_media(t).empty());
  CHECK(conformance::check_stick_folding(t).empty());
}
