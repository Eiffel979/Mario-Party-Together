// See mpt/mp4/adapter.h.
#include "mpt/mp4/adapter.h"

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

PartySnapshot Mp4Adapter::readParty() {
  PartySnapshot snap;
  MptMp4Party raw{};
  if (!runtime_.readParty(raw)) return snap;
  snap.phase = phase_of(raw);
  if (snap.phase == PartyPhase::none) return snap;
  snap.turn = raw.turn;
  snap.max_turns = raw.max_turns;
  for (std::size_t i = 0; i < kMaxPlayers; ++i) {
    const MptMp4PartyPlayer& p = raw.players[i];
    snap.players[i].is_cpu = p.is_cpu != 0;
    snap.players[i].coins = p.coins;
    snap.players[i].stars = p.stars;
    snap.players[i].board_space = p.space;
    // character, board and current_minigame IDs: P1-17's catalog mapping.
  }
  return snap;
}

}  // namespace mpt::mp4
