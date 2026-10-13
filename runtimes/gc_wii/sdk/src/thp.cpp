// THP decoding (ADR 0007 §4): baseline JPEG video and DSP-ADPCM audio, integer only.
//
// Written from the public JPEG specification (ITU T.81: sequential DCT,
// Huffman, 8-bit) and the documented DSP-ADPCM frame format. No allocation and
// no floating point, so the output is a pure function of the input bytes.
#include <algorithm>
#include <cstring>

#include "mpt/gc/thp.h"
#include "mpt_gc/thp.h"
#include "sdk_internal.h"

namespace mpt::gc::thp {

const std::int32_t kDctBasis[8][8] = {
    {2896, 2896, 2896, 2896, 2896, 2896, 2896, 2896},
    {4017, 3406, 2276, 799, -799, -2276, -3406, -4017},
    {3784, 1567, -1567, -3784, -3784, -1567, 1567, 3784},
    {3406, -799, -4017, -2276, 2276, 4017, 799, -3406},
    {2896, -2896, -2896, 2896, 2896, -2896, -2896, 2896},
    {2276, -4017, 799, 3406, -3406, -799, 4017, -2276},
    {1567, -3784, 3784, -1567, -1567, 3784, -3784, 1567},
    {799, -2276, 3406, -4017, 4017, -3406, 2276, -799},
};

const std::uint8_t kZigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

namespace {

std::uint32_t be32(const std::uint8_t* p) {
  return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
}
std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

// ---- JPEG ------------------------------------------------------------------------------

struct Huffman {
  bool defined = false;
  std::uint8_t values[256];
  std::int32_t maxcode[18];  // per code length 1..16; [17] sentinel
  std::int32_t valptr[17];
  std::int32_t mincode[17];
};

struct Component {
  std::uint8_t id;
  std::uint8_t h, v;
  std::uint8_t tq;
  std::uint8_t td, ta;
  std::int32_t pred;
};

struct BitReader {
  const std::uint8_t* p;
  const std::uint8_t* end;
  std::uint32_t acc = 0;
  int bits = 0;
  bool hit_marker = false;
  bool overrun = false;

  int bit() {
    if (bits == 0) {
      std::uint8_t b = 0;
      if (hit_marker || p >= end) {
        overrun = overrun || p >= end;
        b = 0;  // past a marker the stream is padded with zeros
      } else {
        b = *p++;
        if (b == 0xFF) {
          if (p >= end) {
            overrun = true;
          } else if (*p == 0x00) {
            ++p;  // stuffed byte
          } else {
            hit_marker = true;  // a marker: leave p on it
            --p;
            b = 0;
          }
        }
      }
      acc = b;
      bits = 8;
    }
    --bits;
    return static_cast<int>((acc >> bits) & 1);
  }
  std::int32_t receive(int n) {
    std::int32_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 1) | bit();
    return v;
  }
  void align() { bits = 0; }
};

std::int32_t extend(std::int32_t v, int n) { return (n == 0 || v >= (1 << (n - 1))) ? v : v - (1 << n) + 1; }

int decodeSymbol(BitReader& br, const Huffman& h) {
  std::int32_t code = br.bit();
  for (int len = 1; len <= 16; ++len) {
    if (h.maxcode[len] >= 0 && code <= h.maxcode[len])
      return h.values[h.valptr[len] + code - h.mincode[len]];
    code = (code << 1) | br.bit();
  }
  return -1;
}

bool buildHuffman(Huffman& h, const std::uint8_t counts[16], const std::uint8_t* symbols, int total) {
  std::memcpy(h.values, symbols, static_cast<std::size_t>(total));
  std::int32_t code = 0;
  int k = 0;
  for (int len = 1; len <= 16; ++len) {
    const int n = counts[len - 1];
    if (n) {
      h.valptr[len] = k;
      h.mincode[len] = code;
      code += n;
      k += n;
      h.maxcode[len] = code - 1;
      if (code > (1 << len)) return false;  // over-subscribed
    } else {
      h.maxcode[len] = -1;
    }
    code <<= 1;
  }
  h.maxcode[17] = 0x7FFF'FFFF;
  h.defined = true;
  return true;
}

std::uint8_t clampPixel(std::int64_t v) { return static_cast<std::uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

// out[y][x] = sum_u sum_v B[v][y] B[u][x] F[v][u] / 2^26 + 128, in integers.
void idct(const std::int32_t* coef, std::uint8_t* plane, std::uint32_t width, std::uint32_t x0, std::uint32_t y0) {
  std::int64_t tmp[8][8];  // tmp[v][x]
  for (int v = 0; v < 8; ++v) {
    for (int x = 0; x < 8; ++x) {
      std::int64_t s = 0;
      for (int u = 0; u < 8; ++u) s += std::int64_t{coef[v * 8 + u]} * kDctBasis[u][x];
      tmp[v][x] = s;  // scale 2^13
    }
  }
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      std::int64_t s = 0;
      for (int v = 0; v < 8; ++v) s += tmp[v][x] * kDctBasis[v][y];
      // Round to nearest (ties up), the same on every platform: arithmetic shift of a 64-bit value.
      const std::int64_t value = (s + (std::int64_t{1} << 25)) >> 26;
      plane[i8Offset(width, x0 + static_cast<std::uint32_t>(x), y0 + static_cast<std::uint32_t>(y))] =
          clampPixel(value + 128);
    }
  }
}

