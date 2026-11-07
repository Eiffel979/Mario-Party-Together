// Mp4Adapter: ITitleAdapter for Mario Party 4. Host side, reading the game through Mp4Runtime's raw
// party reads (MptMp4Party).
//
// The catalog is docs/titles/mp4.md section 12: 8 boards, 61 minigames and 8 characters as mp4.* content
// IDs (P1-17). readParty maps GWSystem.board, each player's character and the running minigame overlay
// to those IDs.
#pragma once

#include <cstdint>
#include <optional>

#include "mpt/core/title_adapter.h"
#include "mpt/mp4/runtime.h"

namespace mpt::mp4 {

class Mp4Adapter final : public ITitleAdapter {
 public:
  explicit Mp4Adapter(Mp4Runtime& runtime) : runtime_(runtime) {}

  Catalog catalog() override;
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

// The PartyPhase for the module's raw party flags and running overlay (docs/titles/mp4.md section 18):
// the raw classifier, which calls any m4NN overlay a minigame.
PartyPhase phase_of(const MptMp4Party& party);

// The whole snapshot readParty() returns for a raw party read: phase_of, content IDs for the board,
// the characters and the running minigame (ADR 0005: current_minigame exactly in the minigame phase;
// an uncatalogued minigame overlay counts as the board phase), and the turn kept within max_turns.
PartySnapshot snapshot_of(const MptMp4Party& party);

// Content IDs for game values (docs/titles/mp4.md section 12), nullopt for an uncatalogued value: the
// tutorial board, an empty character slot, an overlay that is not a catalogued minigame.
std::optional<BoardId> board_for(std::int32_t gw_board);
std::optional<CharacterId> character_for(std::int32_t character);
std::optional<MinigameId> minigame_for_overlay(const char* overlay_name);

}  // namespace mpt::mp4
