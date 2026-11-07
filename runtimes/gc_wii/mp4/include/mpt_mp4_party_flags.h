/* MP4's party flags (MptMp4Party.flags) from facts read in game memory (src/game_reads.c). A pure
 * function, so the host tests can check it on synthetic facts (tests/unit/test_mp4_host.cpp). */
#ifndef MPT_MP4_PARTY_FLAGS_H
#define MPT_MP4_PARTY_FLAGS_H

#include <stdint.h>

#include "mpt/mp4/game_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What game_reads.c reads. `heading` is the overlay the game is in or switching to: the running one,
 * or the next one while omcurovl is DLL_NONE between two overlays. */
typedef enum MptMp4Heading {
  MPT_MP4_HEADING_OTHER = 0,
  MPT_MP4_HEADING_SETUP = 1,         /* mentDll, party setup */
  MPT_MP4_HEADING_BONUS_STARS = 2,   /* mstory3Dll event 0: the end-of-party board result (bonus stars) */
  MPT_MP4_HEADING_FINAL_RESULTS = 3  /* mstory3Dll event 1: the final ranking */
} MptMp4Heading;

typedef struct MptMp4PartyFacts {
  int party;            /* GWSystem.party */
  int32_t turn;         /* GWSystem.turn */
  int32_t max_turn;     /* GWSystem.max_turn */
  int setup_running;    /* the running overlay (omcurovl) is mentDll */
  int board_in_history; /* a board overlay is in the overlay history */
  MptMp4Heading heading;
} MptMp4PartyFacts;

/* The flags (docs/titles/mp4.md section 21):
 * - MODE: party mode. IN_SETUP: mentDll runs. IN_PROGRESS: a board is in the history.
 * - TURNS_OVER: the last turn has ended. On a party board the game never moves GWSystem.turn past
 *   max_turn: on the last turn mg_setup.c sets flag (0,10), and when the board comes back from that
 *   minigame it kills itself at once (board/main.c MainFunc) and returns to mstory3Dll, which mentDll
 *   put in its history slot. So the turns are over once turn == max_turn and the board has left the
 *   history, outside mentDll (where a new party starts). The Extra Room boards do move turn past
 *   max_turn (BoardTurnNext), which counts too.
 * - RESULTS_FINAL: turns over, no board in the history, and not on the bonus-star event: the final
 *   ranking, and everything after it until a new party's setup. */
static inline uint32_t mpt_mp4_party_flags(const MptMp4PartyFacts* f) {
  uint32_t flags = 0;
  if (!f->party) return 0;
  flags |= MPT_MP4_PARTY_MODE;
  if (f->setup_running) flags |= MPT_MP4_PARTY_IN_SETUP;
  if (f->board_in_history) flags |= MPT_MP4_PARTY_IN_PROGRESS;
  if (f->max_turn <= 0 || f->heading == MPT_MP4_HEADING_SETUP) return flags;
  if (f->turn > f->max_turn || (f->turn == f->max_turn && !f->board_in_history)) {
    flags |= MPT_MP4_PARTY_TURNS_OVER;
    if (!f->board_in_history && f->heading != MPT_MP4_HEADING_BONUS_STARS) flags |= MPT_MP4_PARTY_RESULTS_FINAL;
  }
  return flags;
}

#ifdef __cplusplus
}
#endif

#endif /* MPT_MP4_PARTY_FLAGS_H */
