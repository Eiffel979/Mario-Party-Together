// See mpt/mp4/adapter.h.
#include "mpt/mp4/adapter.h"

#include "catalog.h"

#include <cstring>

namespace mpt::mp4 {
namespace {

bool is_minigame(const char* name) {
  // The minigame overlays: m401Dll..m463Dll (mp4.md section 12). m300..m333 are empty placeholders.
  return name[0] == 'm' && name[1] == '4' && name[2] >= '0' && name[2] <= '9' && name[3] >= '0' && name[3] <= '9';
}

}  // namespace

PartyPhase phase_of(const MptMp4Party& party) {
  const std::uint32_t f = party.flags;
  if (!(f & MPT_MP4_PARTY_MODE)) return PartyPhase::none;
  if (f & MPT_MP4_PARTY_RESULTS_FINAL) return PartyPhase::finished;
  if (f & MPT_MP4_PARTY_TURNS_OVER) return PartyPhase::results;
  if (f & MPT_MP4_PARTY_IN_PROGRESS) return is_minigame(party.overlay_name) ? PartyPhase::minigame : PartyPhase::board;
  if (f & MPT_MP4_PARTY_IN_SETUP) return PartyPhase::setup;
  return PartyPhase::none;
}

Result Mp4Adapter::applyRuleset(const ResolvedRuleset& rules) {
  if (rules.board || rules.turn_count || rules.starting_coins || rules.star_cost || rules.starting_stars ||
      rules.bonus_stars || rules.minigame_time_limit_frames || !rules.minigame_pool.empty()) {
    return Result::failure("Mario Party 4 supports only an empty ruleset in Phase 1");
  }
  return Result::success();
}

Catalog Mp4Adapter::catalog() { return mp4_catalog(); }

PartySnapshot Mp4Adapter::readParty() {
  MptMp4Party raw{};
  if (!runtime_.readParty(raw)) return PartySnapshot{};
  return snapshot_of(raw);
}

PartySnapshot snapshot_of(const MptMp4Party& raw) {
  PartySnapshot snap;
  snap.phase = phase_of(raw);
  if (snap.phase == PartyPhase::none) return snap;
  snap.max_turns = raw.max_turns;
  // The game moves GWSystem.turn one past max_turn when the last turn ends (BoardTurnNext); the
  // snapshot keeps turn within max_turns (ADR 0005), so the results phases report the last turn.
  snap.turn = (raw.max_turns > 0 && raw.turn > raw.max_turns) ? raw.max_turns : raw.turn;
  // GWSystem.board holds a value from the first board on; it names the party's board once a board has
  // run (in progress, or the end-of-party phases after it).
  if ((raw.flags & (MPT_MP4_PARTY_IN_PROGRESS | MPT_MP4_PARTY_TURNS_OVER)) != 0) snap.board = board_for(raw.board);
  for (std::size_t i = 0; i < kMaxPlayers; ++i) {
    const MptMp4PartyPlayer& p = raw.players[i];
    snap.players[i].is_cpu = p.is_cpu != 0;
    snap.players[i].coins = p.coins;
    snap.players[i].stars = p.stars;
    snap.players[i].board_space = p.space;
    snap.players[i].character = character_for(p.character);
  }
  // ADR 0005: current_minigame is set exactly in the minigame phase. phase_of classifies the overlay
  // by its name; one the catalog does not list (m452 and m454 point back at m450 and have no row) is
  // reported as part of the board phase.
  if (snap.phase == PartyPhase::minigame) {
    snap.current_minigame = minigame_for_overlay(raw.overlay_name);
    if (!snap.current_minigame) snap.phase = PartyPhase::board;
  }
  return snap;
}

}  // namespace mpt::mp4
