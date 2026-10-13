#pragma once

// Aspect-correct placement of a game frame in the window (P1-06 c). Pure integer math.

#include <cstdint>

namespace mpt::shell {

struct PixelRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  friend bool operator==(const PixelRect&, const PixelRect&) = default;
};

struct Aspect {
  std::uint32_t num = 4;
  std::uint32_t den = 3;
};

// The display aspect: 16:9 when the session runs widescreen (the runtime renders
// anamorphic), otherwise 4:3. Console output is 4:3 whatever the frame's pixel size
// (an EFB such as 640x448 has non-square pixels), so the frame's own width:height is never
// used. Titles that are natively 16:9 (MP9 on, later phases) will need the runtime to
// report its display aspect, which is a core contract change for its own ADR.
inline Aspect display_aspect(bool widescreen) { return widescreen ? Aspect{16, 9} : Aspect{4, 3}; }

// The largest rectangle of `aspect` that fits in dst_w x dst_h, centred. Empty if the
// window is empty.
inline PixelRect letterbox(int dst_w, int dst_h, Aspect aspect) {
  if (dst_w <= 0 || dst_h <= 0 || aspect.num == 0 || aspect.den == 0) return {};
  const std::uint64_t w = static_cast<std::uint64_t>(dst_w);
  const std::uint64_t h = static_cast<std::uint64_t>(dst_h);
  std::uint64_t out_w = w;
  std::uint64_t out_h = w * aspect.den / aspect.num;
  if (out_h > h) {
    out_h = h;
    out_w = h * aspect.num / aspect.den;
  }
  PixelRect r;
  r.w = static_cast<int>(out_w);
  r.h = static_cast<int>(out_h);
  r.x = (dst_w - r.w) / 2;
  r.y = (dst_h - r.h) / 2;
  return r;
}

}  // namespace mpt::shell
