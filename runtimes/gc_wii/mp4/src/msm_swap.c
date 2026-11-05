/* See mpt_msm_swap.h. Compiled with the game's headers, so the layouts below are msm's own. */
#include <stddef.h>
#include <stdint.h>

#include "dolphin.h"
#include "game/msm_data.h"

#include "mpt_msm_swap.h"

/* The layouts the conversions assume: the console's, which the 64-bit build keeps (no pointers). */
_Static_assert(sizeof(MSM_HEADER) == 0x60, "MSM_HEADER");
_Static_assert(sizeof(MSM_INFO) == 0x40, "MSM_INFO");
_Static_assert(offsetof(MSM_INFO, musMax) == 4 && offsetof(MSM_INFO, seMax) == 6, "MSM_INFO s16 fields");
_Static_assert(offsetof(MSM_INFO, minMem) == 0x10 && offsetof(MSM_INFO, unk24) == 0x24, "MSM_INFO s32 fields");
_Static_assert(offsetof(MSM_INFO, baseGrpNum) == 0x28, "MSM_INFO tail");
_Static_assert(sizeof(MSM_AUXPARAM) == 40 && offsetof(MSM_AUXPARAM, revHi) == 4, "MSM_AUXPARAM");
_Static_assert(sizeof(MSM_GRP_INFO) == 32 && offsetof(MSM_GRP_INFO, dataOfs) == 4, "MSM_GRP_INFO");
_Static_assert(sizeof(MSM_GRP_HEAD) == 16, "MSM_GRP_HEAD");
_Static_assert(sizeof(MSM_MUS) == 16 && offsetof(MSM_MUS, songOfs) == 4 && offsetof(MSM_MUS, songGrp) == 12,
               "MSM_MUS");
_Static_assert(sizeof(MSM_SE) == 16 && offsetof(MSM_SE, pitchBend) == 6 && offsetof(MSM_SE, span) == 8, "MSM_SE");
_Static_assert(sizeof(MSM_STREAM_HEADER) == 0x20 && offsetof(MSM_STREAM_HEADER, chanMax) == 4, "MSM_STREAM_HEADER");
_Static_assert(sizeof(MSM_STREAM) == 8 && offsetof(MSM_STREAM, adpcmParamIdx) == 4, "MSM_STREAM");
_Static_assert(offsetof(MSM_STREAM_PACK, frq) == 6 && offsetof(MSM_STREAM_PACK, loopOfsEnd) == 8 &&
                   offsetof(MSM_STREAM_PACK, stream) == 16 && sizeof(MSM_STREAM_PACK) == 32,
               "MSM_STREAM_PACK");
_Static_assert(sizeof(SND_ADPCMSTREAM_INFO) == 32, "SND_ADPCMSTREAM_INFO");

static void swap16(void* p) {
  uint8_t* b = (uint8_t*)p;
  const uint8_t t = b[0];
  b[0] = b[1];
  b[1] = t;
}

static void swap32(void* p) {
  uint8_t* b = (uint8_t*)p;
  uint8_t t = b[0];
  b[0] = b[3];
  b[3] = t;
  t = b[1];
  b[1] = b[2];
  b[2] = t;
}

static void swap32s(void* p, size_t count) {
  for (size_t i = 0; i < count; ++i) swap32((uint8_t*)p + 4 * i);
}

void mpt_msm_swap_header(MSM_HEADER* header) { swap32s(header, sizeof(MSM_HEADER) / 4); }

void mpt_msm_swap_info(MSM_INFO* info, size_t size) {
  if (size < offsetof(MSM_INFO, baseGrpNum)) return; /* a short info block: nothing msm can use */
  swap16(&info->musMax);
  swap16(&info->seMax);
  swap32s(&info->minMem, (offsetof(MSM_INFO, baseGrpNum) - offsetof(MSM_INFO, minMem)) / 4);
}

void mpt_msm_swap_aux_params(void* params, size_t size) {
  MSM_AUXPARAM* p = (MSM_AUXPARAM*)params;
  for (size_t i = 0; i < size / sizeof(MSM_AUXPARAM); ++i) {
    /* Every member of the union is 32-bit words (u32 or f32). */
    swap32s(&p[i].revHi, (sizeof(MSM_AUXPARAM) - offsetof(MSM_AUXPARAM, revHi)) / 4);
  }
}

void mpt_msm_swap_grp_info(void* infos, size_t size) {
  MSM_GRP_INFO* g = (MSM_GRP_INFO*)infos;
  for (size_t i = 0; i < size / sizeof(MSM_GRP_INFO); ++i) {
    swap16(&g[i].gid);
    swap32(&g[i].dataOfs);
    swap32(&g[i].dataSize);
    swap32(&g[i].sampOfs);
    swap32(&g[i].sampSize);
  }
}

void mpt_msm_swap_grp_head(MSM_GRP_HEAD* head) { swap32s(head, sizeof(MSM_GRP_HEAD) / 4); }

void mpt_msm_swap_mus(void* mus, size_t size) {
  MSM_MUS* m = (MSM_MUS*)mus;
  for (size_t i = 0; i < size / sizeof(MSM_MUS); ++i) {
    swap16(&m[i].sgid);
    swap16(&m[i].sid);
    swap32(&m[i].songOfs);
    swap32(&m[i].songSize);
  }
}

void mpt_msm_swap_se(void* se, size_t size) {
  MSM_SE* s = (MSM_SE*)se;
  for (size_t i = 0; i < size / sizeof(MSM_SE); ++i) {
    swap16(&s[i].gid);
    swap16(&s[i].fxId);
    swap16(&s[i].pitchBend);
  }
}

void mpt_msm_swap_stream_header(MSM_STREAM_HEADER* header) {
  swap16(&header->version);
  swap16(&header->streamMax);
  swap32s(&header->chanMax, (sizeof(MSM_STREAM_HEADER) - offsetof(MSM_STREAM_HEADER, chanMax)) / 4);
}

static void swap_stream(MSM_STREAM* s) {
  swap32(&s->sampleOfs);
  swap16(&s->adpcmParamIdx);
}

void mpt_msm_swap_stream_tables(const MSM_STREAM_HEADER* header, unsigned int* list, size_t list_size, void* packs,
                                size_t packs_size, void* adpcm, size_t adpcm_size) {
  const size_t n = list_size / 4;
  swap32s(list, n);
  uint8_t* base = (uint8_t*)packs;
  for (size_t i = 0; i < n; ++i) {
    const uint32_t ofs = list[i];
    if (ofs == 0 || ofs < header->streamPackOfs) continue;
    const size_t at = ofs - header->streamPackOfs;
    if (at + offsetof(MSM_STREAM_PACK, stream) + sizeof(MSM_STREAM) > packs_size) continue;
    int seen = 0; /* streams may share a pack: convert it once */
    for (size_t j = 0; j < i && !seen; ++j) seen = list[j] == ofs;
    if (seen) continue;
    MSM_STREAM_PACK* p = (MSM_STREAM_PACK*)(base + at);
    swap16(&p->frq);
    swap32(&p->loopOfsEnd);
    swap32(&p->loopOfsStart);
    swap_stream(&p->stream[0]);
    if ((p->flag & MSM_STREAM_FLAG_STEREO) && at + sizeof(MSM_STREAM_PACK) <= packs_size) swap_stream(&p->stream[1]);
  }
  uint8_t* a = (uint8_t*)adpcm;
  for (size_t i = 0; i + 2 <= adpcm_size; i += 2) swap16(a + i); /* s16 coefficient pairs */
}
