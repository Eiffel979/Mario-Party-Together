/* M4-03, the seed 2 crash (ADR 0008, "Gate status after the writer fixes", point 2; Party Board patch
 * 0032): HuSprAnimRead (src/game/sprman.c) reading one anim file more than once. w01Dll's
 * RoundItemInit hands one file to Hu3DAnimCreate four times. Before the patch the byteswapping path
 * swapped the file in place on every read, so the second read got a big-endian header and
 * byteswap_animbankdata read far past the file (an access violation). Now every read of the file
 * shares the first conversion, as the console shares the file it relocated in place, and the frames
 * and layers stay in host order.
 *
 * A hand-made anim file, big-endian as on disc, no dump, no game data: one bank of two frames, one
 * pattern of one layer, one 4x4 I4 bitmap. sprman.c and byteswap.cpp come from the patched pin;
 * HuMem is malloc here, and everything else sprman.c links against fails the test if reached. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/animdata.h"
#include "game/hu3d.h"
#include "game/init.h"
#include "game/memory.h"
#include "game/sprite.h"

static int g_failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

/* ---- The file, big-endian ---- */
enum {
  kHeader = 0x00, /* AnimData32b, 0x14 bytes */
  kBank = 0x14,   /* AnimBankData32b[1], 8 bytes */
  kPat = 0x1C,    /* AnimPatData32b[1], 16 bytes */
  kBmp = 0x2C,    /* AnimBmpData32b[1], 20 bytes */
  kFrames = 0x40, /* ANIMFRAME[2], 24 bytes */
  kLayer = 0x58,  /* ANIMLAYER[1], 32 bytes */
  kPixels = 0x78, /* 4x4 I4: 8 bytes */
  kFileSize = 0x80
};

static void be16(unsigned char *p, unsigned v) {
  p[0] = (unsigned char)(v >> 8);
  p[1] = (unsigned char)v;
}
static void be32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

static void makeFile(unsigned char *f) {
  memset(f, 0, kFileSize);
  be16(f + kHeader + 0, 1); /* bankNum */
  be16(f + kHeader + 2, 1); /* patNum */
  be16(f + kHeader + 4, 1); /* bmpNum */
  be16(f + kHeader + 6, 0); /* useNum */
  be32(f + kHeader + 8, kBank);
  be32(f + kHeader + 12, kPat);
  be32(f + kHeader + 16, kBmp);

  be16(f + kBank + 0, 2); /* timeNum */
  be16(f + kBank + 2, 0x1234);
  be32(f + kBank + 4, kFrames);

  be16(f + kPat + 0, 1); /* layerNum */
  be16(f + kPat + 2, 2); /* centerX */
  be16(f + kPat + 4, 3); /* centerY */
  be16(f + kPat + 6, 4); /* sizeX */
  be16(f + kPat + 8, 4); /* sizeY */
  be32(f + kPat + 12, kLayer);

  f[kBmp + 0] = 4;             /* pixSize */
  f[kBmp + 1] = ANIM_BMP_I4;   /* dataFmt */
  be16(f + kBmp + 2, 0);       /* palNum */
  be16(f + kBmp + 4, 4);       /* sizeX */
  be16(f + kBmp + 6, 4);       /* sizeY */
  be32(f + kBmp + 8, 8);       /* dataSize */
  be32(f + kBmp + 12, kPixels); /* palData (none; any offset) */
  be32(f + kBmp + 16, kPixels);

  for (int i = 0; i < 2; ++i) { /* pat, time, shiftX, shiftY, flip, pad */
    unsigned char *fr = f + kFrames + 12 * i;
    be16(fr + 0, 0);
    be16(fr + 2, 10 + i);
    be16(fr + 4, 0x0102 + i);
    be16(fr + 6, 0x0304 + i);
  }
  f[kLayer + 0] = 0xFF;        /* alpha */
  be16(f + kLayer + 2, 0);     /* bmpNo */
  be16(f + kLayer + 8, 4);     /* sizeX */
  be16(f + kLayer + 10, 4);    /* sizeY */
  be16(f + kLayer + 16, 0x0506); /* vtx[0] */
  memset(f + kPixels, 0x5A, 8);
}

