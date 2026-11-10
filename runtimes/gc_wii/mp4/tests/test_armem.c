/* HuARMalloc on PC (Party Board's src/game/armem.c from the patched pin; no dump, no game data):
 *  - ADR 0008, "Stale heap bytes" (patch 0028): every block comes back all zero, whole, split and
 *    merged with a freed neighbour, including the rounded-up tail of a request that is not a multiple
 *    of 32 bytes;
 *  - ADR 0008, "Gate status after the implementation", finding 2 (patch 0030): blocks start at the
 *    console's HU_AMEM_BASE, never in msm's ARAM below it, and mpt_mp4_armem_in_use lists exactly the
 *    in-use blocks in address order.
 * The SDK's ARAM calls armem.c makes are stubbed here over the test's own 16 MB buffer; every other
 * call armem.c can make (DMA, data dirs) fails the test if reached. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
typedef int32_t s32;

void HuARInit(void);
u32 HuARMalloc(u32 size);
void HuARFree(u32 aMemP);
s32 mpt_mp4_armem_in_use(u32 *addr, u32 *size, s32 max);
u32 mpt_mp4_armem_floor(void);

enum { kAramBytes = 16 * 1024 * 1024, kFloor = 0x808000 };
static unsigned char g_aram[kAramBytes];

static int g_failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

/* ---- The SDK calls armem.c makes for allocation, over g_aram ---- */
int ARCheckInit(void) { return 1; }
u32 ARGetSize(void) { return kAramBytes; }
void mpt_gc_ARClear(u32 aram_addr, u32 length) {
  if ((uint64_t)aram_addr + length > kAramBytes) {
    fprintf(stderr, "mpt_gc_ARClear out of range\n");
    exit(2);
  }
  memset(g_aram + aram_addr, 0, length);
}
void OSReport(const char *fmt, ...) { (void)fmt; }

/* ---- Everything else armem.c links against: not reached by allocation ---- */
#define UNREACHED(name)                                  \
  void name(void) {                                      \
    fprintf(stderr, "armem test: " #name " reached\n"); \
    exit(2);                                             \
  }
UNREACHED(ARInit)
UNREACHED(ARQInit)
UNREACHED(ARQPostRequest)
UNREACHED(DCFlushRangeNoSync)
UNREACHED(DCInvalidateRange)
UNREACHED(PPCSync)
UNREACHED(HuDataDirClose)
UNREACHED(HuDataDirRead)
UNREACHED(HuDataDirSet)
UNREACHED(HuDataGetDirPtr)
UNREACHED(HuDataGetStatus)
UNREACHED(HuDataReadChk)
UNREACHED(HuDecodeData)
UNREACHED(HuMemDirectFree)
UNREACHED(HuMemDirectMalloc)
UNREACHED(HuMemDirectMallocNum)
UNREACHED(HuMemMemorySizeGet)
UNREACHED(byteswap_s32)
u32 DirDataSize; /* data.c's, read only by the DMA paths */

static int allZero(u32 addr, u32 n) {
  for (u32 i = 0; i < n; ++i)
    if (g_aram[addr + i] != 0) return 0;
  return 1;
}

static void fill(u32 addr, u32 n, unsigned char v) { memset(g_aram + addr, v, n); }

int main(void) {
  memset(g_aram, 0xEE, sizeof g_aram); /* as if ARAM had history (DMA, earlier blocks) */
  HuARInit();
  CHECK(mpt_mp4_armem_floor() == kFloor);

  u32 addr[8], size[8];
  CHECK(mpt_mp4_armem_in_use(addr, size, 8) == 0);

  /* The console's base: the first block starts at HU_AMEM_BASE, above msm's ARAM (patch 0030). */
  const u32 a = HuARMalloc(0x400);
  const u32 guard = HuARMalloc(0x40); /* keeps a from merging into the free tail */
  CHECK(a == kFloor);
  CHECK(guard == kFloor + 0x400);
  CHECK(allZero(a, 0x400) && allZero(guard, 0x40));

  /* Whole: the same size again comes back in the same block, all zero. */
  fill(a, 0x400, 0xA5);
  HuARFree(a);
  u32 a2 = HuARMalloc(0x400);
  CHECK(a2 == a && allZero(a2, 0x400));

  /* Split: two smaller blocks from the freed one. */
  fill(a2, 0x400, 0x5A);
  HuARFree(a2);
  const u32 s1 = HuARMalloc(0x100);
  const u32 s2 = HuARMalloc(0x80);
  CHECK(s1 == a && s2 == a + 0x100);
  CHECK(allZero(s1, 0x100) && allZero(s2, 0x80));

  /* Listed: in-use blocks only, in address order, with their sizes. */
  s32 n = mpt_mp4_armem_in_use(addr, size, 8);
  CHECK(n == 3);
  if (n == 3) {
    CHECK(addr[0] == s1 && size[0] == 0x100);
    CHECK(addr[1] == s2 && size[1] == 0x80);
    CHECK(addr[2] == guard && size[2] == 0x40);
  }

  /* Merged: two freed neighbours coalesce, and one block over both is zero. */
  fill(s1, 0x100, 0xC3);
  fill(s2, 0x80, 0x3C);
  HuARFree(s1);
  HuARFree(s2);
  const u32 m = HuARMalloc(0x180);
  CHECK(m == a && allZero(m, 0x180));
  HuARFree(m);

  /* Not a multiple of 32: a 100-byte request takes a 128-byte block, and all 128 bytes are zero. */
  fill(a, 0x400, 0x77);
  const u32 r = HuARMalloc(100);
  CHECK(r == a && allZero(r, 128));
  CHECK(mpt_mp4_armem_in_use(addr, size, 8) == 2 && addr[0] == r && size[0] == 128);

  /* Nothing below the floor was ever handed out or cleared: msm's ARAM keeps its bytes. */
  CHECK(g_aram[0x20] == 0xEE && g_aram[kFloor - 1] == 0xEE);

  printf("mp4_armem: %d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
