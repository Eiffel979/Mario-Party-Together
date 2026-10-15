/* Game-side coroutines on the SDK's context switch (ADR 0008 section 1).
 *
 * For a title's own cooperative scheduler that runs inside one SDK thread, such as
 * MP4's process system (HuPrc). A coroutine's stack is memory the caller provides,
 * normally from a game heap in the arena, so a suspended coroutine is part of the
 * save state. The same rules as SDK threads apply: no C++ exception, longjmp or SEH
 * unwind may cross a switch, and `entry` must never return.
 *
 * The SDK does not schedule these coroutines and knows nothing about them: SDK call
 * boundaries inside one run on the coroutine's stack, as they would on the SDK
 * thread that switched to it, and virtual interrupts that fall due there use that
 * stack too. Size the stacks for it.
 *
 * Not SDK call boundaries.
 */
#ifndef MPT_GC_CORO_H
#define MPT_GC_CORO_H

#include "mpt_gc/types.h"

MPT_GC_EXTERN_C_BEGIN

typedef void (*MptGcCoroEntry)(void* arg);

/* Prepares a coroutine on [stack_lo, stack_lo + stack_size). The first switch to the
 * returned context calls entry(arg). The context starts with the SDK's deterministic
 * MXCSR and x87 control word. Returns the context (its saved stack pointer). */
void* mpt_gc_coro_make(void* stack_lo, size_t stack_size, MptGcCoroEntry entry, void* arg);

/* Saves the running context into *save and resumes `load`. Returns when something
 * switches back to the saved context. */
void mpt_gc_coro_switch(void** save, void* load);

MPT_GC_EXTERN_C_END

#endif /* MPT_GC_CORO_H */