struct Decoder {
  std::uint16_t q[4][64]{};  // in zigzag order
  bool q_defined[4]{};
  Huffman dc[4], ac[4];
  Component comp[3]{};
  std::uint32_t width = 0, height = 0;
  std::uint32_t restart = 0;
  bool have_frame = false;
};

std::int32_t decodeBlock(BitReader& br, Decoder& d, Component& c, std::int32_t out[64]) {
  std::memset(out, 0, 64 * sizeof(std::int32_t));
  const Huffman& dc = d.dc[c.td];
  const Huffman& ac = d.ac[c.ta];
  const std::uint16_t* q = d.q[c.tq];
  const int t = decodeSymbol(br, dc);
  if (t < 0 || t > 11) return THP_ERROR_BAD_STREAM;
  c.pred += extend(br.receive(t), t);
  out[0] = c.pred * q[0];
  for (int k = 1; k < 64;) {
    const int rs = decodeSymbol(br, ac);
    if (rs < 0) return THP_ERROR_BAD_STREAM;
    const int r = rs >> 4, s = rs & 15;
    if (s == 0) {
      if (r != 15) break;  // EOB
      k += 16;
      continue;
    }
    k += r;
    if (k > 63) return THP_ERROR_BAD_STREAM;
    out[kZigzag[k]] = extend(br.receive(s), s) * q[k];
    ++k;
  }
  return br.overrun ? THP_ERROR_BAD_STREAM : THP_OK;
}

std::int32_t decodeScan(const std::uint8_t*& p, const std::uint8_t* end, Decoder& d, std::uint8_t* y, std::uint8_t* u,
                        std::uint8_t* v) {
  const std::uint32_t mcux = d.width / 16, mcuy = d.height / 16;
  BitReader br{p, end};
  for (Component& c : d.comp) c.pred = 0;
  std::int32_t block[64];
  std::uint32_t since_restart = 0;
  std::uint8_t next_rst = 0;
  for (std::uint32_t my = 0; my < mcuy; ++my) {
    for (std::uint32_t mx = 0; mx < mcux; ++mx) {
      if (d.restart && since_restart == d.restart) {
        // Expect RSTn here: realign, check the marker, reset predictions.
        br.align();
        if (br.p + 1 >= end || br.p[0] != 0xFF || br.p[1] != 0xD0 + next_rst) return THP_ERROR_BAD_STREAM;
        br.p += 2;
        br.hit_marker = false;
        next_rst = (next_rst + 1) & 7;
        since_restart = 0;
        for (Component& c : d.comp) c.pred = 0;
      }
      for (std::uint32_t b = 0; b < 4; ++b) {
        if (std::int32_t r = decodeBlock(br, d, d.comp[0], block); r != THP_OK) return r;
        idct(block, y, d.width, mx * 16 + (b & 1) * 8, my * 16 + (b >> 1) * 8);
      }
      if (std::int32_t r = decodeBlock(br, d, d.comp[1], block); r != THP_OK) return r;
      idct(block, u, d.width / 2, mx * 8, my * 8);
      if (std::int32_t r = decodeBlock(br, d, d.comp[2], block); r != THP_OK) return r;
      idct(block, v, d.width / 2, mx * 8, my * 8);
      ++since_restart;
    }
  }
  // Skip to the marker that ends the scan.
  const std::uint8_t* q = br.p;
  while (q + 1 < end && !(q[0] == 0xFF && q[1] != 0x00 && !(q[1] >= 0xD0 && q[1] <= 0xD7))) ++q;
  p = q;
  return THP_OK;
}

}  // namespace

