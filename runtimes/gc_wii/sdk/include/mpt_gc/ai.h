/* AI: audio DMA to the output, driven by virtual AI ticks (ADR 0007 §4, ADR 0008 §2).
 *
 * The console's AI interface plays a block of s16 stereo samples from main RAM
 * and raises an interrupt when it starts the next one. Here a block "plays" at a
 * virtual AI tick: its length in frames at the DSP sample rate, in timer ticks,
 * with an integer remainder accumulator (5 ms = 202,500 ticks for MusyX's
 * 160-frame blocks at 32 kHz). At each tick, the registered DMA callback runs in
 * interrupt context (it typically refills the block or chains another one), then
 * the block at the current DMA address is handed to the output. Null audio
 * output drops that copy only: callbacks, engine rendering and every register
 * stay the same.
 *
 * When the runtime attaches a sound engine (SdkConfig::audio_engine, MusyX's PC
 * synth in pull mode), the engine plays the part MusyX's own AI hookup plays on
 * the console: DMA runs from boot, the "previous" callback returned by the
 * first AIRegisterDMACallback is the engine's, and calling it renders the next
 * block and makes it the DMA source. A game callback that chains to it (THP
 * movie audio) can then mix into AIGetDMAStartAddr(), as on the console.
 *
 * Every function here is an SDK call boundary.
 */
#ifndef MPT_GC_AI_H
#define MPT_GC_AI_H

#include "mpt_gc/types.h"

MPT_GC_EXTERN_C_BEGIN

#define AI_SAMPLERATE_32KHZ 0u
#define AI_SAMPLERATE_48KHZ 1u
#define AI_STREAM_STOP 0u
#define AI_STREAM_START 1u

typedef void (*AIDCallback)(void);
typedef void (*AISCallback)(u32 count);

void AIInit(u8* stack);
BOOL AICheckInit(void);
void AIReset(void);
/* Returns the previous callback (the attached engine's, the first time). */
AIDCallback AIRegisterDMACallback(AIDCallback callback);
/* `start_addr` is a host pointer to s16 stereo samples (host byte order), passed as an integer. */
void AIInitDMA(uintptr_t start_addr, u32 length);
void AIStartDMA(void);
void AIStopDMA(void);
BOOL AIGetDMAEnableFlag(void);
uintptr_t AIGetDMAStartAddr(void);
u32 AIGetDMALength(void);
/* Bytes of the current block not yet played, from virtual time, in 32-byte units. */
u32 AIGetDMABytesLeft(void);
void AISetDSPSampleRate(u32 rate);
u32 AIGetDSPSampleRate(void);

/* DVD audio streaming: stored registers only (nothing is streamed from the disc). */
AISCallback AIRegisterStreamCallback(AISCallback callback);
void AISetStreamPlayState(u32 state);
u32 AIGetStreamPlayState(void);
void AISetStreamSampleRate(u32 rate);
u32 AIGetStreamSampleRate(void);
void AISetStreamVolLeft(u8 volume);
void AISetStreamVolRight(u8 volume);
u8 AIGetStreamVolLeft(void);
u8 AIGetStreamVolRight(void);
void AISetStreamTrigger(u32 trigger);
u32 AIGetStreamTrigger(void);
u32 AIGetStreamSampleCount(void);
void AIResetStreamSampleCount(void);

MPT_GC_EXTERN_C_END

#endif /* MPT_GC_AI_H */
