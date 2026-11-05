// A minimal PNG writer for the dump lane's frame captures (M2): 8-bit RGBA, no filter, stored
// (uncompressed) deflate blocks. No dependency; files are large but exact.
//
// Captures are the user's own game output. The lane writes them only to the directory given on its
// command line (a scratch folder); they must never be committed (CLAUDE.md rule 1).
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace mpt::dump_lane {

namespace png_detail {

inline std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc = 0) {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t n = 0; n < 256; ++n) {
      std::uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[n] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (std::uint8_t b : data) crc = table[(crc ^ b) & 0xFFu] ^ (crc >> 8);
  return ~crc;
}

inline void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::uint8_t>(v >> s));
}

inline void chunk(std::vector<std::uint8_t>& out, const char type[4], const std::vector<std::uint8_t>& data) {
  put32(out, static_cast<std::uint32_t>(data.size()));
  const std::size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  put32(out, crc32({out.data() + start, out.size() - start}));
}

}  // namespace png_detail

// Writes `pixels` (width * height, each pixel's bytes R, G, B, A in memory, top row first).
inline bool write_png(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
                      std::span<const std::uint32_t> pixels) {
  using namespace png_detail;
  if (width == 0 || height == 0 || pixels.size() < std::size_t{width} * height) return false;
  // Raw scanlines: filter byte 0, then the row.
  const std::size_t row = std::size_t{width} * 4;
  std::vector<std::uint8_t> raw;
  raw.reserve((row + 1) * height);
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(pixels.data());
  for (std::uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), bytes + y * row, bytes + (y + 1) * row);
  }
  // zlib stream of stored blocks (at most 65535 bytes each) and the Adler-32.
  std::vector<std::uint8_t> z{0x78, 0x01};
  std::uint32_t a = 1, b = 0;
  for (std::uint8_t v : raw) {
    a = (a + v) % 65521u;
    b = (b + a) % 65521u;
  }
  for (std::size_t at = 0; at < raw.size() || at == 0;) {
    const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
    const bool last = at + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(static_cast<std::uint8_t>(n));
    z.push_back(static_cast<std::uint8_t>(n >> 8));
    z.push_back(static_cast<std::uint8_t>(~n));
    z.push_back(static_cast<std::uint8_t>(~n >> 8));
    z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
    at += n;
    if (last) break;
  }
  put32(z, (b << 16) | a);

  std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<std::uint8_t> ihdr;
  put32(ihdr, width);
  put32(ihdr, height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit, RGBA, deflate, no filter method, no interlace
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});

  std::FILE* f = nullptr;
#if defined(_WIN32)
  if (_wfopen_s(&f, path.wstring().c_str(), L"wb") != 0) f = nullptr;
#else
  f = std::fopen(path.string().c_str(), "wb");
#endif
  if (f == nullptr) return false;
  const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
  return std::fclose(f) == 0 && ok;
}

}  // namespace mpt::dump_lane
