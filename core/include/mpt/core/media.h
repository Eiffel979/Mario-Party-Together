#pragma once

#include <cstdint>
#include <span>

namespace mpt {

// One finished video frame from a runtime. A runtime either hands the presenter
// CPU pixels or a GPU texture it rendered into.
//
// cpu_rgba8 (ADR 0002 rule 7): width * height pixels, tightly packed (stride = width),
// top row first, and each pixel's bytes in memory are R, G, B, A.
// gpu_texture (ADR 0002 rule 8): the contract gives runtimes no presenter device yet,
// so a handle cannot be shared. Runtimes use cpu_rgba8 until an ADR defines the
// device handshake.
struct FrameOutput {
  enum class Kind : std::uint8_t { none, cpu_rgba8, gpu_texture };

  Kind kind = Kind::none;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::span<const std::uint32_t> pixels;  // cpu_rgba8: width * height pixels, see above
  std::uint64_t gpu_texture = 0;          // gpu_texture: backend handle owned by the runtime
};

// Audio produced during the last stepFrame(), interleaved signed 16-bit PCM.
struct AudioChunk {
  std::uint32_t sample_rate = 0;
  std::uint8_t channels = 0;
  std::span<const std::int16_t> samples;
};

}  // namespace mpt
