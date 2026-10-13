// AI (audio DMA) and the sound-engine attachment (ADR 0007 §4, ADR 0008 §2, §8).
//
// One virtual AI tick per DMA block. The tick's deadline is the block's length
// in timer ticks at the DSP sample rate, carried with an integer remainder, so
// the number of samples produced is a pure function of virtual time. At a tick
// (interrupt context): the block that just played goes to the output, then the
// DMA callback runs and sets up the next block, then the next tick is scheduled
// from the (possibly new) block length.
#include <cstring>

#include "sdk_internal.h"

namespace mpt::gc::detail {
namespace {

std::uint32_t rateHz(std::uint32_t dsp_rate) { return dsp_rate == AI_SAMPLERATE_48KHZ ? kAiRate48k : kAiRate32k; }

// Length of the current block in timer ticks, with the remainder carried.
std::int64_t nextPeriod(AiState& a) {
  const std::uint32_t rate = rateHz(a.dsp_rate);
  const std::int64_t num = std::int64_t{a.dma_len / 4} * kTimerClockHz;
  std::int64_t whole = num / rate;
  a.period_acc += num % rate;
  if (a.period_acc >= rate) {
    ++whole;
    a.period_acc -= rate;
  }
  return whole;
}

void scheduleTick(AiState& a, std::int64_t from) {
  if (a.dma_len < 4) {
    a.dma_enabled = 0;  // nothing to play: DMA stops
    return;
  }
  a.period = nextPeriod(a);
  a.next_deadline = from + a.period;
  schedule(a.next_deadline, Source::ai, EventKind::ai_dma, nullptr, nullptr, 0);
}

// The engine's AI callback: render the next block and make it the DMA source.
// This is the role MusyX's own AI callback plays on the console.
void aiEngineCallback() {
  auto& I = impl();
  AiState& a = st().ai;
  std::int16_t* block = a.engine_buf[a.engine_block];
  I.config.audio_engine.render(I.config.audio_engine.user, block, kAiEngineBlockFrames);
  a.dma_addr = block;
  a.dma_len = kAiEngineBlockSamples * sizeof(std::int16_t);
  a.engine_block ^= 1;
}

}  // namespace

void aiBoot(Sdk::Impl& I) {
  AiState& a = I.st->ai;
  a.dsp_rate = AI_SAMPLERATE_32KHZ;
  if (!I.config.audio_engine.attached()) return;
  // As after the engine's init on the console: its callback is registered and
  // DMA runs from its (silent) first block.
  a.engine_attached = 1;
  a.callback = &aiEngineCallback;
  a.dma_addr = a.engine_buf[0];
  a.dma_len = kAiEngineBlockSamples * sizeof(std::int16_t);
  a.engine_block = 1;
  a.dma_enabled = 1;
  scheduleTick(a, I.st->now);
}

void aiDispatch(const Event&) {
  auto& I = impl();
  AiState& a = I.st->ai;
  if (!a.dma_enabled) return;
  ++a.ticks;
  // The block that just finished playing.
  if (a.dma_addr && a.dma_len >= 4) {
    const std::uint32_t samples = (a.dma_len / 4) * 2;
    a.frames_out += a.dma_len / 4;
    if (I.config.audio_output) {
      const std::size_t at = I.pcm.size();
      I.pcm.resize(at + samples);
      std::memcpy(I.pcm.data() + at, a.dma_addr, samples * sizeof(std::int16_t));
    }
  }
  const std::int64_t deadline = a.next_deadline;
  if (a.callback) a.callback();
  if (I.st->faulted) return;
  if (a.dma_enabled) scheduleTick(a, deadline);
}

void aiHash(StateHasher& h) {
  const auto& I = impl();
  const AiState& a = I.st->ai;
  h.update_u64(0x4D50'5447'4341'4931ull);  // "MPTGCAI1"
  h.update_u64(a.initialized | (std::uint64_t{a.dma_enabled} << 8) | (std::uint64_t{a.engine_attached} << 16) |
               (std::uint64_t{a.engine_block} << 24) | (std::uint64_t{a.dsp_rate} << 32));
  h.update_u64(hashPointer(a.dma_addr));
  h.update_u64(a.dma_len);
  h.update_u64(hashFunction(reinterpret_cast<const void*>(a.callback)));
  h.update_u64(static_cast<std::uint64_t>(a.next_deadline));
  h.update_u64(static_cast<std::uint64_t>(a.period));
  h.update_u64(static_cast<std::uint64_t>(a.period_acc));
  h.update_u64(a.ticks);
  h.update_u64(a.frames_out);
  h.update_u64(a.stream_play_state | (std::uint64_t{a.stream_rate} << 8) | (std::uint64_t{a.stream_vol_l} << 16) |
               (std::uint64_t{a.stream_vol_r} << 24) | (std::uint64_t{a.stream_trigger} << 32));
  h.update_u64(hashFunction(reinterpret_cast<const void*>(a.stream_callback)));
  if (I.config.audio_engine.attached() && I.config.audio_engine.hash)
    I.config.audio_engine.hash(I.config.audio_engine.user, h);
}

}  // namespace mpt::gc::detail

using namespace mpt::gc;
using namespace mpt::gc::detail;

extern "C" {

void AIInit(u8* stack) {
  (void)stack;
  boundary();
  st().ai.initialized = 1;
}

BOOL AICheckInit(void) {
  boundary();
  return st().ai.initialized ? TRUE : FALSE;
}

void AIReset(void) {
  boundary();
  st().ai.initialized = 0;
}

AIDCallback AIRegisterDMACallback(AIDCallback callback) {
  boundary();
  AiState& a = st().ai;
  const AIDCallback old = a.callback;
  a.callback = callback;
  return old;
}

void AIInitDMA(uintptr_t start_addr, u32 length) {
  boundary();
  AiState& a = st().ai;
  a.dma_addr = reinterpret_cast<void*>(start_addr);
  a.dma_len = length & ~31u;  // the console's 32-byte DMA granularity
}

void AIStartDMA(void) {
  boundary();
  AiState& a = st().ai;
  if (a.dma_enabled) return;
  a.dma_enabled = 1;
  a.period_acc = 0;
  scheduleTick(a, st().now);
}

void AIStopDMA(void) {
  boundary();
  AiState& a = st().ai;
  a.dma_enabled = 0;
  cancelEvents(EventKind::ai_dma, nullptr);
}

BOOL AIGetDMAEnableFlag(void) {
  boundary();
  return st().ai.dma_enabled ? TRUE : FALSE;
}

uintptr_t AIGetDMAStartAddr(void) {
  boundary();
  return reinterpret_cast<uintptr_t>(st().ai.dma_addr);
}

u32 AIGetDMALength(void) {
  boundary();
  return st().ai.dma_len;
}

u32 AIGetDMABytesLeft(void) {
  boundary();
  const AiState& a = st().ai;
  if (!a.dma_enabled || a.period <= 0) return 0;
  std::int64_t left = a.next_deadline - st().now;
  if (left < 0) left = 0;
  if (left > a.period) left = a.period;
  return static_cast<u32>(left * a.dma_len / a.period) & ~31u;
}

void AISetDSPSampleRate(u32 rate) {
  boundary();
  st().ai.dsp_rate = rate == AI_SAMPLERATE_48KHZ ? AI_SAMPLERATE_48KHZ : AI_SAMPLERATE_32KHZ;
}

u32 AIGetDSPSampleRate(void) {
  boundary();
  return st().ai.dsp_rate;
}

AISCallback AIRegisterStreamCallback(AISCallback callback) {
  boundary();
  const AISCallback old = st().ai.stream_callback;
  st().ai.stream_callback = callback;
  return old;
}

void AISetStreamPlayState(u32 state) {
  boundary();
  st().ai.stream_play_state = state ? AI_STREAM_START : AI_STREAM_STOP;
}

u32 AIGetStreamPlayState(void) {
  boundary();
  return st().ai.stream_play_state;
}

void AISetStreamSampleRate(u32 rate) {
  boundary();
  st().ai.stream_rate = rate ? 1u : 0u;
}

u32 AIGetStreamSampleRate(void) {
  boundary();
  return st().ai.stream_rate;
}

void AISetStreamVolLeft(u8 volume) {
  boundary();
  st().ai.stream_vol_l = volume;
}

void AISetStreamVolRight(u8 volume) {
  boundary();
  st().ai.stream_vol_r = volume;
}

u8 AIGetStreamVolLeft(void) {
  boundary();
  return st().ai.stream_vol_l;
}

u8 AIGetStreamVolRight(void) {
  boundary();
  return st().ai.stream_vol_r;
}

void AISetStreamTrigger(u32 trigger) {
  boundary();
  st().ai.stream_trigger = trigger;
}

u32 AIGetStreamTrigger(void) {
  boundary();
  return st().ai.stream_trigger;
}

u32 AIGetStreamSampleCount(void) {
  boundary();
  return 0;  // nothing is streamed from the disc
}

void AIResetStreamSampleCount(void) { boundary(); }

}  // extern "C"
