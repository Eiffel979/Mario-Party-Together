/* THP movie decoding (ADR 0007 §4).
 *
 * Our own decoder, written from the public format: video frames are baseline
 * JPEG (8-bit, Huffman, 4:2:0, optional restart intervals) and audio frames are
 * GameCube DSP ADPCM with per-frame coefficients. Everything is integer
 * arithmetic, so results are identical on every platform and compiler. The
 * console's float IDCT gives slightly different pixels; that only affects the
 * picture (no game logic reads decoded pixels, mp4.md §8). ADPCM is exact.
 *
 * Decoding runs synchronously in the calling game thread. MP4 calls it from its
 * OS idle function, which the SDK runs once per frame step (OSSetIdleFunction),
 * so decoding happens inside stepFrame. Movie audio reaches the output through
 * the game's AI DMA callback (mpt_gc/ai.h), in the same stream as MusyX.
 *
 * Multi-byte fields in THP data are big-endian, as on the disc.
 */
#ifndef MPT_GC_THP_H
#define MPT_GC_THP_H

#include "mpt_gc/types.h"

MPT_GC_EXTERN_C_BEGIN

#define THP_OK 0
#define THP_ERROR_NOT_INITIALIZED 1
#define THP_ERROR_BAD_STREAM 2   /* malformed or truncated JPEG data */
#define THP_ERROR_UNSUPPORTED 3  /* not 8-bit baseline 4:2:0 with sizes a multiple of 16 */

/* THPAudioDecode layouts. */
#define THP_AUDIO_INTERLEAVE 0   /* L R L R ... */
#define THP_AUDIO_NOINTERLEAVE 1 /* all L samples, then all R samples */

/* The fixed header of one audio frame (big-endian on disc), followed by
 * `channelSize` bytes of left-channel ADPCM and then `channelSize` bytes of right. */
#define THP_AUDIO_HEADER_SIZE 80u

BOOL THPInit(void);
/* Decodes one JPEG video frame. Y is written as a GX I8 texture (8x4 tiles) of
 * the frame size; U and V as I8 textures of half the width and height. `work`
 * is not needed and may be NULL. Returns THP_OK or an error above. */
s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV, void* work);
/* Decodes one audio frame into s16 samples (host byte order); returns the
 * number of samples per channel. */
u32 THPAudioDecode(s16* audioBuffer, u8* audioFrame, s32 flag);

MPT_GC_EXTERN_C_END

#endif /* MPT_GC_THP_H */