static void checkConverted(const unsigned char *file, ANIMDATA *anim) {
  CHECK(anim != NULL);
  if (anim == NULL) return;
  CHECK(anim->bankNum == 1 && anim->patNum == 1 && anim->bmpNum == 1);
  CHECK(anim->bank != NULL && anim->bank[0].timeNum == 2 && anim->bank[0].unk == 0x1234);
  CHECK((const unsigned char *)anim->bank[0].frame == file + kFrames);
  CHECK(anim->bank[0].frame[0].time == 10 && anim->bank[0].frame[1].time == 11);
  CHECK(anim->bank[0].frame[0].shiftX == 0x0102 && anim->bank[0].frame[1].shiftY == 0x0305);
  CHECK(anim->pat != NULL && anim->pat[0].layerNum == 1 && anim->pat[0].centerX == 2 && anim->pat[0].sizeY == 4);
  CHECK((const unsigned char *)anim->pat[0].layer == file + kLayer);
  CHECK(anim->pat[0].layer[0].sizeX == 4 && anim->pat[0].layer[0].vtx[0] == 0x0506);
  CHECK(anim->bmp != NULL && anim->bmp[0].dataFmt == ANIM_BMP_I4 && anim->bmp[0].sizeX == 4);
  CHECK(anim->bmp[0].dataSize == 8 && (const unsigned char *)anim->bmp[0].data == file + kPixels);
}

/* ---- What sprman.c links against ---- */
void *HuMemDirectMalloc(HeapID heap, size_t size) {
  (void)heap;
  void *p = calloc(1, size);
  if (p == NULL) { /* a byteswapped count asks for gigabytes */
    fprintf(stderr, "sprman anim test: HuMemDirectMalloc(%zu) failed\n", size);
    exit(2);
  }
  return p;
}
void *HuMemDirectMallocNum(HeapID heap, size_t size, uintptr_t num) {
  (void)num;
  return HuMemDirectMalloc(heap, size);
}
void HuMemDirectFree(void *ptr) { free(ptr); }

/* Drawing and the frame loop's (sprput.c, hsfman.c, init.c): not reached by HuSprAnimRead. */
static void unreached(const char *name) {
  fprintf(stderr, "sprman anim test: %s reached\n", name);
  exit(2);
}
void HuSprDisp(HUSPRITE *sprite) {
  (void)sprite;
  unreached("HuSprDisp");
}
void mtxTransCat(Mtx mtx, float x, float y, float z) {
  (void)mtx;
  (void)x;
  (void)y;
  (void)z;
  unreached("mtxTransCat");
}
/* HuSprAnimKill's, the GX layer's only functions sprman.c calls. Defined here so the test links
 * without a renderer: the GX layer's own definitions write to its FIFO, and the FIFO's command
 * processor needs one. */
void GXDestroyTexObj(GXTexObj *obj) {
  (void)obj;
  unreached("GXDestroyTexObj");
}
void GXDestroyTlutObj(GXTlutObj *obj) {
  (void)obj;
  unreached("GXDestroyTlutObj");
}
u32 minimumVcount = 1;

int main(void) {
  static unsigned char file[kFileSize];
  makeFile(file);

  /* First read: a new ANIMDATA in host order. */
  ANIMDATA *a = HuSprAnimRead(file);
  checkConverted(file, a);
  CHECK(a != NULL && a->useNum == 0);

  /* Three more reads of the same file (RoundItemInit's four items): the same ANIMDATA, shared as on the
   * console, with nothing in the file swapped back. */
  for (int i = 1; i < 4; ++i) {
    ANIMDATA *b = HuSprAnimRead(file);
    CHECK(b == a);
    checkConverted(file, b);
    CHECK(b != NULL && b->useNum == i);
  }

  /* A converted ANIMDATA handed back in is shared too (the port's own valid check). */
  CHECK(HuSprAnimRead(a) == a && a->useNum == 4);

  /* A second, separate copy of the file converts on its own. */
  static unsigned char other[kFileSize];
  makeFile(other);
  ANIMDATA *c = HuSprAnimRead(other);
  CHECK(c != NULL && c != a);
  checkConverted(other, c);

  printf("mp4_sprman_anim: %d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
