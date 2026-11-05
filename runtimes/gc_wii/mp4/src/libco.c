/* The libco-compatible API of include/libco/libco.h on the SDK's coroutines (mpt_gc/coro.h).
 *
 * A coroutine's record and a canary sit at the low end of the stack process.c allocated from the
 * process heap, so everything about a suspended process is in game memory. The record of the
 * calling context (the SDK thread that runs HuPrcCall) is module data, which the runtime registers
 * as a state region (ADR 0008 section 10). */
#include "libco/libco.h"

#include <stdint.h>
#include <string.h>

#include "mpt_gc/coro.h"
#include "mpt_gc/os.h"

typedef struct MptCoThread {
  void* sp;            /* saved context while suspended */
  void (*entry)(void); /* 0 for the calling context's record */
  uint64_t magic;
  uint64_t pad;
} MptCoThread;

#define MPT_CO_MAGIC 0x4D50544C4942434Full /* "MPTLIBCO" */
#define MPT_CO_CANARY_WORDS 32             /* 256 bytes above the record */
#define MPT_CO_CANARY 0x4D5054434F43414Eull /* "MPTCOCAN" */
#define MPT_CO_OVERHEAD (sizeof(MptCoThread) + MPT_CO_CANARY_WORDS * 8)

static MptCoThread s_caller = {0, 0, MPT_CO_MAGIC, 0};
static MptCoThread* s_current = &s_caller;

static void check(MptCoThread* t, const char* what) {
  if (t == 0 || t->magic != MPT_CO_MAGIC) {
    OSPanic(__FILE__, __LINE__, "%s: not a coroutine", what);
  }
  if (t == &s_caller) {
    return;
  }
  const uint64_t* canary = (const uint64_t*)(t + 1);
  for (int i = 0; i < MPT_CO_CANARY_WORDS; ++i) {
    if (canary[i] != MPT_CO_CANARY) {
      OSPanic(__FILE__, __LINE__, "%s: process stack overflow", what);
    }
  }
}

static void trampoline(void* arg) {
  MptCoThread* self = (MptCoThread*)arg;
  self->entry();
  OSPanic(__FILE__, __LINE__, "a process coroutine returned");
}

cothread_t co_active(void) { return s_current; }

cothread_t mpt_co_create_on(void* stack, size_t stack_size, void (*entry)(void)) {
  if (stack == 0 || stack_size < MPT_CO_OVERHEAD + 2048 || ((uintptr_t)stack & 15) != 0) {
    OSPanic(__FILE__, __LINE__, "mpt_co_create_on: stack missing, misaligned or too small");
  }
  MptCoThread* t = (MptCoThread*)stack;
  memset(t, 0, sizeof *t);
  t->entry = entry;
  t->magic = MPT_CO_MAGIC;
  uint64_t* canary = (uint64_t*)(t + 1);
  for (int i = 0; i < MPT_CO_CANARY_WORDS; ++i) {
    canary[i] = MPT_CO_CANARY;
  }
  unsigned char* lo = (unsigned char*)stack + MPT_CO_OVERHEAD;
  t->sp = mpt_gc_coro_make(lo, stack_size - MPT_CO_OVERHEAD, trampoline, t);
  return t;
}

void co_switch(cothread_t thread) {
  MptCoThread* to = (MptCoThread*)thread;
  MptCoThread* from = s_current;
  check(to, "co_switch");
  check(from, "co_switch (the running coroutine)");
  s_current = to;
  mpt_gc_coro_switch(&from->sp, to->sp);
}

void co_delete(cothread_t thread) {
  MptCoThread* t = (MptCoThread*)thread;
  if (t == &s_caller || t == s_current) {
    OSPanic(__FILE__, __LINE__, "co_delete: the running coroutine");
  }
  check(t, "co_delete");
  t->magic = 0; /* the stack itself belongs to the process heap, which process.c frees */
}
