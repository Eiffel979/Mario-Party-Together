/* Reads of game state for the C ABI (party mirroring and the game part of the state hash). Compiled with
 * the game's headers; called between frame steps only. */
#include <string.h>

#include "dolphin.h"
#include "game/dvd.h"
#include "game/gamework_data.h"
#include "game/object.h"

#include "mpt/mp4/game_abi.h"
#include "mpt_mp4_reads.h"

extern FileListEntry _ovltbl[];
extern s32 rnd_seed;

void mpt_mp4_read_party(MptMp4Party* out) {
  out->overlay = (omcurovl >= 0 && omcurovl < DLL_MAX) ? (int32_t)omcurovl : -1;
  memset(out->overlay_name, 0, sizeof out->overlay_name);
  if (out->overlay >= 0 && _ovltbl[out->overlay].name != NULL) {
    strncpy(out->overlay_name, _ovltbl[out->overlay].name, sizeof out->overlay_name - 1);
  }
  out->board = GWSystem.board;
  out->turn = GWSystem.turn;
  out->max_turns = GWSystem.max_turn;
  for (int i = 0; i < 4; ++i) {
    MptMp4PartyPlayer* p = &out->players[i];
    p->character = GWPlayerCfg[i].character;
    p->is_cpu = GWPlayerCfg[i].iscom != 0;
    p->coins = GWPlayer[i].coins;
    p->stars = GWPlayer[i].stars;
    p->space = out->board >= 0 ? GWPlayer[i].space_curr : -1;
  }
}

/* The game work blocks hold no pointers, so they are hashed as bytes. A first part of MP4's hash
 * manifest; P1-16 completes it (ADR 0008 section 8). */
void mpt_mp4_hash_game(MptMp4HashFn update, void* user) {
  update(user, GWPlayerCfg, sizeof GWPlayerCfg);
  update(user, GWPlayer, sizeof GWPlayer);
  update(user, &GWSystem, sizeof GWSystem);
  update(user, &GWGameStat, sizeof GWGameStat);
  update(user, &rnd_seed, sizeof rnd_seed);
  const int32_t ovl = (int32_t)omcurovl;
  update(user, &ovl, sizeof ovl);
}
