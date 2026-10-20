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

static const char* overlay_name(int32_t ovl) {
  return (ovl >= 0 && ovl < DLL_MAX) ? _ovltbl[ovl].name : NULL;
}

/* A board overlay: w01Dll..w06Dll (party boards) and the other wNN boards. */
static int is_board(const char* name) {
  return name != NULL && name[0] == 'w' && name[1] >= '0' && name[1] <= '9' && name[2] >= '0' && name[2] <= '9';
}

/* Whether a board is anywhere in the overlay history (omOvlHisGet: level 0 is the newest). Read
 * directly from the history array through omOvlHisGet, which only reads. */
static int board_in_history(void) {
  for (s32 level = 0; level <= omovlhisidx; ++level) {
    const omOvlHisData* his = omOvlHisGet(level);
    if (his != NULL && is_board(overlay_name(his->overlay))) return 1;
  }
  return 0;
}

void mpt_mp4_read_party(MptMp4Party* out) {
  out->overlay = (omcurovl >= 0 && omcurovl < DLL_MAX) ? (int32_t)omcurovl : -1;
  memset(out->overlay_name, 0, sizeof out->overlay_name);
  if (out->overlay >= 0 && _ovltbl[out->overlay].name != NULL) {
    strncpy(out->overlay_name, _ovltbl[out->overlay].name, sizeof out->overlay_name - 1);
  }
  uint32_t flags = 0;
  if (GWSystem.party) flags |= MPT_MP4_PARTY_MODE;
  const char* running = overlay_name(out->overlay);
  if (GWSystem.party && running != NULL && strcmp(running, "mentDll") == 0) flags |= MPT_MP4_PARTY_IN_SETUP;
  const int on_board = GWSystem.party && board_in_history();
  if (on_board) flags |= MPT_MP4_PARTY_IN_PROGRESS;
  if (GWSystem.party && GWSystem.max_turn > 0 && GWSystem.turn > GWSystem.max_turn) {
    flags |= MPT_MP4_PARTY_TURNS_OVER;
    if (!on_board) flags |= MPT_MP4_PARTY_RESULTS_FINAL;
  }
  out->flags = flags;
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

/* The running overlay's name, for the copy-destination trap's allow-list (NULL before the first). */
const char* mpt_mp4_current_overlay_name(void) {
  if (omcurovl < 0 || omcurovl >= DLL_MAX) {
    return NULL;
  }
  return _ovltbl[omcurovl].name;
}