std::int32_t decodeVideo(std::span<const std::uint8_t> jpeg, std::uint8_t* y, std::uint8_t* u, std::uint8_t* v,
                         std::uint32_t* width, std::uint32_t* height) {
  Decoder d;
  const std::uint8_t* p = jpeg.data();
  const std::uint8_t* end = p + jpeg.size();
  if (jpeg.size() < 4 || p[0] != 0xFF || p[1] != 0xD8) return THP_ERROR_BAD_STREAM;
  p += 2;
  bool decoded = false;
  while (p + 1 < end) {
    if (p[0] != 0xFF) return THP_ERROR_BAD_STREAM;
    const std::uint8_t m = p[1];
    p += 2;
    if (m == 0xFF) {  // fill byte
      --p;
      continue;
    }
    if (m == 0xD9) break;  // EOI
    if (m >= 0xD0 && m <= 0xD7) continue;
    if (p + 2 > end) return THP_ERROR_BAD_STREAM;
    const std::uint32_t len = be16(p);
    if (len < 2 || p + len > end) return THP_ERROR_BAD_STREAM;
    const std::uint8_t* seg = p + 2;
    const std::uint8_t* seg_end = p + len;
    p = seg_end;
    switch (m) {
      case 0xDB:  // DQT
        while (seg < seg_end) {
          const std::uint8_t pq = seg[0] >> 4, tq = seg[0] & 15;
          if (pq != 0 || tq > 3) return THP_ERROR_UNSUPPORTED;
          if (seg + 65 > seg_end) return THP_ERROR_BAD_STREAM;
          for (int k = 0; k < 64; ++k) d.q[tq][k] = seg[1 + k];
          d.q_defined[tq] = true;
          seg += 65;
        }
        break;
      case 0xC4:  // DHT
        while (seg < seg_end) {
          if (seg + 17 > seg_end) return THP_ERROR_BAD_STREAM;
          const std::uint8_t tc = seg[0] >> 4, th = seg[0] & 15;
          if (tc > 1 || th > 3) return THP_ERROR_BAD_STREAM;
          int total = 0;
          for (int i = 0; i < 16; ++i) total += seg[1 + i];
          if (total > 256 || seg + 17 + total > seg_end) return THP_ERROR_BAD_STREAM;
          Huffman& h = tc ? d.ac[th] : d.dc[th];
          if (!buildHuffman(h, seg + 1, seg + 17, total)) return THP_ERROR_BAD_STREAM;
          seg += 17 + total;
        }
        break;
      case 0xDD:  // DRI
        if (len != 4) return THP_ERROR_BAD_STREAM;
        d.restart = be16(seg);
        break;
      case 0xC0: {  // SOF0, baseline
        if (len != 17 || seg[0] != 8 || seg[5] != 3) return THP_ERROR_UNSUPPORTED;
        d.height = be16(seg + 1);
        d.width = be16(seg + 3);
        for (int i = 0; i < 3; ++i) {
          Component& c = d.comp[i];
          c.id = seg[6 + 3 * i];
          c.h = seg[7 + 3 * i] >> 4;
          c.v = seg[7 + 3 * i] & 15;
          c.tq = seg[8 + 3 * i];
          if (c.tq > 3) return THP_ERROR_BAD_STREAM;
        }
        if (d.comp[0].h != 2 || d.comp[0].v != 2 || d.comp[1].h != 1 || d.comp[1].v != 1 || d.comp[2].h != 1 ||
            d.comp[2].v != 1)
          return THP_ERROR_UNSUPPORTED;
        if (d.width == 0 || d.height == 0 || d.width % 16 || d.height % 16) return THP_ERROR_UNSUPPORTED;
        d.have_frame = true;
        break;
      }
      case 0xDA: {  // SOS
        if (!d.have_frame || seg[0] != 3 || len != 12) return THP_ERROR_UNSUPPORTED;
        for (int i = 0; i < 3; ++i) {
          const std::uint8_t cid = seg[1 + 2 * i];
          Component* c = nullptr;
          for (Component& cc : d.comp)
            if (cc.id == cid) c = &cc;
          if (c != &d.comp[i]) return THP_ERROR_UNSUPPORTED;  // interleaved Y, U, V in frame order
          c->td = seg[2 + 2 * i] >> 4;
          c->ta = seg[2 + 2 * i] & 15;
          if (c->td > 3 || c->ta > 3 || !d.dc[c->td].defined || !d.ac[c->ta].defined || !d.q_defined[c->tq])
            return THP_ERROR_BAD_STREAM;
        }
        if (seg[7] != 0 || seg[8] != 63 || seg[9] != 0) return THP_ERROR_UNSUPPORTED;  // sequential only
        if (std::int32_t r = decodeScan(p, end, d, y, u, v); r != THP_OK) return r;
        decoded = true;
        break;
      }
      default:
        if (m >= 0xC1 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) return THP_ERROR_UNSUPPORTED;
        break;  // APPn, COM and others: skipped
    }
  }
  if (!decoded) return THP_ERROR_BAD_STREAM;
  if (width) *width = d.width;
  if (height) *height = d.height;
  return THP_OK;
}

