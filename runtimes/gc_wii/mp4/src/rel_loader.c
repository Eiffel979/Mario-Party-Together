/* REL linking inside the game module: see mpt_mp4_rel.h. */
#include "mpt_mp4_rel.h"

#include <stdlib.h>
#include <string.h>

/* Pristine copies of each REL's data range, in table order. Host memory: constant after init and not
 * game state, so it is not part of a save state. */
static unsigned char** s_pristine = 0;

int mpt_mp4_rel_init(void) {
  if (s_pristine != 0) {
    return 1;
  }
  s_pristine = (unsigned char**)calloc(mpt_mp4_rel_count ? mpt_mp4_rel_count : 1, sizeof(unsigned char*));
  if (s_pristine == 0) {
    return 0;
  }
  for (unsigned i = 0; i < mpt_mp4_rel_count; ++i) {
    const MptMp4RelInfo* rel = mpt_mp4_rel_table[i];
    if (rel->data_lo == 0 || rel->data_hi <= rel->data_lo) {
      continue;
    }
    const size_t size = (size_t)(rel->data_hi - rel->data_lo);
    s_pristine[i] = (unsigned char*)malloc(size);
    if (s_pristine[i] == 0) {
      return 0;
    }
    memcpy(s_pristine[i], rel->data_lo, size);
  }
  return 1;
}

const MptMp4RelInfo* mpt_mp4_rel_link(const char* name) {
  if (name == 0 || s_pristine == 0) {
    return 0;
  }
  for (unsigned i = 0; i < mpt_mp4_rel_count; ++i) {
    const MptMp4RelInfo* rel = mpt_mp4_rel_table[i];
    if (strcmp(rel->name, name) != 0) {
      continue;
    }
    if (s_pristine[i] != 0) {
      memcpy(rel->data_lo, s_pristine[i], (size_t)(rel->data_hi - rel->data_lo));
    }
    return rel;
  }
  return 0;
}

void mpt_mp4_rel_object_setup(const MptMp4RelInfo* rel) {
  if (rel != 0 && rel->object_setup != 0) {
    rel->object_setup();
  }
}

void mpt_mp4_rel_unlink(const MptMp4RelInfo* rel) {
  (void)rel; /* nothing to free: the REL stays mapped, and its data is restored on the next link */
}

/* The game core's pristine image, taken on the first call (before any game code has run) and restored
 * on every later one: a fresh boot, whether a new game in the same process or a soft reset. */
static unsigned char* s_game_pristine = 0;

int mpt_mp4_game_data_reset(void) {
  unsigned char* lo = mpt_mp4_game_data_lo();
  unsigned char* hi = mpt_mp4_game_data_hi();
  if (lo == 0 || hi <= lo) {
    return 1;
  }
  const size_t size = (size_t)(hi - lo);
  if (s_game_pristine == 0) {
    s_game_pristine = (unsigned char*)malloc(size);
    if (s_game_pristine == 0) {
      return 0;
    }
    memcpy(s_game_pristine, lo, size);
    return 1;
  }
  memcpy(lo, s_game_pristine, size);
  return 1;
}
