/* Reads of game state for the C ABI (party mirroring and the game part of the state hash). Compiled with
 * the game's headers; called between frame steps only. */
#include <string.h>

#include "dolphin.h"
#include "game/dvd.h"
#include "game/gamework_data.h"
#include "game/memory.h"
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
    size_t n = strlen(_ovltbl[out->overlay].name);
    if (n > sizeof out->overlay_name - 1) n = sizeof out->overlay_name - 1;
    memcpy(out->overlay_name, _ovltbl[out->overlay].name, n); /* the rest was zeroed above */
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

/* HuMem's block header (src/game/memory.c keeps it private). Party Board's 64-bit layout: the data
 * starts 64 bytes after the header (BLOCK_GET_DATA), and flag != 0 marks an allocated block. */
struct mpt_humem_block {
  s32 size; /* including the 64-byte header */
  u8 magic;
  u8 flag;
  struct mpt_humem_block* prev;
  struct mpt_humem_block* next;
  uintptr_t num;
  uintptr_t retaddr;
};
#define MPT_HUMEM_HEADER 64u

static int humem_heap_index(const void* base) {
  for (int i = 0; i < HEAP_MAX; ++i) {
    if (base != NULL && HuMemHeapPtrGet((HeapID)i) == base) return i;
  }
  return -1;
}

/* Walks a heap's circular block chain once, checking every link stays inside the heap. */
typedef int (*MptHumemVisit)(struct mpt_humem_block* block, uint32_t offset, void* ctx);
static int humem_walk(int heap, MptHumemVisit visit, void* ctx) {
  unsigned char* lo = (unsigned char*)HuMemHeapPtrGet((HeapID)heap);
  const size_t size = HuMemHeapSizeGet((HeapID)heap);
  if (lo == NULL) return 0;
  struct mpt_humem_block* block = (struct mpt_humem_block*)lo;
  size_t seen = 0;
  do {
    unsigned char* b = (unsigned char*)block;
    if (b < lo || b >= lo + size || block->size < (s32)MPT_HUMEM_HEADER || (size_t)block->size > size ||
        ++seen > size / MPT_HUMEM_HEADER) {
      return -1;
    }
    const int r = visit(block, (uint32_t)(b - lo), ctx);
    if (r != 0) return r;
    block = block->next;
  } while (block != (struct mpt_humem_block*)lo);
  return 0;
}

typedef struct {
  MptMp4HashFn fields;
  MptMp4HashFn memory;
  void* user;
} HumemHashCtx;

static int humem_hash_block(struct mpt_humem_block* block, uint32_t offset, void* ctx) {
  const HumemHashCtx* c = (const HumemHashCtx*)ctx;
  if (!block->flag) return 0;
  /* Offsets instead of the prev/next pointers; retaddr is a debug tag and stays out. */
  const uint64_t header[3] = {((uint64_t)offset << 32) | (uint32_t)block->size,
                              ((uint64_t)block->magic << 8) | block->flag, (uint64_t)block->num};
  c->fields(c->user, header, sizeof header);
  c->memory(c->user, (unsigned char*)block + MPT_HUMEM_HEADER, (size_t)block->size - MPT_HUMEM_HEADER);
  return 0;
}

int mpt_mp4_hash_humem_heap(const void* base, MptMp4HashFn fields, MptMp4HashFn memory, void* user) {
  const int heap = humem_heap_index(base);
  if (heap < 0) return 0;
  HumemHashCtx ctx = {fields, memory, user};
  const int32_t tag = heap;
  fields(user, &tag, sizeof tag);
  return humem_walk(heap, humem_hash_block, &ctx) < 0 ? -1 : 1;
}

typedef struct {
  int allocated;
  size_t min_size;
  void* found;
} HumemFindCtx;

static int humem_find(struct mpt_humem_block* block, uint32_t offset, void* ctx) {
  HumemFindCtx* c = (HumemFindCtx*)ctx;
  (void)offset;
  if ((block->flag != 0) == (c->allocated != 0) && (size_t)block->size - MPT_HUMEM_HEADER >= c->min_size) {
    c->found = (unsigned char*)block + MPT_HUMEM_HEADER;
    return 1;
  }
  return 0;
}

void* mpt_mp4_humem_find_block(int allocated, size_t min_size) {
  for (int heap = 0; heap < HEAP_MAX; ++heap) {
    HumemFindCtx ctx = {allocated, min_size, NULL};
    if (humem_walk(heap, humem_find, &ctx) == 1) return ctx.found;
  }
  return NULL;
}

void* mpt_mp4_registered_global(size_t* size) {
  *size = sizeof GWSystem;
  return &GWSystem;
}
