/* Reads of game state for the C ABI (party mirroring and the game part of the state hash). Compiled with
 * the game's headers; called between frame steps only. */
#include <string.h>

#include "dolphin.h"
#include "game/dvd.h"
#include "game/gamework_data.h"
#include "game/memory.h"
#include "game/object.h"
#include "game/process.h"
#include "libco/libco.h"

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
#define MPT_HUMEM_USED 165u

typedef int (*MptHumemVisit)(struct mpt_humem_block* block, uint32_t offset, void* ctx);

/* Walks the circular block chain of the HuMem heap at [lo, lo + size) once, checking that every block
 * stays inside it. 0 when done, a visitor's non-zero result, or -1 for a broken chain. */
static int humem_walk(unsigned char* lo, size_t size, MptHumemVisit visit, void* ctx) {
  if (lo == NULL || size < MPT_HUMEM_HEADER) return 0;
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

static unsigned char* block_data(struct mpt_humem_block* block) { return (unsigned char*)block + MPT_HUMEM_HEADER; }
static size_t block_data_size(const struct mpt_humem_block* block) {
  return (size_t)block->size - MPT_HUMEM_HEADER;
}

/* A process heap (HuPrcCreate): one HEAP_SYSTEM block made into its own HuMem heap, whose first
 * block holds the Process that points back at it. */
static int is_process_heap(unsigned char* data, size_t size) {
  struct mpt_humem_block* first = (struct mpt_humem_block*)data;
  if (size < MPT_HUMEM_HEADER + sizeof(Process) || first->magic != MPT_HUMEM_USED || !first->flag ||
      first->size < (s32)(MPT_HUMEM_HEADER + sizeof(Process)) || (size_t)first->size > size) {
    return 0;
  }
  return ((const Process*)block_data(first))->heap == data;
}

typedef struct {
  MptMp4HashFn fields;
  MptMp4HashFn memory;
  void* user;
  int depth;
} HumemHashCtx;

static int humem_hash_block(struct mpt_humem_block* block, uint32_t offset, void* ctx);

static int hash_heap(unsigned char* lo, size_t size, HumemHashCtx* c) {
  return humem_walk(lo, size, humem_hash_block, c);
}

/* An allocated block: its position-independent header fields (offsets for prev/next; retaddr is a
 * debug tag and stays out), then its contents. A process heap is walked in turn, so its free space
 * stays out too; a coroutine stack contributes its record only (mpt_co_stack_record). */
static int humem_hash_block(struct mpt_humem_block* block, uint32_t offset, void* ctx) {
  HumemHashCtx* c = (HumemHashCtx*)ctx;
  if (!block->flag) return 0;
  const uint64_t header[3] = {((uint64_t)offset << 32) | (uint32_t)block->size,
                              ((uint64_t)block->magic << 8) | block->flag, (uint64_t)block->num};
  c->fields(c->user, header, sizeof header);
  unsigned char* data = block_data(block);
  const size_t size = block_data_size(block);
  unsigned long long record[3];
  if (mpt_co_stack_record(data, size, record)) {
    const uint32_t tag = 0x53544B; /* "STK" */
    c->fields(c->user, &tag, sizeof tag);
    c->fields(c->user, record, sizeof record);
    return 0;
  }
  if (c->depth == 0 && is_process_heap(data, size)) {
    const uint32_t tag = 0x505243; /* "PRC" */
    c->fields(c->user, &tag, sizeof tag);
    c->depth = 1;
    const int r = hash_heap(data, size, c);
    c->depth = 0;
    return r < 0 ? r : 0;
  }
  c->memory(c->user, data, size);
  return 0;
}

static int humem_heap_index(const void* base) {
  for (int i = 0; i < HEAP_MAX; ++i) {
    if (base != NULL && HuMemHeapPtrGet((HeapID)i) == base) return i;
  }
  return -1;
}

int mpt_mp4_hash_humem_heap(const void* base, MptMp4HashFn fields, MptMp4HashFn memory, void* user) {
  const int heap = humem_heap_index(base);
  if (heap < 0) return 0;
  HumemHashCtx ctx = {fields, memory, user, 0};
  const int32_t tag = heap;
  fields(user, &tag, sizeof tag);
  return hash_heap((unsigned char*)HuMemHeapPtrGet((HeapID)heap), HuMemHeapSizeGet((HeapID)heap), &ctx) < 0 ? -1 : 1;
}

/* For the perturbation test: blocks whose data the hash takes byte for byte (allocated), or leaves
 * out entirely (free, in a top-level heap or a process heap). */
typedef struct {
  int allocated;
  size_t min_size;
  void* found;
  int depth;
} HumemFindCtx;

static int humem_find(struct mpt_humem_block* block, uint32_t offset, void* ctx) {
  HumemFindCtx* c = (HumemFindCtx*)ctx;
  (void)offset;
  unsigned char* data = block_data(block);
  const size_t size = block_data_size(block);
  if (block->flag) {
    unsigned long long record[3];
    if (mpt_co_stack_record(data, size, record)) return 0;
    if (c->depth == 0 && is_process_heap(data, size)) {
      c->depth = 1;
      const int r = humem_walk(data, size, humem_find, c);
      c->depth = 0;
      return r == 1 ? 1 : 0;
    }
  }
  if ((block->flag != 0) == (c->allocated != 0) && size >= c->min_size) {
    c->found = data;
    return 1;
  }
  return 0;
}

void* mpt_mp4_humem_find_block(int allocated, size_t min_size) {
  for (int heap = 0; heap < HEAP_MAX; ++heap) {
    HumemFindCtx ctx = {allocated, min_size, NULL, 0};
    if (humem_walk((unsigned char*)HuMemHeapPtrGet((HeapID)heap), HuMemHeapSizeGet((HeapID)heap), humem_find, &ctx) == 1) {
      return ctx.found;
    }
  }
  return NULL;
}

void* mpt_mp4_registered_global(size_t* size) {
  *size = sizeof GWSystem;
  return &GWSystem;
}

/* For the perturbation test: a byte inside the first coroutine stack (its contents are not hashed). */
typedef struct {
  void* found;
  int depth;
} StackFindCtx;

static int stack_find(struct mpt_humem_block* block, uint32_t offset, void* ctx) {
  StackFindCtx* c = (StackFindCtx*)ctx;
  (void)offset;
  if (!block->flag) return 0;
  unsigned char* data = block_data(block);
  const size_t size = block_data_size(block);
  unsigned long long record[3];
  if (mpt_co_stack_record(data, size, record)) {
    c->found = data + (size - (size_t)record[1]) + (size_t)record[1] / 2; /* middle of the usable stack */
    return 1;
  }
  if (c->depth == 0 && is_process_heap(data, size)) {
    c->depth = 1;
    const int r = humem_walk(data, size, stack_find, c);
    c->depth = 0;
    return r == 1 ? 1 : 0;
  }
  return 0;
}

void* mpt_mp4_humem_find_stack(void) {
  for (int heap = 0; heap < HEAP_MAX; ++heap) {
    StackFindCtx ctx = {NULL, 0};
    if (humem_walk((unsigned char*)HuMemHeapPtrGet((HeapID)heap), HuMemHeapSizeGet((HeapID)heap), stack_find, &ctx) == 1) {
      return ctx.found;
    }
  }
  return NULL;
}

/* For memory_use (ABI v1.4): each HuMem heap's size and allocated bytes (headers included). Walks the
 * chain with humem_walk's bounds checks, so a broken chain stops the count instead of the game. */
static int humem_count_used(struct mpt_humem_block* block, uint32_t offset, void* ctx) {
  (void)offset;
  if (block->flag) *(size_t*)ctx += (size_t)block->size;
  return 0;
}

int mpt_mp4_heap_use(int heap, size_t* size, size_t* used) {
  *size = 0;
  *used = 0;
  if (heap < 0 || heap >= HEAP_MAX) return 0;
  unsigned char* base = (unsigned char*)HuMemHeapPtrGet((HeapID)heap);
  if (base == NULL) return 1;
  *size = HuMemHeapSizeGet((HeapID)heap);
  humem_walk(base, *size, humem_count_used, used);
  return 1;
}

int mpt_mp4_heap_count(void) { return HEAP_MAX; }