// ---- Audio --------------------------------------------------------------------------------

std::uint32_t decodeAudio(std::span<const std::uint8_t> frame, std::int16_t* out, std::int32_t flag) {
  if (frame.size() < THP_AUDIO_HEADER_SIZE) return 0;
  const std::uint8_t* h = frame.data();
  const std::uint32_t channel_size = be32(h);
  const std::uint32_t samples = be32(h + 4);
  const std::uint64_t needed_bytes = (std::uint64_t{samples} + 13) / 14 * 8;
  if (needed_bytes > channel_size || THP_AUDIO_HEADER_SIZE + std::uint64_t{channel_size} * 2 > frame.size()) return 0;
  for (std::uint32_t ch = 0; ch < 2; ++ch) {
    std::int16_t coef[16];
    for (int i = 0; i < 16; ++i) coef[i] = static_cast<std::int16_t>(be16(h + 8 + ch * 32 + 2 * i));
    std::int32_t yn1 = static_cast<std::int16_t>(be16(h + 72 + ch * 4));
    std::int32_t yn2 = static_cast<std::int16_t>(be16(h + 74 + ch * 4));
    const std::uint8_t* data = h + THP_AUDIO_HEADER_SIZE + std::size_t{ch} * channel_size;
    for (std::uint32_t i = 0; i < samples; ++i) {
      const std::uint8_t* f = data + (i / 14) * 8;
      const std::uint32_t n = i % 14;
      const std::uint8_t ps = f[0];
      const std::int32_t scale = 1 << (ps & 15);
      const std::int32_t c1 = coef[((ps >> 4) & 7) * 2], c2 = coef[((ps >> 4) & 7) * 2 + 1];
      const std::uint8_t byte = f[1 + n / 2];
      std::int32_t nib = (n & 1) ? (byte & 15) : (byte >> 4);
      if (nib >= 8) nib -= 16;
      std::int32_t s = ((nib * scale) * 2048 + 1024 + c1 * yn1 + c2 * yn2) >> 11;
      s = std::clamp(s, -32768, 32767);
      yn2 = yn1;
      yn1 = s;
      const std::size_t at = flag == THP_AUDIO_INTERLEAVE ? std::size_t{i} * 2 + ch : std::size_t{ch} * samples + i;
      out[at] = static_cast<std::int16_t>(s);
    }
  }
  return samples;
}

// ---- Container ----------------------------------------------------------------------------

