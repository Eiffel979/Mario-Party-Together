// Mp4Adapter: ITitleAdapter for Mario Party 4. Host side, reading the game through Mp4Runtime's raw
// party reads (MptMp4Party).
//
// P1-16 scope: enough for registration and phase mirroring. P1-17 adds the catalog (content IDs and
// display names from docs/titles/mp4.md section 12), the character/board/minigame ID mapping and the
// dump-gated party fixture test.
#pragma once

#include "mpt/core/title_adapter.h"
#include "mpt/mp4/runtime.h"

namespace mpt::mp4 {

class Mp4Adapter final : public ITitleAdapter {
 public:
  explicit Mp4Adapter(Mp4Runtime& runtime) : runtime_(runtime) {}

  Catalog catalog() override { return {}; }  // P1-17
  // Phase 1: only an empty ruleset (the seed is always set by the host).
  Result applyRuleset(const ResolvedRuleset& rules) override;
  PartySnapshot readParty() override;
  void writeParty(const PartySnapshot&) override {}  // unused in Phase 1
  Result launchMinigame(const MinigameId&, const MinigameSetup&) override {
    return Result::failure("Mario Party 4 cannot launch a minigame on request yet");
  }
  std::optional<MinigameResult> pollMinigameResult() override { return std::nullopt; }

 private:
  Mp4Runtime& runtime_;
};

// The PartyPhase for the module's raw party flags and running overlay (docs/titles/mp4.md section 18).
PartyPhase phase_of(const MptMp4Party& party);

}  // namespace mpt::mp4
