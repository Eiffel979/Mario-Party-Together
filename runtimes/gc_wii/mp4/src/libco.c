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
  uint64_t usable; /* bytes of stack above the record and canary */
} MptCoThread;

#define MPT_CO_MAGIC 0x4D50544C4942434Full /* "MPTLIBCO" */
#define MPT_CO_CANARY_WORDS 32             /* 256 bytes above the record */
#define MPT_CO_CANARY 0x4D5054434F43414Eull /* "MPTCOCAN" */
#define MPT_CO_OVERHEAD (sizeof(MptCoThread) + MPT_CO_CANARY_WORDS * 8)

static MptCoThread s_caller = {0, 0, MPT_CO_MAGIC, 0};
static MptCoThread* s_current = &s_caller;

/* Stack high-water measurement (P1-16): each process stack is painted at creation, and the
 * untouched paint is measured when the process ends and, sampled, when it yields (co_switch).
 * The largest share used so far is reported
 * through OSReport whenever it grows, with the stack's size, so the dump lane shows how
 * close interrupt work on these stacks (MusyX rendering, ADR 0008 section 1) comes to the canary. */
#define MPT_CO_PAINT 0xA5u
#define MPT_CO_SAMPLE_EVERY 256u /* power of two */
static uint32_t s_peak_permille;
static uint32_t s_switches;

static void measure(const MptCoThread* t) {
  const unsigned char* lo = (const unsigned char*)t + MPT_CO_OVERHEAD;
  size_t untouched = 0;
  while (untouched < t->usable && lo[untouched] == MPT_CO_PAINT) ++untouched;
  const size_t used = (size_t)t->usable - untouched;
  const uint32_t permille = t->usable ? (uint32_t)(used * 1000u / t->usable) : 0;
  if (permille > s_peak_permille) {
    s_peak_permille = permille;
    {
      /* The entry is an address in the module, which sits at a fixed base: it names the process. */
      OSReport("libco: process stack high water %u of %u bytes (%u.%u%%), entry %p\n", (unsigned)used,
               (unsigned)t->usable, permille / 10, permille % 10, (void*)t->entry);
    }
  }
}

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

int mpt_co_stack_record(const void* p, size_t size, unsigned long long out[3]) {
  const MptCoThread* t = (const MptCoThread*)p;
  if (p == 0 || size < MPT_CO_OVERHEAD || ((uintptr_t)p & 15) != 0 || t->magic != MPT_CO_MAGIC || t->entry == 0 ||
      t->usable > size - MPT_CO_OVERHEAD) {
    return 0;
  }
  out[0] = (unsigned long long)(uintptr_t)t->entry; /* the module sits at a fixed base */
  out[1] = (unsigned long long)t->usable;
  out[2] = t->sp ? (unsigned long long)((const unsigned char*)t->sp - (const unsigned char*)p) : 0;
  return 1;
}

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
  t->usable = stack_size - MPT_CO_OVERHEAD;
  memset(lo, MPT_CO_PAINT, (size_t)t->usable);
  t->sp = mpt_gc_coro_make(lo, (size_t)t->usable, trampoline, t);
  return t;
}

void co_switch(cothread_t thread) {
  MptCoThread* to = (MptCoThread*)thread;
  MptCoThread* from = s_current;
  check(to, "co_switch");
  check(from, "co_switch (the running coroutine)");
  /* Long-lived processes (the main loop, the title screen) are never deleted, so also sample the
   * stack a process leaves behind: one switch in MPT_CO_SAMPLE_EVERY, which keeps the paint scan off
   * the per-frame path. The scan only reads the stack. The counter and the peak sit in the game data
   * range like every static here, so they are saved and hashed; both follow from the switch order
   * alone, which is deterministic. */
  if (from != &s_caller && (++s_switches & (MPT_CO_SAMPLE_EVERY - 1u)) == 0) {
    measure(from);
  }
  s_current = to;
  mpt_gc_coro_switch(&from->sp, to->sp);
}

void co_delete(cothread_t thread) {
  MptCoThread* t = (MptCoThread*)thread;
  if (t == &s_caller || t == s_current) {
    OSPanic(__FILE__, __LINE__, "co_delete: the running coroutine");
  }
  check(t, "co_delete");
  measure(t);
  /* The stack itself belongs to the process heap, which process.c frees, and the game may hand the
   * memory out again. Host frames left values on it that differ per process (stack cookies, host
   * heap pointers), so scrub it before it is free: a later block that is not fully written must not
   * carry them into the state hash. */
  memset((unsigned char*)t + MPT_CO_OVERHEAD, MPT_CO_PAINT, (size_t)t->usable);
  t->magic = 0;
}
