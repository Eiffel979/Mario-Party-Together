/* msm's disc structures in host byte order (src/msm_swap.c). The input is synthetic: big-endian
 * records written field by field from the structure definitions in msm_data.h, with a distinct value
 * in every field, never bytes from the game. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dolphin.h"
#include "game/msm_data.h"

#include "mpt_msm_swap.h"

static int failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                       \
    }                                                                   \
  } while (0)

static void be16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}
static void be32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static void test_header(void) {
  _Alignas(8) uint8_t b[0x60];
  for (int i = 0; i < 24; ++i) be32(b + 4 * i, 0x01020300u + (uint32_t)i);
  mpt_msm_swap_header((MSM_HEADER*)b);
  const MSM_HEADER* h = (const MSM_HEADER*)b;
  CHECK(h->magic == 0x01020300);
  CHECK(h->version == 0x01020301);
  CHECK(h->infoOfs == 0x01020304 && h->infoSize == 0x01020305u);
  CHECK(h->grpInfoSize == 0x01020309 && h->musOfs == 0x0102030A && h->seSize == 0x0102030D);
  CHECK(h->sampSize == 0x01020311 && h->grpSetOfs == 0x01020314 && h->grpSetSize == 0x01020315);
}

static void test_info(void) {
  _Alignas(8) uint8_t b[0x40];
  for (int i = 0; i < 0x40; ++i) b[i] = (uint8_t)(0x80 + i);
  be16(b + 4, 0x1234);
  be16(b + 6, 0x5678);
  for (int i = 0; i < 6; ++i) be32(b + 0x10 + 4 * i, 0x00100000u * (uint32_t)(i + 1) + 0x20u);
  mpt_msm_swap_info((MSM_INFO*)b, sizeof b);
  const MSM_INFO* f = (const MSM_INFO*)b;
  CHECK((uint8_t)f->voices == 0x80 && (uint8_t)f->grpMax == 0x83);
  CHECK(f->musMax == 0x1234 && f->seMax == 0x5678);
  CHECK((uint8_t)f->musChanMax == 0x88 && (uint8_t)f->auxParamB == 0x8F);
  CHECK(f->minMem == 0x00100020 && f->aramSize == 0x00200020 && f->grpBufSizeA == 0x00300020);
  CHECK(f->grpBufSizeB == 0x00400020 && f->dummyMusSize == 0x00500020 && f->unk24 == 0x00600020);
  CHECK((uint8_t)f->baseGrpNum == 0xA8 && (uint8_t)f->baseGrp[22] == 0xBF);
}

static void test_grp_info_and_head(void) {
  _Alignas(8) uint8_t b[3 * 32 + 5]; /* three records and a partial one, which is left alone */
  memset(b, 0xEE, sizeof b);
  for (int i = 0; i < 3; ++i) {
    uint8_t* r = b + 32 * i;
    be16(r, (uint16_t)(0x0100 + i));
    r[2] = (uint8_t)i;
    r[3] = (uint8_t)(0x40 + i);
    be32(r + 4, 0x1000u * (uint32_t)(i + 1));
    be32(r + 8, 0x200u + (uint32_t)i);
    be32(r + 12, 0x30000u + (uint32_t)i);
    be32(r + 16, 0x4000u + (uint32_t)i);
  }
  mpt_msm_swap_grp_info(b, sizeof b);
  const MSM_GRP_INFO* g = (const MSM_GRP_INFO*)b;
  CHECK(g[2].gid == 0x0102 && g[2].stackNo == 2 && g[2].subGrpId == 0x42);
  CHECK(g[1].dataOfs == 0x2000 && g[1].dataSize == 0x201 && g[1].sampOfs == 0x30001 && g[1].sampSize == 0x4001);
  CHECK(g[0].pad[0] == 0xEE && b[3 * 32 + 4] == 0xEE);

  _Alignas(8) uint8_t h[16 + 4];
  be32(h, 0x10);
  be32(h + 4, 0x20);
  be32(h + 8, 0x30);
  be32(h + 12, 0x40);
  be32(h + 16, 0xAABBCCDD); /* group data after the head stays big-endian for MusyX */
  mpt_msm_swap_grp_head((MSM_GRP_HEAD*)h);
  const MSM_GRP_HEAD* head = (const MSM_GRP_HEAD*)h;
  CHECK(head->poolOfs == 0x10 && head->projOfs == 0x20 && head->sdirOfs == 0x30 && head->sngOfs == 0x40);
  CHECK(h[16] == 0xAA && h[19] == 0xDD);
}

