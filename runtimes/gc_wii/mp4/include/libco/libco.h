/* libco-compatible coroutine API for Party Board's process system (src/game/process.c), on the SDK's
 * own context switch (ADR 0008 section 1 and its amendment after P1-13: libco is not adopted).
 *
 * Differences from libco, so that every coroutine lives in game memory (save states, ADR 0008
 * section 1):
 * - co_create is not used. Patched process.c calls mpt_co_create_on() with a stack it allocated from
 *   the process heap, as the console code does.
 * - co_delete frees nothing: the stack belongs to the process heap, which process.c frees.
 * - A cothread_t points at an MptCoThread record at the low end of its stack.
 */
#ifndef MPT_MP4_LIBCO_H
#define MPT_MP4_LIBCO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* cothread_t;

/* The calling context (the game thread that runs HuPrcCall). */
cothread_t co_active(void);
/* A coroutine on [stack, stack + stack_size) that starts in entry(), which must never return. */
cothread_t mpt_co_create_on(void* stack, size_t stack_size, void (*entry)(void));
void co_switch(cothread_t thread);
void co_delete(cothread_t thread);

#ifdef __cplusplus
}
#endif

#endif /* MPT_MP4_LIBCO_H */
