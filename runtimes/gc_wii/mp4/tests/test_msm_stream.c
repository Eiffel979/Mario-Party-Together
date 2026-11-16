/* The minigame-end jingle (Party Board patch 0036): msm streams a non-looping track through a ring of two
 * halves, refilling the half the voice has just finished. On PC the DVD layer can complete a read, and
 * run msm's callback, inside DVDReadAsync. Before the patch the callback then uploaded the other half,
 * one half late, and a stream's last half, which no later read uploads, played the ring's contents from
 * two halves earlier: every winner and draw jingle ended with 0.58 s of earlier audio.
 *
 * A hand-made stream file, big-endian as on disc, no dump and no game data: one mono, non-looping stream
 * of 340 ADPCM blocks (3.4 ring halves, so the last read is under a full half), each block numbered.
 * msmstream.c comes from the patched pin. DVD reads complete synchronously, as ours can, and this test
 * plays MusyX's part: a voice that walks the ring and calls msm's update function the way MusyX's
 * streamHandle does. The voice must play blocks 0 to 339 in order, then only silence until msm stops it.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dolphin.h"
#include "msm/msmfio.h"
#include "msm/msmmem.h"
#include "msm/msmstream.h"
#include "msm/msmsys.h"

static int g_failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

/* ---- The stream file, big-endian ---- */
enum {
  kSampleFrq = 1400,   /* ring half: 100 blocks of 14 samples, 800 bytes; whole ring 1600 bytes */
  kBlocks = 340,       /* 3 full halves and 40 blocks: the last read is 320 bytes, under a half */
  kDataBytes = kBlocks * 8,
  kListOfs = 0x20,
  kAdpcmOfs = 0x40,
  kPackOfs = 0x60,
  kSampleOfs = 0x80,
  kFileSize = kSampleOfs + kDataBytes,
  kRingBlocks = 200,
  kRingBytes = kRingBlocks * 8,
  kRingSamples = kRingBlocks * 14,
  kStepSamples = 70,   /* five blocks per MusyX update, like its 5 ms ticks */
};

static uint8_t g_file[kFileSize];

static void put16(uint8_t* p, unsigned v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}
static void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

/* Block k: a marker byte, k, then filler. Never all zero, so it differs from msm's zero fill. */
static void block_bytes(unsigned k, uint8_t out[8]) {
  out[0] = 0x11;
  out[1] = (uint8_t)(k >> 8);
  out[2] = (uint8_t)k;
  memset(out + 3, 0x5A, 5);
}

static void build_file(void) {
  memset(g_file, 0, sizeof g_file);
  put16(g_file + 0x00, 1);          /* version (MSM_PDT_FILE_VERSION) */
  put16(g_file + 0x02, 1);          /* streamMax */
  put32(g_file + 0x04, 1);          /* chanMax */
  put32(g_file + 0x08, kSampleFrq); /* sampleFrq */
  put32(g_file + 0x0C, 2);          /* maxBufs */
  put32(g_file + 0x10, kListOfs);
  put32(g_file + 0x14, kAdpcmOfs);
  put32(g_file + 0x18, kPackOfs);
  put32(g_file + 0x1C, kSampleOfs);
  put32(g_file + kListOfs, kPackOfs); /* stream 0 -> the pack */
  uint8_t* pack = g_file + kPackOfs;
  pack[0] = 0;                        /* flag: mono, no loop */
  pack[1] = 127;                      /* vol */
  pack[2] = 64;                       /* pan */
  put16(pack + 6, kSampleFrq);        /* frq */
  put32(pack + 8, 2u * kDataBytes);   /* loopOfsEnd, in nibbles */
  put32(pack + 12, 0);                /* loopOfsStart */
  put32(pack + 16, kSampleOfs);       /* stream[0].sampleOfs */
  put16(pack + 20, 0);                /* stream[0].adpcmParamIdx */
  for (unsigned k = 0; k < kBlocks; ++k) block_bytes(k, g_file + kSampleOfs + 8 * k);
}

