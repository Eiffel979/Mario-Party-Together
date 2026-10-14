/* Replaces upstream hw_memory.c (salMalloc/salFree). Upstream's PC target falls
 * back to the host malloc/free when the game never called sndSetHooks. Game
 * memory must come from the arena (ADR 0008 §7), so here an unhooked call is a
 * fault in game code instead: MP4's msm calls sndSetHooks before sndInit. With
 * hooks set, the behaviour is upstream's. */
#include <stddef.h>

#include "musyx/hardware.h"

extern void OSPanic(const char* file, int line, const char* msg, ...);

void* salMalloc(size_t len) {
  if (!salHooks.malloc) {
    OSPanic(__FILE__, __LINE__, "musyx allocated before sndSetHooks (the host heap is not allowed)");
    return NULL;
  }
  return salHooks.malloc(len);
}

#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
void* salMallocPhysical(size_t len) { return salHooks.mallocPhysical(len); }
#endif

void salFree(void* addr) {
  if (!salHooks.free) {
    OSPanic(__FILE__, __LINE__, "musyx freed before sndSetHooks (the host heap is not allowed)");
    return;
  }
  salHooks.free(addr);
}
