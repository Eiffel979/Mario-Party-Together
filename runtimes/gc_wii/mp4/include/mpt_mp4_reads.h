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

#ifdef __cplusplus
}
#endif

#endif /* MPT_MP4_READS_H */