/* ---- DVD: reads complete synchronously, inside DVDReadAsync, as the PC DVD layer may ---- */
s32 DVDConvertPathToEntrynum(const char* pathPtr) {
  (void)pathPtr;
  return 1;
}
BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo) {
  (void)entrynum;
  memset(fileInfo, 0, sizeof *fileInfo);
  return TRUE;
}
BOOL DVDClose(DVDFileInfo* fileInfo) {
  (void)fileInfo;
  return TRUE;
}
s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) {
  (void)block;
  return 0; /* idle: every read has completed */
}
BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio) {
  (void)prio;
  CHECK(offset >= 0 && length >= 0 && offset + length <= kFileSize);
  if (offset < 0 || length < 0 || offset + length > kFileSize) return FALSE;
  memcpy(addr, g_file + offset, (size_t)length);
  if (callback) callback(length, fileInfo);
  return TRUE;
}

BOOL msmFioOpen(s32 entrynum, DVDFileInfo* fileInfo) { return DVDFastOpen(entrynum, fileInfo); }
BOOL msmFioClose(DVDFileInfo* fileInfo) { return DVDClose(fileInfo); }
BOOL msmFioRead(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset) {
  (void)fileInfo;
  if (offset < 0 || length < 0) return -1;
  size_t n = (size_t)length;
  memset(addr, 0, n);
  if ((size_t)offset < sizeof g_file) {
    const size_t avail = sizeof g_file - (size_t)offset;
    memcpy(addr, g_file + offset, n < avail ? n : avail);
  }
  return TRUE;
}

/* ---- msm's heap and IRQ guards ---- */
void* msmMemAlloc(u32 size) { return calloc(1, size); }
void msmMemFree(void* ptr) { free(ptr); }
void msmSysIrqDisable(void) {}
void msmSysIrqEnable(void) {}

/* ---- MusyX's part: one stream voice over a modelled ARAM ring ---- */
typedef u32 (*UpdateFn)(void*, u32, void*, u32, u32);
static struct {
  int allocated, active, stopped;
  uint8_t* buffer; /* msm's main-RAM ring */
  u32 size;        /* ring samples */
  UpdateFn update;
  u32 user;
  uint8_t aram[kRingBytes];
  u32 pos, last;
} g_voice;

SND_STREAMID sndStreamAllocEx(u8 prio, void* buffer, u32 samples, u32 frq, u8 vol, u8 pan, u8 span, u8 auxa,
                              u8 auxb, u8 studio, u32 flags,
                              u32 (*updateFunction)(void* buffer1, u32 len1, void* buffer2, u32 len2, u32 user),
                              u32 user, SND_ADPCMSTREAM_INFO* adpcmInfo) {
  (void)prio, (void)frq, (void)vol, (void)pan, (void)span, (void)auxa, (void)auxb, (void)studio, (void)flags;
  (void)adpcmInfo;
  CHECK(samples == kRingSamples);
  g_voice.allocated = 1;
  g_voice.buffer = buffer;
  g_voice.size = samples;
  g_voice.update = updateFunction;
  g_voice.user = user;
  return 7;
}
void sndStreamFree(SND_STREAMID stid) { (void)stid; }
void sndStreamMixParameterEx(SND_STREAMID stid, u8 vol, u8 pan, u8 span, u8 auxa, u8 auxb) {
  (void)stid, (void)vol, (void)pan, (void)span, (void)auxa, (void)auxb;
}
void sndStreamADPCMParameter(SND_STREAMID stid, SND_ADPCMSTREAM_INFO* adpcmInfo) { (void)stid, (void)adpcmInfo; }
void sndStreamFrq(SND_STREAMID stid, u32 frq) { (void)stid, (void)frq; }
bool sndStreamActivate(SND_STREAMID stid) {
  CHECK(stid == 7);
  g_voice.active = 1;
  g_voice.pos = g_voice.last = 0;
  return TRUE;
}
void sndStreamDeactivate(SND_STREAMID stid) {
  CHECK(stid == 7);
  g_voice.active = 0;
  g_voice.stopped = 1;
}
/* MusyX's ADPCM conversion: samples to whole 8-byte blocks of 14 samples. */
void sndStreamARAMUpdate(SND_STREAMID stid, u32 off1, u32 len1, u32 off2, u32 len2) {
  CHECK(stid == 7);
  const u32 offs[2] = {off1, off2}, lens[2] = {len1, len2};
  for (int i = 0; i < 2; ++i) {
    if (lens[i] == 0) continue;
    const u32 off = (offs[i] / 14) * 8;
    const u32 bytes = ((lens[i] + 13) / 14) * 8;
    CHECK(off + bytes <= kRingBytes);
    if (off + bytes <= kRingBytes) memcpy(g_voice.aram + off, g_voice.buffer + off, bytes);
  }
}

