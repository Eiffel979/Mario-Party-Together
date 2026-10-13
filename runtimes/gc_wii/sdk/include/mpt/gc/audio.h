// Sound-engine attachment for the SDK layer (ADR 0007 §4, ADR 0008 §8).
//
// A title's sound engine (MusyX for MP4, through its PC synth in pull mode)
// runs as game code. The SDK steps it on virtual AI-DMA ticks inside
// stepFrame: never on a host thread or timer, and never through the engine's
// own device backend. With null audio output the engine still renders every
// block, because its state (voices, sequencer, streams, MIDI controllers) is
// game-visible; only the host's copy of the PCM is dropped.
#pragma once

#include <cstdint>

#include "mpt/core/state.h"

namespace mpt::gc {

inline constexpr std::uint32_t kAiRate32k = 32000;
inline constexpr std::uint32_t kAiRate48k = 48000;
// The block an attached engine renders per AI tick: 5 ms at 32 kHz, the size
// of MusyX's AI-DMA blocks on the console. 160 * 40.5 MHz / 32 kHz is exact.
inline constexpr std::uint32_t kAiEngineBlockFrames = 160;
inline constexpr std::uint32_t kAiEngineBlockTicks = 202'500;

struct AudioEngine {
  // Renders exactly `frames` interleaved s16 stereo frames at 32 kHz into
  // `out` (a block in the arena) and advances the engine by that much time.
  // Runs in interrupt context during stepFrame; it may call SDK functions that
  // never block (ARAM DMA, for example). Must be deterministic: no host clock,
  // RNG, threads or host libm (ADR 0008).
  void (*render)(void* user, std::int16_t* out, std::uint32_t frames) = nullptr;
  // Hashes the engine's game-visible state by value: pointers as arena offsets,
  // never raw struct bytes (ADR 0008 §8). Called by Sdk::hashState.
  void (*hash)(void* user, StateHasher& h) = nullptr;
  void* user = nullptr;

  bool attached() const { return render != nullptr; }
};

}  // namespace mpt::gc
