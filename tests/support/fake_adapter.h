#pragma once

// ITitleAdapter over FakeRuntime (P1-20b2), so Session and the Party Director (P1-04)
// and the conformance suite (P1-20c) have a real adapter to run against.
//
// The catalog is synthetic: "mp4.<kind>.fake_*" IDs with made-up display names, no game
// content. readParty() only copies FakeRuntime::party() into a PartySnapshot, so it is
// cheap and never changes game state. Like the Phase 1 Mp4Adapter (P1-17), it accepts
// only an empty ruleset, ignores writeParty() (the v0 Director never calls it) and
// fails launchMinigame() (Phase 5).

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "fake_runtime.h"
#include "mpt/core/title_adapter.h"

namespace mpt::test {

class FakeAdapter final : public ITitleAdapter {
 public:
  // The adapter must not outlive 'runtime' (ADR 0002 rule 10).
  explicit FakeAdapter(const FakeRuntime& runtime) : runtime_(runtime) {
    board_ = make_id("board", FakeRuntime::kBoardName);
    catalog_.boards.push_back(CatalogEntry{*board_, "Fake Board"});
    for (std::size_t i = 0; i < FakeRuntime::kMinigameNames.size(); ++i) {
      minigames_[i] = make_id("minigame", FakeRuntime::kMinigameNames[i]);
      catalog_.minigames.push_back(CatalogEntry{*minigames_[i], "Fake Minigame " + std::to_string(i + 1)});
    }
    for (std::size_t p = 0; p < kMaxPlayers; ++p) {
      characters_[p] = make_id("character", FakeRuntime::kCharacterNames[p]);
      catalog_.characters.push_back(CatalogEntry{*characters_[p], "Fake Character " + std::to_string(p + 1)});
    }
  }

  // For TitleRegistry::AdapterFactory. Returns null unless 'runtime' is a FakeRuntime.
  static std::unique_ptr<ITitleAdapter> create(ITitleRuntime& runtime) {
    const auto* fake = dynamic_cast<const FakeRuntime*>(&runtime);
    if (!fake) return nullptr;
    return std::make_unique<FakeAdapter>(*fake);
  }

  Catalog catalog() override { return catalog_; }

  Result applyRuleset(const ResolvedRuleset& rules) override {
    // The seed is always set by the host and is fine. Any rule is beyond this title.
    if (rules.board || rules.turn_count || rules.starting_coins || rules.star_cost || rules.starting_stars ||
        rules.bonus_stars || rules.minigame_time_limit_frames || !rules.minigame_pool.empty()) {
      return Result::failure("FakeAdapter supports only an empty ruleset");
    }
    return Result::success();
  }

  PartySnapshot readParty() override {
    const FakeRuntime::Party& party = runtime_.party();
    PartySnapshot snapshot;
    snapshot.phase = party.phase;
    if (party.phase == PartyPhase::none) return snapshot;  // no party: all defaults

    snapshot.board = board_;
    snapshot.max_turns = static_cast<std::int32_t>(FakeRuntime::kMaxTurns);
    snapshot.turn = static_cast<std::int32_t>(party.turn);
    const bool seated = party.phase != PartyPhase::setup;  // characters are chosen when setup ends
    for (std::size_t p = 0; p < kMaxPlayers; ++p) {
      PlayerState& player = snapshot.players[p];
      if (seated) player.character = characters_[p];
      player.coins = party.coins[p];
      player.stars = party.stars[p];
      player.board_space = party.board_space[p];
    }
    // Set exactly when phase == minigame (ADR 0005).
    if (party.phase == PartyPhase::minigame && party.minigame >= 1 && party.minigame <= minigames_.size()) {
      snapshot.current_minigame = minigames_[party.minigame - 1];
    }
    return snapshot;
  }

  void writeParty(const PartySnapshot&) override {}  // unused in Phase 1 (ADR 0005 Director v0)

  Result launchMinigame(const MinigameId&, const MinigameSetup&) override {
    return Result::failure("FakeAdapter cannot launch minigames (Phase 5)");
  }
  std::optional<MinigameResult> pollMinigameResult() override { return std::nullopt; }

 private:
  static ContentId make_id(std::string_view kind, std::string_view name) {
    const std::string text = "mp4." + std::string(kind) + "." + std::string(name);
    return ContentId::parse(text).value();  // the names are constants; a test checks they parse
  }

  const FakeRuntime& runtime_;
  Catalog catalog_;
  std::optional<BoardId> board_;
  std::array<std::optional<MinigameId>, FakeRuntime::kMinigameNames.size()> minigames_{};
  std::array<std::optional<CharacterId>, kMaxPlayers> characters_{};
};

}  // namespace mpt::test
