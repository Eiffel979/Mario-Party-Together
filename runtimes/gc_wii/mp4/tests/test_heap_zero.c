/* ADR 0008, "Stale heap bytes: game heaps return zeroed blocks on PC": Party Board's HuMem allocator
 * (src/game/memory.c, patch 0026) and msm's heap (src/msm/msmmem.c, patch 0027) return every block
 * all zero, whatever earlier blocks or the allocators' own headers left there. A block is filled,
 * freed and allocated again whole, split, and merged with a freed neighbour. Built from the pinned
 * checkout in module builds (no dump, no game data); the heaps are this test's own buffers. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void *HuMemHeapInit(void *ptr, size_t size);
void *HuMemMemoryAlloc(void *heap_ptr, size_t size, uintptr_t retaddr);
void *HuMemMemoryAllocNum(void *heap_ptr, size_t size, uintptr_t num, uintptr_t retaddr);
void HuMemMemoryFree(void *ptr, uintptr_t retaddr);
void msmMemInit(void *ptr, uint32_t size);
void *msmMemAlloc(uint32_t size);
void msmMemFree(void *ptr);

static int g_failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

static int allZero(const void *p, size_t n) {
  const unsigned char *b = (const unsigned char *)p;
  for (size_t i = 0; i < n; ++i)
    if (b[i] != 0) return 0;
  return 1;
}

static _Alignas(64) unsigned char g_humem[64 * 1024];
static _Alignas(64) unsigned char g_msm[64 * 1024];

static void testHuMem(void) {
  void *heap = HuMemHeapInit(g_humem, sizeof g_humem);
  memset((unsigned char *)g_humem + 64, 0xEE, sizeof g_humem - 64); /* as if the heap had history */
  /* Whole: the same size again comes back in the same block. */
  unsigned char *a = HuMemMemoryAlloc(heap, 256, 0);
  void *guard = HuMemMemoryAlloc(heap, 64, 0); /* keeps a from merging into the free tail */
  CHECK(a != NULL && guard != NULL);
  memset(a, 0xA5, 256);
  HuMemMemoryFree(a, 0);
  unsigned char *a2 = HuMemMemoryAlloc(heap, 256, 0);
  CHECK(a2 == a && allZero(a2, 256));
  /* Split: two smaller blocks from the freed one; the second starts where the first's tail and a
   * block header lay. */
  memset(a2, 0x5A, 256);
  HuMemMemoryFree(a2, 0);
  unsigned char *s1 = HuMemMemoryAllocNum(heap, 32, 7, 0);
  unsigned char *s2 = HuMemMemoryAlloc(heap, 64, 0);
  CHECK(s1 == a && s2 != NULL && s2 > s1 && s2 < a + 256);
  CHECK(allZero(s1, 32) && allZero(s2, 64));
  /* Merged: two freed neighbours coalesce; one block over both, and the header between them, is zero. */
  memset(s1, 0xC3, 32);
  memset(s2, 0x3C, 64);
  HuMemMemoryFree(s2, 0);
  HuMemMemoryFree(s1, 0);
  unsigned char *m = HuMemMemoryAlloc(heap, 256, 0);
  CHECK(m == a && allZero(m, 256));
  /* Larger than the request: a 200-byte request takes the whole 320-byte block (the 32 bytes left are
   * too few to split off), and all 256 bytes after the header are zero, which the state hash reads. */
  memset(m, 0x77, 256);
  HuMemMemoryFree(m, 0);
  m = HuMemMemoryAlloc(heap, 200, 0);
  CHECK(m == a && allZero(m, 256));
  HuMemMemoryFree(m, 0);
  HuMemMemoryFree(guard, 0);
}

static void testMsm(void) {
  /* msm places blocks by its own policy, so fill the start of the heap through allocations, free
   * them all and check what comes back: every byte it returns there held the pattern before. (Not to
   * the heap's end: msm's PC block header is 48 bytes where its layout assumes 32, so each block takes
   * 16 bytes more than msm accounts for, and a full heap runs past its end; see mp4.md section 11.) */
  enum { kMax = 64 };
  unsigned char *blocks[kMax];
  int n = 0;
  msmMemInit(g_msm, sizeof g_msm);
  while (n < kMax && (blocks[n] = msmMemAlloc(256)) != NULL) memset(blocks[n++], 0xA5, 256);
  CHECK(n > 8);
  for (int i = n - 1; i >= 0; --i) msmMemFree(blocks[i]);
  /* Whole, merged (two blocks' worth and the header between them) and split. */
  unsigned char *w = msmMemAlloc(256);
  unsigned char *m = msmMemAlloc(512);
  unsigned char *s = msmMemAlloc(96);
  CHECK(w != NULL && m != NULL && s != NULL);
  CHECK(allZero(w, 256) && allZero(m, 512) && allZero(s, 96));
  /* Not a multiple of 32 (qa, #72): msm rounds a 100-byte request up to a 128-byte user area (the
   * next header starts after it), and all 128 bytes are zero, not only the 100 asked for. */
  msmMemFree(s);
  msmMemFree(m);
  msmMemFree(w);
  unsigned char *x = msmMemAlloc(512);
  CHECK(x != NULL);
  if (x != NULL) memset(x, 0x5A, 512);
  msmMemFree(x);
  unsigned char *r = msmMemAlloc(100);
  CHECK(r != NULL && r >= x && r + 128 <= x + 512); /* inside the bytes just filled */
  CHECK(r != NULL && allZero(r, 128));
}

int main(void) {
  testHuMem();
  testMsm();
  printf("mp4_heap_zero: %d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
