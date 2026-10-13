// A small THP writer for tests: baseline JPEG (4:2:0) and DSP-ADPCM, integer
// only, so the generated streams are identical on every platform. It writes
// synthetic pictures and tones generated here; no movie from any game is used.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "mpt/gc/thp.h"
#include "mpt_gc/thp.h"

namespace mpt::gc::test::thpenc {

struct Planes {
  std::uint32_t w = 0, h = 0;
  std::vector<std::uint8_t> y, u, v;  // row-major; u and v are w/2 x h/2
};

struct Bits {
  std::vector<std::uint8_t>& out;
  std::uint32_t acc = 0;
  int n = 0;
  void put(std::uint32_t code, int len) {
    for (int i = len - 1; i >= 0; --i) {
      acc = (acc << 1) | ((code >> i) & 1);
      if (++n == 8) {
        out.push_back(static_cast<std::uint8_t>(acc));
        if (acc == 0xFF) out.push_back(0);  // byte stuffing
        acc = 0;
        n = 0;
      }
    }
  }
  void flush() {
    while (n) put(1, 1);  // pad with 1 bits
  }
};

// Huffman tables of our own design (valid canonical JPEG tables, no all-ones code).
inline constexpr std::uint8_t kDcCounts[16] = {0, 1, 3, 4, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
inline constexpr std::uint8_t kAcCounts[16] = {0, 1, 0, 4, 0, 8, 0, 40, 0, 109, 0, 0, 0, 0, 0, 0};

inline std::vector<std::uint8_t> dcSymbols() {
  std::vector<std::uint8_t> s;
  for (int i = 0; i < 12; ++i) s.push_back(static_cast<std::uint8_t>(i));
  return s;
}
inline std::vector<std::uint8_t> acSymbols() {
  std::vector<std::uint8_t> s{0x00};
  for (int size = 1; size <= 10; ++size)
    for (int run = 0; run < 16; ++run) s.push_back(static_cast<std::uint8_t>((run << 4) | size));
  s.push_back(0xF0);
  return s;
}

struct Codes {
  std::array<std::uint32_t, 256> code{};
  std::array<int, 256> len{};
};
inline Codes canonical(const std::uint8_t counts[16], const std::vector<std::uint8_t>& symbols) {
  Codes c;
  std::uint32_t code = 0;
  std::size_t k = 0;
  for (int len = 1; len <= 16; ++len) {
    for (int i = 0; i < counts[len - 1]; ++i) {
      c.code[symbols[k]] = code++;
      c.len[symbols[k]] = len;
      ++k;
    }
    code <<= 1;
  }
  return c;
}

inline int category(std::int32_t v) {
  std::uint32_t a = static_cast<std::uint32_t>(v < 0 ? -v : v);
  int n = 0;
  while (a) {
    ++n;
    a >>= 1;
  }
  return n;
}

inline void putValue(Bits& b, const Codes& table, int symbol, std::int32_t v, int size) {
  b.put(table.code[symbol], table.len[symbol]);
  if (size) b.put(static_cast<std::uint32_t>(v >= 0 ? v : v + (1 << size) - 1), size);
}

// Forward DCT with the decoder's integer basis, then quantization (zigzag-ordered q).
inline void fdctQuant(const std::uint8_t* src, std::size_t stride, const std::uint8_t q[64], std::int32_t out[64]) {
  std::int64_t tmp[8][8];  // tmp[y][u]
  for (int y = 0; y < 8; ++y)
    for (int u = 0; u < 8; ++u) {
      std::int64_t s = 0;
      for (int x = 0; x < 8; ++x) s += (std::int64_t{src[y * stride + x]} - 128) * thp::kDctBasis[u][x];
      tmp[y][u] = s;
    }
  for (int k = 0; k < 64; ++k) {
    const int n = thp::kZigzag[k], v = n / 8, u = n % 8;
    std::int64_t s = 0;
    for (int y = 0; y < 8; ++y) s += tmp[y][u] * thp::kDctBasis[v][y];
    const std::int64_t f = (s + (std::int64_t{1} << 25)) >> 26;
    const std::int64_t qq = q[k];
    out[k] = static_cast<std::int32_t>(f >= 0 ? (f + qq / 2) / qq : -((-f + qq / 2) / qq));
  }
}

inline void encodeBlock(Bits& b, const std::int32_t zz[64], std::int32_t& pred, const Codes& dc, const Codes& ac) {
  const std::int32_t diff = zz[0] - pred;
  pred = zz[0];
  putValue(b, dc, category(diff), diff, category(diff));
  int run = 0;
  for (int k = 1; k < 64; ++k) {
    if (zz[k] == 0) {
      ++run;
      continue;
    }
    while (run > 15) {
      putValue(b, ac, 0xF0, 0, 0);
      run -= 16;
    }
    const int size = category(zz[k]);
    putValue(b, ac, (run << 4) | size, zz[k], size);
    run = 0;
  }
  if (run) putValue(b, ac, 0x00, 0, 0);
}

inline void seg(std::vector<std::uint8_t>& o, std::uint8_t marker, const std::vector<std::uint8_t>& body) {
  o.push_back(0xFF);
  o.push_back(marker);
  const std::size_t len = body.size() + 2;
  o.push_back(static_cast<std::uint8_t>(len >> 8));
  o.push_back(static_cast<std::uint8_t>(len));
  o.insert(o.end(), body.begin(), body.end());
}

// Quantization tables (zigzag order). AC steps >= 2 keep AC categories <= 10.
inline std::array<std::uint8_t, 64> quantY() {
  std::array<std::uint8_t, 64> q{};
  for (int k = 0; k < 64; ++k) q[k] = static_cast<std::uint8_t>(2 + k / 3);
  return q;
}
inline std::array<std::uint8_t, 64> quantC() {
  std::array<std::uint8_t, 64> q{};
  for (int k = 0; k < 64; ++k) q[k] = static_cast<std::uint8_t>(3 + k / 2);
  return q;
}

struct JpegOptions {
  std::uint16_t restart = 0;  // MCUs per restart interval, 0 = none
  std::uint8_t y_sampling = 0x22;
  bool app_segment = false;
};

inline std::vector<std::uint8_t> encodeJpeg(const Planes& p, const JpegOptions& opt = {}) {
  const auto qy = quantY(), qc = quantC();
  std::vector<std::uint8_t> o{0xFF, 0xD8};
  if (opt.app_segment) seg(o, 0xE0, {'M', 'P', 'T', 0, 1});
  std::vector<std::uint8_t> dqt{0x00};
  dqt.insert(dqt.end(), qy.begin(), qy.end());
  dqt.push_back(0x01);
  dqt.insert(dqt.end(), qc.begin(), qc.end());
  seg(o, 0xDB, dqt);
  seg(o, 0xC0,
      {8, static_cast<std::uint8_t>(p.h >> 8), static_cast<std::uint8_t>(p.h), static_cast<std::uint8_t>(p.w >> 8),
       static_cast<std::uint8_t>(p.w), 3, 1, opt.y_sampling, 0, 2, 0x11, 1, 3, 0x11, 1});
  const auto dcs = dcSymbols(), acs = acSymbols();
  std::vector<std::uint8_t> dht{0x00};
  dht.insert(dht.end(), kDcCounts, kDcCounts + 16);
  dht.insert(dht.end(), dcs.begin(), dcs.end());
  dht.push_back(0x10);
  dht.insert(dht.end(), kAcCounts, kAcCounts + 16);
  dht.insert(dht.end(), acs.begin(), acs.end());
  seg(o, 0xC4, dht);
  if (opt.restart)
    seg(o, 0xDD, {static_cast<std::uint8_t>(opt.restart >> 8), static_cast<std::uint8_t>(opt.restart)});
  seg(o, 0xDA, {3, 1, 0x00, 2, 0x00, 3, 0x00, 0, 63, 0});
  const Codes dc = canonical(kDcCounts, dcs), ac = canonical(kAcCounts, acs);
  Bits b{o};
  std::int32_t pred[3] = {0, 0, 0};
  std::int32_t zz[64];
  const std::uint32_t mcus = (p.w / 16) * (p.h / 16);
  std::uint32_t mcu = 0;
  std::uint8_t rst = 0;
  for (std::uint32_t my = 0; my < p.h / 16; ++my) {
    for (std::uint32_t mx = 0; mx < p.w / 16; ++mx, ++mcu) {
      if (opt.restart && mcu && mcu % opt.restart == 0) {
        b.flush();
        o.push_back(0xFF);
        o.push_back(static_cast<std::uint8_t>(0xD0 + rst));
        rst = (rst + 1) & 7;
        pred[0] = pred[1] = pred[2] = 0;
      }
      for (int k = 0; k < 4; ++k) {
        const std::size_t x = mx * 16 + (k & 1) * 8, y = my * 16 + (k >> 1) * 8;
        fdctQuant(&p.y[y * p.w + x], p.w, qy.data(), zz);
        encodeBlock(b, zz, pred[0], dc, ac);
      }
      fdctQuant(&p.u[(my * 8) * (p.w / 2) + mx * 8], p.w / 2, qc.data(), zz);
      encodeBlock(b, zz, pred[1], dc, ac);
      fdctQuant(&p.v[(my * 8) * (p.w / 2) + mx * 8], p.w / 2, qc.data(), zz);
      encodeBlock(b, zz, pred[2], dc, ac);
    }
  }
  (void)mcus;
  b.flush();
  o.push_back(0xFF);
  o.push_back(0xD9);
  return o;
}

// ---- DSP-ADPCM -------------------------------------------------------------------------------

inline constexpr std::int16_t kCoefs[16] = {0, 0, 2048, 0, 4096, -2048, 3072, -1024,
                                            1024, 0, 3584, -1792, 2560, -512, 1536, 512};

struct AdpcmChannel {
  std::vector<std::uint8_t> bytes;
  std::vector<std::int16_t> decoded;  // what an exact decoder must produce
  std::int16_t yn1_start = 0, yn2_start = 0;
};

inline std::int32_t adpcmStep(std::int32_t nib, std::int32_t scale, std::int32_t c1, std::int32_t c2,
                              std::int32_t yn1, std::int32_t yn2) {
  return std::clamp(((nib * scale) * 2048 + 1024 + c1 * yn1 + c2 * yn2) >> 11, -32768, 32767);
}

inline AdpcmChannel encodeAdpcm(const std::vector<std::int16_t>& pcm, std::int16_t yn1, std::int16_t yn2) {
  AdpcmChannel ch;
  ch.yn1_start = yn1;
  ch.yn2_start = yn2;
  std::int32_t h1 = yn1, h2 = yn2;
  for (std::size_t base = 0; base < pcm.size(); base += 14) {
    std::int64_t best_err = -1;
    std::uint8_t best_frame[8]{};
    std::int16_t best_out[14]{};
    std::int32_t best_h1 = 0, best_h2 = 0;
    for (int idx = 0; idx < 8; ++idx) {
      for (int sc = 0; sc < 12; ++sc) {
        const std::int32_t scale = 1 << sc, c1 = kCoefs[idx * 2], c2 = kCoefs[idx * 2 + 1];
        std::uint8_t frame[8]{static_cast<std::uint8_t>((idx << 4) | sc)};
        std::int16_t out[14]{};
        std::int32_t a = h1, b = h2;
        std::int64_t err = 0;
        for (int i = 0; i < 14; ++i) {
          const std::int32_t target = base + i < pcm.size() ? pcm[base + i] : 0;
          const std::int64_t pred = 1024 + std::int64_t{c1} * a + std::int64_t{c2} * b;
          const std::int64_t want = std::int64_t{target} * 2048 - pred;
          const std::int64_t div = std::int64_t{scale} * 2048;
          std::int64_t n = want >= 0 ? (want + div / 2) / div : -((-want + div / 2) / div);
          n = std::clamp<std::int64_t>(n, -8, 7);
          const std::int32_t s = adpcmStep(static_cast<std::int32_t>(n), scale, c1, c2, a, b);
          if (base + i < pcm.size()) err += std::int64_t{s - target} * (s - target);
          out[i] = static_cast<std::int16_t>(s);
          frame[1 + i / 2] |= static_cast<std::uint8_t>((n & 15) << ((i & 1) ? 0 : 4));
          b = a;
          a = s;
        }
        if (best_err < 0 || err < best_err) {
          best_err = err;
          std::memcpy(best_frame, frame, 8);
          std::memcpy(best_out, out, sizeof out);
          best_h1 = a;
          best_h2 = b;
        }
      }
    }
    ch.bytes.insert(ch.bytes.end(), best_frame, best_frame + 8);
    for (int i = 0; i < 14 && base + i < pcm.size(); ++i) ch.decoded.push_back(best_out[i]);
    h1 = best_h1;
    h2 = best_h2;
  }
  return ch;
}

inline void be32(std::vector<std::uint8_t>& o, std::uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) o.push_back(static_cast<std::uint8_t>(v >> s));
}
inline void be16(std::vector<std::uint8_t>& o, std::uint16_t v) {
  o.push_back(static_cast<std::uint8_t>(v >> 8));
  o.push_back(static_cast<std::uint8_t>(v));
}

// One THP audio frame: header, then left and right ADPCM.
inline std::vector<std::uint8_t> audioFrame(const AdpcmChannel& l, const AdpcmChannel& r, std::uint32_t samples) {
  std::vector<std::uint8_t> o;
  const std::uint32_t channel_size = static_cast<std::uint32_t>(std::max(l.bytes.size(), r.bytes.size()));
  be32(o, channel_size);
  be32(o, samples);
  for (int ch = 0; ch < 2; ++ch)
    for (std::int16_t c : kCoefs) be16(o, static_cast<std::uint16_t>(c));
  be16(o, static_cast<std::uint16_t>(l.yn1_start));
  be16(o, static_cast<std::uint16_t>(l.yn2_start));
  be16(o, static_cast<std::uint16_t>(r.yn1_start));
  be16(o, static_cast<std::uint16_t>(r.yn2_start));
  for (const AdpcmChannel* c : {&l, &r}) {
    o.insert(o.end(), c->bytes.begin(), c->bytes.end());
    o.resize(o.size() + (channel_size - c->bytes.size()), 0);
  }
  return o;
}

// ---- Synthetic content -------------------------------------------------------------------------

inline Planes picture(std::uint32_t w, std::uint32_t h, std::uint32_t f) {
  Planes p{w, h, std::vector<std::uint8_t>(w * h), std::vector<std::uint8_t>(w * h / 4),
           std::vector<std::uint8_t>(w * h / 4)};
  for (std::uint32_t y = 0; y < h; ++y)
    for (std::uint32_t x = 0; x < w; ++x) {
      std::uint32_t v = (x * 3 + y * 2 + f * 5) & 255;
      const std::uint32_t sx = (f * 4) % w, sy = (f * 3) % h;
      if (x >= sx && x < sx + 12 && y >= sy && y < sy + 10) v = 235;  // a moving square
      p.y[y * w + x] = static_cast<std::uint8_t>(v);
    }
  for (std::uint32_t y = 0; y < h / 2; ++y)
    for (std::uint32_t x = 0; x < w / 2; ++x) {
      p.u[y * (w / 2) + x] = static_cast<std::uint8_t>(96 + ((x + 64 - f) & 63));
      p.v[y * (w / 2) + x] = static_cast<std::uint8_t>(96 + ((y + f * 2) & 63));
    }
  return p;
}

// Integer triangle waves: no host math.
inline std::vector<std::int16_t> tone(std::uint32_t start, std::uint32_t n, std::uint32_t period, std::int32_t amp) {
  std::vector<std::int16_t> s(n);
  for (std::uint32_t i = 0; i < n; ++i) {
    const std::int32_t ph = static_cast<std::int32_t>((start + i) % period);
    const std::int32_t half = static_cast<std::int32_t>(period / 2);
    const std::int32_t tri = ph < half ? ph * 2 - half : (static_cast<std::int32_t>(period) - ph) * 2 - half;
    s[i] = static_cast<std::int16_t>(tri * amp / half);
  }
  return s;
}

struct Movie {
  std::vector<std::uint8_t> file;
  std::vector<Planes> pictures;
  std::vector<std::vector<std::uint8_t>> jpegs;
  std::vector<std::int16_t> left, right;  // exact decoded audio, all frames
  std::uint32_t samples_per_frame = 0;
};

inline Movie movie(std::uint32_t w, std::uint32_t h, std::uint32_t frames, std::uint32_t samples_per_frame) {
  Movie m;
  m.samples_per_frame = samples_per_frame;
  std::vector<std::vector<std::uint8_t>> bodies;
  std::int16_t l1 = 0, l2 = 0, r1 = 0, r2 = 0;
  std::uint32_t max_body = 0;
  for (std::uint32_t f = 0; f < frames; ++f) {
    m.pictures.push_back(picture(w, h, f));
    m.jpegs.push_back(encodeJpeg(m.pictures.back(), {static_cast<std::uint16_t>(f % 2 ? 3 : 0)}));
    const auto lp = tone(f * samples_per_frame, samples_per_frame, 73, 9000);
    const auto rp = tone(f * samples_per_frame, samples_per_frame, 41, 5000);
    const AdpcmChannel l = encodeAdpcm(lp, l1, l2), r = encodeAdpcm(rp, r1, r2);
    l1 = l.decoded.back();
    l2 = l.decoded[l.decoded.size() - 2];
    r1 = r.decoded.back();
    r2 = r.decoded[r.decoded.size() - 2];
    m.left.insert(m.left.end(), l.decoded.begin(), l.decoded.end());
    m.right.insert(m.right.end(), r.decoded.begin(), r.decoded.end());
    const auto audio = audioFrame(l, r, samples_per_frame);
    std::vector<std::uint8_t> body;
    be32(body, 0);  // next size, patched below
    be32(body, 0);  // previous size
    be32(body, static_cast<std::uint32_t>(m.jpegs.back().size()));
    be32(body, static_cast<std::uint32_t>(audio.size()));
    body.insert(body.end(), m.jpegs.back().begin(), m.jpegs.back().end());
    body.insert(body.end(), audio.begin(), audio.end());
    body.resize((body.size() + 31) & ~std::size_t{31}, 0);
    max_body = std::max(max_body, static_cast<std::uint32_t>(body.size()));
    bodies.push_back(std::move(body));
  }
  auto patch = [](std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
  };
  for (std::uint32_t f = 0; f < frames; ++f) {
    patch(bodies[f], 0, static_cast<std::uint32_t>(bodies[(f + 1) % frames].size()));
    patch(bodies[f], 4, static_cast<std::uint32_t>(bodies[(f + frames - 1) % frames].size()));
  }
  const std::uint32_t movie_off = 96;
  std::uint32_t total = 0, final_off = movie_off;
  for (std::uint32_t f = 0; f < frames; ++f) {
    if (f + 1 == frames) final_off = movie_off + total;
    total += static_cast<std::uint32_t>(bodies[f].size());
  }
  auto& o = m.file;
  o = {'T', 'H', 'P', 0};
  be32(o, thp::kVersion11);
  be32(o, max_body);
  be32(o, samples_per_frame);
  be32(o, std::bit_cast<std::uint32_t>(29.97f));
  be32(o, frames);
  be32(o, static_cast<std::uint32_t>(bodies[0].size()));
  be32(o, total);
  be32(o, 48);  // component info
  be32(o, 0);   // no offset table
  be32(o, movie_off);
  be32(o, final_off);
  be32(o, 2);
  o.push_back(thp::kCompVideo);
  o.push_back(thp::kCompAudio);
  for (int i = 2; i < 16; ++i) o.push_back(thp::kCompNone);
  be32(o, w);
  be32(o, h);
  be32(o, 0);  // video type
  be32(o, 2);
  be32(o, 32000);
  be32(o, samples_per_frame * frames);
  be32(o, 1);
  o.resize(movie_off, 0);
  for (const auto& b : bodies) o.insert(o.end(), b.begin(), b.end());
  return m;
}

// I8 tiles to row-major.
inline std::vector<std::uint8_t> untile(const std::uint8_t* t, std::uint32_t w, std::uint32_t h) {
  std::vector<std::uint8_t> out(w * h);
  for (std::uint32_t y = 0; y < h; ++y)
    for (std::uint32_t x = 0; x < w; ++x) out[y * w + x] = t[thp::i8Offset(w, x, y)];
  return out;
}

}  // namespace mpt::gc::test::thpenc
