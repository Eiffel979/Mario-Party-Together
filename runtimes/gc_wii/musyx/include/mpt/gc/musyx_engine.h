// MusyX attached to the SDK as a pull-mode AudioEngine (ADR 0007 §4).
//
// Usage by a title runtime (Mp4Runtime, P1-16), before Sdk::create:
//
//   mpt::gc::MusyxEngine::reset();          // musyx statics back to their boot image
//   SdkConfig cfg;
//   cfg.audio_engine = mpt::gc::MusyxEngine::engine();
//   for (auto r : mpt::gc::MusyxEngine::stateRegions()) cfg.state_regions.push_back(r);
//
// Game code (MP4's msm) then calls sndSetHooks/sndInit/... as on the console.
// The SDK renders 160 frames per 5 ms AI tick inside stepFrame through
// sndPCRender, so the sequencer and voices advance only there, identically with
// null and real audio output. MusyX's PC renderer is configured for 32 kHz stereo.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "mpt/gc/audio.h"

namespace mpt::gc {

struct MusyxEngine {
  // The AudioEngine for SdkConfig::audio_engine: render = sndPCRender (silence
  // before sndInit), hash = the musyx manifest by value (musyx_glue.c).
  static AudioEngine engine();
  // musyx's writable statics (two sections), for SdkConfig::state_regions.
  static std::vector<std::span<std::byte>> stateRegions();
  // Restores musyx's statics to the image they had at process start (captured on
  // the first call of any MusyxEngine function), and configures the renderer.
  // Call before each Sdk::create, so every session boots musyx identically.
  static void reset();
};

}  // namespace mpt::gc
