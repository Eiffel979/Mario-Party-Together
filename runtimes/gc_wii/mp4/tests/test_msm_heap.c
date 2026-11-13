/* ADR 0008, "Gate status after the implementation", finding 1 (patch 0035): msm's heap sizes and
 * places its blocks with the PC header, so a heap filled to exactly full stays inside its bounds and
 * the next allocation fails. The heap is this test's own buffer with guard bytes on both sides;
 * msmmem.c is built from the patched pin in module builds (no dump, no game data). Without patch
 * 0035, each block's user area starts 16 bytes later than msm accounts for and the filled heap runs
 * into the guard bytes behind it. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

enum {
  kHeader = 64,        /* sizeof(MSMBLOCK) on PC, pinned by msmmem.c's static assertion */
  kGuard = 4096,       /* guard bytes before and after the heap */
  kHeap = 64 * 1024,   /* a multiple of 32, on a 32-byte boundary: msmMemInit takes it whole */
  kRequest = 192,      /* 192 + 64 is 256, a multiple of 32: 256 blocks fill the heap exactly */
  kBlocks = kHeap / (kRequest + kHeader),
  kGuardByte = 0xC3,
};

static _Alignas(64) unsigned char g_buf[kGuard + kHeap + kGuard];
static unsigned char *const g_heap = g_buf + kGuard;

static void resetBuffer(void) {
  memset(g_buf, kGuardByte, sizeof g_buf);
  msmMemInit(g_heap, kHeap);
}

static size_t guardsTouched(void) {
  size_t n = 0;
  for (size_t i = 0; i < kGuard; ++i) {
    if (g_buf[i] != kGuardByte) ++n;
    if (g_heap[kHeap + i] != kGuardByte) ++n;
  }
  return n;
}

static int inHeap(const unsigned char *p, size_t n) {
  return p != NULL && p >= g_heap && p + n <= g_heap + kHeap;
}

/* Inside this test's buffer, guard bytes included: safe to write, so an overrun shows in the guards. */
static int inBuffer(const unsigned char *p, size_t n) {
  return p != NULL && p >= g_buf && p + n <= g_buf + sizeof g_buf;
}

/* Fill to exactly full with one request size, writing every byte asked for, as msm's callers do. */
static void testExactlyFull(void) {
  static unsigned char *blocks[kBlocks + 8];
  resetBuffer();
  int n = 0;
  int outside = 0, misaligned = 0;
  while (n < (int)(sizeof blocks / sizeof blocks[0])) {
    unsigned char *p = msmMemAlloc(kRequest);
    if (p == NULL) break;
    if (!inHeap(p, kRequest)) ++outside;
    if (inBuffer(p, kRequest)) memset(p, 0xA5, kRequest);
    if (((uintptr_t)p & 0x1F) != 0) ++misaligned;
    blocks[n++] = p;
  }
  CHECK(n == kBlocks);
  CHECK(outside == 0);
  CHECK(misaligned == 0); /* the console's 32-byte user-area alignment */
  CHECK(guardsTouched() == 0);
  /* Full: nothing more fits, not even one byte, and asking writes nothing. */
  CHECK(msmMemAlloc(1) == NULL);
  CHECK(msmMemAlloc(kRequest) == NULL);
  CHECK(guardsTouched() == 0);
  printf("exactly full: %d blocks of %d bytes, guard bytes touched %zu\n", n, kRequest, guardsTouched());

  /* Freed again, the heap takes one block as large as it can hold, and no larger. */
  for (int i = n - 1; i >= 0; --i) msmMemFree(blocks[i]);
  unsigned char *big = msmMemAlloc(kHeap - kHeader);
  CHECK(inHeap(big, kHeap - kHeader));
  if (inBuffer(big, kHeap - kHeader)) memset(big, 0x5A, kHeap - kHeader);
  CHECK(msmMemAlloc(1) == NULL);
  CHECK(guardsTouched() == 0);
  msmMemFree(big);
  CHECK(msmMemAlloc(kHeap - kHeader + 1) == NULL);
  CHECK(msmMemAlloc(0xFFFFFFF0u) == NULL); /* would wrap allocSize */
  CHECK(guardsTouched() == 0);
}

/* Mixed sizes, none a multiple of 32, until the heap refuses one: every block lies inside the heap
 * and the guard bytes stay as they were. */
static void testMixedUntilFull(void) {
  static const uint32_t sizes[] = {100, 7, 513, 64, 33, 2000, 1, 250};
  resetBuffer();
  int n = 0, outside = 0;
  for (int i = 0; i < 4096; ++i) {
    uint32_t sz = sizes[i % (sizeof sizes / sizeof sizes[0])];
    unsigned char *p = msmMemAlloc(sz);
    if (p == NULL) break;
    if (!inHeap(p, sz)) ++outside;
    if (inBuffer(p, sz)) memset(p, 0x3C, sz);
    ++n;
  }
  CHECK(n > 16 && n < 4096);
  CHECK(outside == 0);
  CHECK(guardsTouched() == 0);
  printf("mixed sizes: %d blocks before the heap refused one\n", n);
}

int main(void) {
  testExactlyFull();
  testMixedUntilFull();
  printf("mp4_msm_heap: %d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
