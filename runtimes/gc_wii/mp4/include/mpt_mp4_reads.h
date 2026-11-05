/* Game-state reads behind the C ABI (src/game_reads.c). */
#ifndef MPT_MP4_READS_H
#define MPT_MP4_READS_H

#include <stddef.h>

#include "mpt/mp4/game_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*MptMp4HashFn)(void* user, const void* data, size_t size);

void mpt_mp4_read_party(MptMp4Party* out);
void mpt_mp4_hash_game(MptMp4HashFn update, void* user);
/* If `base` is one of the game's HuMem heaps (HuMemHeapPtrGet), hashes that heap's allocated blocks
 * and returns 1: each block's position-independent header fields through `fields`, its data
 * through `memory`. Free HuMem blocks are skipped. Returns 0, hashing nothing, for any other
 * address. A heap whose block chain is broken is a fault: returns -1. */
int mpt_mp4_hash_humem_heap(const void* base, MptMp4HashFn fields, MptMp4HashFn memory, void* user);
/* For the perturbation test (mpt_mp4_debug_perturb): the data of the first allocated or the first
 * free HuMem block (heaps in HeapID order) with at least `min_size` data bytes, or NULL. */
void* mpt_mp4_humem_find_block(int allocated, size_t min_size);
/* For the perturbation test: GWSystem, a registered global the manifest hashes by name. */
void* mpt_mp4_registered_global(size_t* size);

#ifdef __cplusplus
}
#endif

#endif /* MPT_MP4_READS_H */