/* What the voice played, one entry per block: the stream block number, or -1 for silence, or -2 for
 * anything else (stale or torn data). */
static int g_played[4096];
static int g_nplayed = 0;

static int classify(const uint8_t* b) {
  static const uint8_t zero[8];
  if (memcmp(b, zero, 8) == 0) return -1;
  uint8_t want[8];
  const unsigned k = ((unsigned)b[1] << 8) | b[2];
  block_bytes(k, want);
  return (k < kBlocks && memcmp(b, want, 8) == 0) ? (int)k : -2;
}

/* One MusyX update: the voice plays kStepSamples from ARAM, then msm hears where it is (streamHandle's
 * three cases for an ADPCM stream, positions rounded down to whole blocks). */
static void voice_tick(void) {
  for (u32 s = 0; s < kStepSamples; s += 14) {
    const u32 block = ((g_voice.pos + s) % g_voice.size) / 14;
    if (g_nplayed < (int)(sizeof g_played / sizeof g_played[0])) g_played[g_nplayed++] = classify(g_voice.aram + 8 * block);
  }
  g_voice.pos = (g_voice.pos + kStepSamples) % g_voice.size;
  const u32 cpos = (g_voice.pos / 14) * 14, last = g_voice.last, size = g_voice.size;
  if (cpos == last) return;
  void* at = g_voice.buffer + (last / 14) * 8;
  u32 len;
  if (last < cpos) {
    len = g_voice.update(at, cpos - last, NULL, 0, g_voice.user);
  } else if (cpos == 0) {
    len = g_voice.update(at, size - last, NULL, 0, g_voice.user);
  } else {
    len = g_voice.update(at, size - last, g_voice.buffer, cpos, g_voice.user);
  }
  if (len != 0 && g_voice.active) g_voice.last = (last + len) % size;
}

int main(void) {
  build_file();
  CHECK(msmStreamInit("/sound/synthetic.pdt") == 0);
  CHECK(msmStreamAmemAlloc() > 0);
  CHECK(g_voice.allocated);

  MSM_STREAMPARAM param;
  memset(&param, 0, sizeof param);
  param.flag = MSM_STREAMPARAM_NONE;
  CHECK(msmStreamPlay(0, &param) >= 0);
  CHECK(g_voice.active);

  for (int t = 0; t < 2000 && g_voice.active; ++t) voice_tick();
  CHECK(g_voice.stopped);

  /* Blocks 0..339 in order, then silence until msm stopped the voice. */
  int bad = 0;
  for (int i = 0; i < g_nplayed; ++i) {
    const int want = i < kBlocks ? i : -1;
    if (g_played[i] != want) {
      if (bad < 8) fprintf(stderr, "played[%d] = %d, want %d\n", i, g_played[i], want);
      ++bad;
    }
  }
  CHECK(g_nplayed >= kBlocks);
  CHECK(bad == 0);
  if (bad) fprintf(stderr, "%d of %d played blocks wrong\n", bad, g_nplayed);

  if (g_failures == 0) printf("mp4_msm_stream: ok (%d blocks played)\n", g_nplayed);
  return g_failures == 0 ? 0 : 1;
}
