/* Test only (M4-07, E2 game-side proof, docs/titles/mp4.md section 18): a direct start of one minigame.
 * See mpt_mp4_port.h. Compiled with the game's headers but without game_section.h: the configuration
 * below is host state set before boot, outside the game data range, so it is neither saved nor hashed
 * (the host folds the request into the hashed gameplay word instead, abi.cpp). This file holds no game
 * data and adds no symbol to the game data range, so a normal boot's state hash cannot move. */
#include <stddef.h>

#include "dolphin.h"
#include "game/audio.h"
#include "game/chrman.h"
#include "game/gamework_data.h"
#include "game/object.h"
#include "game/minigame_seq.h"
#include "game/objsub.h"
#include "game/window.h"

#include "mpt_mp4_port.h"

extern FileListEntry _ovltbl[];
extern s32 SystemInitF; /* game/main.c */

static int s_minigame; /* 0: a normal boot */
static int s_focus;

void mpt_mp4_test_start_config(int minigame, int focus) {
  s_minigame = minigame;
  s_focus = focus & 3;
}

/* The overlay named m<number>Dll, compared without case (the list has both "Dll" and "dll"). */
static int overlay_of(int number) {
  char want[8] = {'m', (char)('0' + number / 100 % 10), (char)('0' + number / 10 % 10), (char)('0' + number % 10),
                  'd', 'l', 'l', 0};
  for (int i = 0; i < DLL_MAX; ++i) {
    const char* name = _ovltbl[i].name;
    if (name == NULL) continue;
    int k = 0;
    for (; k < 7; ++k) {
      char c = name[k];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if (c != want[k]) break;
    }
    if (k == 7 && name[7] == 0) return i;
  }
  return -1;
}

int mpt_mp4_test_start(int normal) {
  if (s_minigame == 0) return normal;
  const int ovl = overlay_of(s_minigame);
  const s32 mg = ovl < 0 ? -1 : omMgIndexGet((s16)ovl);
  if (mg < 0) return normal; /* not a minigame: the host sees bootDll and fails the run */
  const int type = mgInfoTbl[mg].type;
  const int focus = s_focus;
  /* Seated categories (1-vs-3, 2-vs-2, Bowser, Fortune, Item) give the minigame's roles by config slot:
   * the single seat is slot 0 (group 0), and some item minigames create only slots 0 and 1. So the pads
   * rotate: slot i is played on pad (i + focus) % 4, and the focus pad takes slot 0. Every other category
   * keeps slot i on pad i. Characters stay slot i's. */
  const int seated = type == 1 || type == 2 || type == 3 || type == 5 || type == 6;
  for (int i = 0; i < 4; ++i) {
    GWPlayerCfg[i].character = (s16)i;
    GWPlayerCfg[i].pad_idx = (s16)(seated ? (i + focus) % 4 : i);
    GWPlayerCfg[i].iscom = 0;
    GWPlayerCfg[i].diff = 0;
    if (type == 2) {
      GWPlayerCfg[i].group = (s16)(i < 2 ? 0 : 1); /* 2-vs-2: slots 0 and 1 are team 0 */
    } else if (seated) {
      GWPlayerCfg[i].group = (s16)(i == 0 ? 0 : 1); /* the single seat is slot 0 (board/mg_setup.c) */
    } else {
      GWPlayerCfg[i].group = (s16)i; /* 4-player and Battle: every player on their own */
    }
  }
  GWSystem.player_curr = 0; /* the focus pad's slot in a seated category */
  GWSystem.mg_next = (s16)mg;
  /* The system setup bootDll does once before any minigame can run (bootDll's
   * BootInitForSkippedSequence): characters, windows, the minigame sequence data (the start and finish
   * words live in ARAM) and the sound group. */
  CharInit();
  HuWindowInit();
  MGSeqInit();
  HuWinInit(1);
  HuAudSndGrpSetSet(0);
  SystemInitF = TRUE;
  for (int i = 0; i < 4; ++i) CharMotionInit((s16)i);
  return DLL_instdll;
}
