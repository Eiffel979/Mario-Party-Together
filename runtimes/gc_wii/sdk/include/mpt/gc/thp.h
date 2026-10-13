// THP container and bounded decoders (ADR 0007 §4).
//
// The C API in mpt_gc/thp.h is what game code calls; it is unbounded like the
// console's. These functions take explicit spans, for players in the SDK or a
// title module, and for tests. Pure functions of their input: no state, no
// allocation, no floating point.
//
// Container layout (all big-endian), as written by the THP tools:
//   header (48 bytes): "THP\0", version (0x00010000 or 0x00011000), bufSize,
//     audioMaxSamples, frameRate (f32), numFrames, firstFrameSize, movieDataSize,
//     compInfoDataOffset, offsetDataOffset (0 = none), movieDataOffset,
//     finalFrameDataOffset
//   component info: numComponents, frameComp[16] (0 video, 1 audio, 0xFF none),
//     then per component: video {xSize, ySize[, videoType in 1.1]},
//     audio {sndChannels, sndFrequency, sndNumSamples[, sndNumTracks in 1.1]}
//   frames: nextTotalSize, prevTotalSize, one size per component, then the
//     component data in component order (audio once per track).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "mpt/core/result.h"

namespace mpt::gc::thp {

inline constexpr std::uint32_t kVersion10 = 0x0001'0000;
inline constexpr std::uint32_t kVersion11 = 0x0001'1000;
inline constexpr std::uint32_t kHeaderSize = 48;
inline constexpr std::uint8_t kCompVideo = 0;
inline constexpr std::uint8_t kCompAudio = 1;
inline constexpr std::uint8_t kCompNone = 0xFF;

struct Info {
  std::uint32_t version = 0;
  std::uint32_t buf_size = 0;
  std::uint32_t audio_max_samples = 0;
  std::uint32_t frame_rate_bits = 0;  // IEEE f32 bits; never converted inside the SDK
  std::uint32_t num_frames = 0;
  std::uint32_t first_frame_size = 0;
  std::uint32_t movie_data_size = 0;
  std::uint32_t offset_data_offset = 0;
  std::uint32_t movie_data_offset = 0;
  std::uint32_t final_frame_offset = 0;
  std::uint32_t num_components = 0;
  std::array<std::uint8_t, 16> components{};
  bool has_video = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t video_type = 0;
  bool has_audio = false;
  std::uint32_t channels = 0;
  std::uint32_t frequency = 0;
  std::uint32_t num_samples = 0;
  std::uint32_t num_tracks = 0;
};

struct Frame {
  std::uint32_t next_size = 0;  // total size of the next frame
  std::uint32_t prev_size = 0;
  std::span<const std::uint8_t> video;
  std::array<std::span<const std::uint8_t>, 8> audio{};  // one per track
  std::uint32_t audio_tracks = 0;
};

// Parses and bounds-checks the header and component info.
Result parse(std::span<const std::uint8_t> file, Info& out);
// The frame of `size` bytes at `offset` (the first is at movie_data_offset,
// first_frame_size long; each frame gives the next one's size).
Result frameAt(std::span<const std::uint8_t> file, const Info& info, std::uint64_t offset, std::uint32_t size,
               Frame& out);

// Bounded video decode. Planes are GX I8 textures (8x4 tiles): Y is
// width x height, U and V half of each. Returns THP_OK or a THP_ERROR_*.
std::int32_t decodeVideo(std::span<const std::uint8_t> jpeg, std::uint8_t* y, std::uint8_t* u, std::uint8_t* v,
                         std::uint32_t* width = nullptr, std::uint32_t* height = nullptr);
// Bounded audio decode; returns samples per channel, or 0 if the frame is malformed.
std::uint32_t decodeAudio(std::span<const std::uint8_t> frame, std::int16_t* out, std::int32_t flag);

// Byte offset of pixel (x, y) in an I8 texture of the given width (multiple of 8).
inline std::size_t i8Offset(std::uint32_t width, std::uint32_t x, std::uint32_t y) {
  return (std::size_t{y / 4} * (width / 8) + x / 8) * 32 + (y % 4) * 8 + x % 8;
}

// The decoder's integer DCT basis: round(8192 * C(u)/2 * cos((2x+1)u*pi/16)),
// C(0) = 1/sqrt(2). The JPEG forward and inverse transforms both use it.
extern const std::int32_t kDctBasis[8][8];
// Zigzag index -> natural (row-major) coefficient index.
extern const std::uint8_t kZigzag[64];

}  // namespace mpt::gc::thp