Result parse(std::span<const std::uint8_t> file, Info& out) {
  out = Info{};
  if (file.size() < kHeaderSize) return Result::failure("THP: file shorter than its header");
  const std::uint8_t* p = file.data();
  if (std::memcmp(p, "THP\0", 4) != 0) return Result::failure("THP: bad magic");
  out.version = be32(p + 4);
  if (out.version != kVersion10 && out.version != kVersion11) return Result::failure("THP: unknown version");
  out.buf_size = be32(p + 8);
  out.audio_max_samples = be32(p + 12);
  out.frame_rate_bits = be32(p + 16);
  out.num_frames = be32(p + 20);
  out.first_frame_size = be32(p + 24);
  out.movie_data_size = be32(p + 28);
  const std::uint32_t comp_off = be32(p + 32);
  out.offset_data_offset = be32(p + 36);
  out.movie_data_offset = be32(p + 40);
  out.final_frame_offset = be32(p + 44);
  if (comp_off < kHeaderSize || std::uint64_t{comp_off} + 20 > file.size())
    return Result::failure("THP: component info out of range");
  if (std::uint64_t{out.movie_data_offset} + out.movie_data_size > file.size() ||
      std::uint64_t{out.movie_data_offset} + out.first_frame_size > file.size() ||
      out.final_frame_offset >= file.size() || out.final_frame_offset < out.movie_data_offset)
    return Result::failure("THP: movie data out of range");
  const std::uint8_t* c = p + comp_off;
  out.num_components = be32(c);
  if (out.num_components == 0 || out.num_components > 16) return Result::failure("THP: bad component count");
  std::memcpy(out.components.data(), c + 4, 16);
  std::uint64_t at = std::uint64_t{comp_off} + 20;
  for (std::uint32_t i = 0; i < out.num_components; ++i) {
    const std::uint8_t kind = out.components[i];
    const std::uint32_t words = kind == kCompVideo ? (out.version == kVersion11 ? 3 : 2)
                                : kind == kCompAudio ? (out.version == kVersion11 ? 4 : 3)
                                                     : 0;
    if (kind != kCompVideo && kind != kCompAudio) return Result::failure("THP: unknown component");
    if (at + words * 4 > file.size()) return Result::failure("THP: component info truncated");
    const std::uint8_t* w = p + at;
    if (kind == kCompVideo) {
      if (out.has_video) return Result::failure("THP: two video components");
      out.has_video = true;
      out.width = be32(w);
      out.height = be32(w + 4);
      out.video_type = words == 3 ? be32(w + 8) : 0;
    } else {
      if (out.has_audio) return Result::failure("THP: two audio components");
      out.has_audio = true;
      out.channels = be32(w);
      out.frequency = be32(w + 4);
      out.num_samples = be32(w + 8);
      out.num_tracks = words == 4 ? be32(w + 12) : 1;
      if (out.num_tracks == 0 || out.num_tracks > 8) return Result::failure("THP: bad audio track count");
    }
    at += words * 4;
  }
  return Result::success();
}

Result frameAt(std::span<const std::uint8_t> file, const Info& info, std::uint64_t offset, std::uint32_t size,
               Frame& out) {
  out = Frame{};
  const std::uint64_t header = 8 + std::uint64_t{info.num_components} * 4;
  if (offset + size > file.size() || size < header) return Result::failure("THP: frame out of range");
  const std::uint8_t* p = file.data() + offset;
  out.next_size = be32(p);
  out.prev_size = be32(p + 4);
  std::uint64_t data = offset + header;
  for (std::uint32_t i = 0; i < info.num_components; ++i) {
    const std::uint32_t n = info.components[i] == kCompAudio ? info.num_tracks : 1;
    for (std::uint32_t t = 0; t < n; ++t) {
      // One size per component; audio tracks share it (each track the same size).
      const std::uint32_t len = be32(p + 8 + 4 * i);
      if (data + len > offset + size) return Result::failure("THP: component data out of range");
      const std::span<const std::uint8_t> part = file.subspan(static_cast<std::size_t>(data), len);
      if (info.components[i] == kCompVideo) {
        out.video = part;
      } else {
        out.audio[out.audio_tracks++] = part;
      }
      data += len;
    }
  }
  return Result::success();
}

}  // namespace mpt::gc::thp

using namespace mpt::gc;
using namespace mpt::gc::detail;

namespace {

// The C API gets no length. Bound reads by the end of the arena when the data
// is there (it always is for game code), else by a fixed cap.
std::span<const std::uint8_t> boundedFrom(const void* p) {
  constexpr std::size_t kCap = 16u << 20;
  const auto& I = impl();
  if (I.inArena(p)) {
    const std::size_t left = I.arena_size - I.offsetOf(p);
    return {static_cast<const std::uint8_t*>(p), std::min(left, kCap)};
  }
  return {static_cast<const std::uint8_t*>(p), kCap};
}

}  // namespace

extern "C" {

BOOL THPInit(void) {
  boundary();
  st().thp_initialized = 1;
  return TRUE;
}

s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV, void* work) {
  (void)work;
  boundary();
  if (!st().thp_initialized) return THP_ERROR_NOT_INITIALIZED;
  if (!file || !tileY || !tileU || !tileV) return THP_ERROR_BAD_STREAM;
  return thp::decodeVideo(boundedFrom(file), static_cast<std::uint8_t*>(tileY), static_cast<std::uint8_t*>(tileU),
                          static_cast<std::uint8_t*>(tileV));
}

u32 THPAudioDecode(s16* audioBuffer, u8* audioFrame, s32 flag) {
  boundary();
  if (!audioBuffer || !audioFrame) return 0;
  return thp::decodeAudio(boundedFrom(audioFrame), audioBuffer, flag);
}

}  // extern "C"