static void test_mus_se_aux(void) {
  _Alignas(8) uint8_t m[2 * 16];
  for (int i = 0; i < 2; ++i) {
    uint8_t* r = m + 16 * i;
    be16(r, (uint16_t)(0x0A00 + i));
    be16(r + 2, (uint16_t)(0x0B00 + i));
    be32(r + 4, 0x00C00000u + (uint32_t)i);
    be32(r + 8, 0x00D00000u + (uint32_t)i);
    r[12] = (uint8_t)(0xF0 + i);
    r[13] = (uint8_t)(0x70 + i);
  }
  mpt_msm_swap_mus(m, sizeof m);
  const MSM_MUS* mus = (const MSM_MUS*)m;
  CHECK(mus[1].sgid == 0x0A01 && mus[1].sid == 0x0B01 && mus[1].songOfs == 0x00C00001 && mus[1].songSize == 0x00D00001);
  CHECK(mus[0].songGrp == (s8)0xF0 && mus[1].vol == 0x71);

  _Alignas(8) uint8_t s[16];
  memset(s, 0, sizeof s);
  be16(s, 0x0123);
  be16(s + 2, 0x0456);
  s[4] = 0x7F;
  s[5] = 0x40;
  be16(s + 6, (uint16_t)-1234);
  s[8] = 0x11;
  s[12] = 0x22;
  mpt_msm_swap_se(s, sizeof s);
  const MSM_SE* se = (const MSM_SE*)s;
  CHECK(se->gid == 0x0123 && se->fxId == 0x0456 && se->vol == 0x7F && se->pan == 0x40);
  CHECK(se->pitchBend == -1234 && se->span == 0x11 && se->emiComp == 0x22);

  _Alignas(8) uint8_t a[2 * 40];
  memset(a, 0, sizeof a);
  a[0] = 2; /* chorus */
  be32(a + 4, 15);
  be32(a + 8, 3);
  be32(a + 12, 500);
  a[40] = 0; /* reverb: f32 fields */
  be32(a + 44, 0);
  const float mix = 0.5f;
  uint32_t bits;
  memcpy(&bits, &mix, 4);
  be32(a + 52, bits);
  mpt_msm_swap_aux_params(a, sizeof a);
  const MSM_AUXPARAM* aux = (const MSM_AUXPARAM*)a;
  CHECK(aux[0].type == 2 && aux[0].chorus.baseDelay == 15 && aux[0].chorus.variation == 3 && aux[0].chorus.period == 500);
  CHECK(aux[1].revHi.mix == 0.5f);
}

/* A .pdt: three streams, the first two sharing one stereo pack, the third a mono pack placed
 * directly after it (so converting a missing second stream would corrupt it). */
static void test_stream(void) {
  enum { kList = 0x20, kAdpcm = 0x40, kPacks = 0x80, kSamples = 0x100 };
  _Alignas(8) uint8_t hdr[0x20];
  be16(hdr, 1);
  be16(hdr + 2, 3);
  be32(hdr + 4, 2);
  be32(hdr + 8, 32000);
  be32(hdr + 12, 4);
  be32(hdr + 16, kList);
  be32(hdr + 20, kAdpcm);
  be32(hdr + 24, kPacks);
  be32(hdr + 28, kSamples);
  mpt_msm_swap_stream_header((MSM_STREAM_HEADER*)hdr);
  const MSM_STREAM_HEADER* h = (const MSM_STREAM_HEADER*)hdr;
  CHECK(h->version == MSM_PDT_FILE_VERSION && h->streamMax == 3 && h->chanMax == 2 && h->sampleFrq == 32000);
  CHECK(h->maxBufs == 4 && h->streamPackListOfs == kList && h->adpcmParamOfs == kAdpcm);
  CHECK(h->streamPackOfs == kPacks && h->sampleOfs == kSamples);

  _Alignas(8) uint8_t list[kAdpcm - kList];
  memset(list, 0, sizeof list);
  be32(list, kPacks);
  be32(list + 4, kPacks);
  be32(list + 8, kPacks + 32);
  _Alignas(8) uint8_t packs[kSamples - kPacks];
  memset(packs, 0x5A, sizeof packs);
  uint8_t* p0 = packs;
  p0[0] = MSM_STREAM_FLAG_STEREO | MSM_STREAM_FLAG_LOOP;
  be16(p0 + 6, 32000);
  be32(p0 + 8, 0x11112222);
  be32(p0 + 12, 0x00003333);
  be32(p0 + 16, 0x00010000);
  be16(p0 + 20, 1);
  be32(p0 + 24, 0x00020000);
  be16(p0 + 28, 2);
  uint8_t* p1 = packs + 32; /* mono: only stream[0], 24 bytes */
  p1[0] = 0;
  be16(p1 + 6, 22050);
  be32(p1 + 8, 0x44);
  be32(p1 + 12, 0x55);
  be32(p1 + 16, 0x00030000);
  be16(p1 + 20, 0);
  const uint8_t after_mono = packs[32 + 24];
  _Alignas(8) uint8_t adpcm[kPacks - kAdpcm];
  for (int i = 0; i < (int)sizeof adpcm / 2; ++i) be16(adpcm + 2 * i, (uint16_t)(-100 - i));

  mpt_msm_swap_stream_tables(h, (unsigned int*)list, sizeof list, packs, sizeof packs, adpcm, sizeof adpcm);
  const uint32_t* l = (const uint32_t*)list;
  CHECK(l[0] == kPacks && l[1] == kPacks && l[2] == kPacks + 32 && l[3] == 0);
  const MSM_STREAM_PACK* s0 = (const MSM_STREAM_PACK*)p0;
  CHECK(s0->frq == 32000 && s0->loopOfsEnd == 0x11112222 && s0->loopOfsStart == 0x3333); /* converted once */
  CHECK(s0->stream[0].sampleOfs == 0x10000 && s0->stream[0].adpcmParamIdx == 1);
  CHECK(s0->stream[1].sampleOfs == 0x20000 && s0->stream[1].adpcmParamIdx == 2);
  const MSM_STREAM_PACK* s1 = (const MSM_STREAM_PACK*)p1;
  CHECK(s1->frq == 22050 && s1->loopOfsEnd == 0x44 && s1->stream[0].sampleOfs == 0x30000);
  CHECK(packs[32 + 24] == after_mono);
  const SND_ADPCMSTREAM_INFO* info = (const SND_ADPCMSTREAM_INFO*)adpcm;
  CHECK(info[0].coefTab[0][0] == -100 && info[0].coefTab[7][1] == -115 && info[1].coefTab[0][0] == -116);
}

int main(void) {
  test_header();
  test_info();
  test_grp_info_and_head();
  test_mus_se_aux();
  test_stream();
  if (failures == 0) printf("msm byte order: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
