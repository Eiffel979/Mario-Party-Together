// The dump lane's PNG writer (tests/dump_lane/png.h): CRC-32 check values, a byte-exact golden
// built independently (Python's zlib at level 0 writes the same stored-block stream), and a
// round trip that decodes the file again, including a picture that needs two stored blocks.
// Synthetic pixels only.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "../dump_lane/png.h"
#include "../support/temp_dir.h"

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes readAll(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::uint32_t be32(const Bytes& b, std::size_t at) {
  return (std::uint32_t{b[at]} << 24) | (std::uint32_t{b[at + 1]} << 16) | (std::uint32_t{b[at + 2]} << 8) | b[at + 3];
}

std::uint32_t crc(const Bytes& b) { return mpt::dump_lane::png_detail::crc32(b); }

struct Decoded {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  Bytes rgba;            // the pixels' bytes, top row first
  int stored_blocks = 0;
  std::string error;     // empty when the file decoded and every check held
};

// A strict decoder for what write_png writes: signature, IHDR, one IDAT of stored deflate blocks,
// IEND, every chunk CRC, the zlib header and the Adler-32.
Decoded decode(const Bytes& f) {
  Decoded d;
  const auto fail = [&d](std::string why) {
    d.error = std::move(why);
    return d;
  };
  static const Bytes kSig{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (f.size() < 8 || !std::equal(kSig.begin(), kSig.end(), f.begin())) return fail("signature");
  std::size_t at = 8;
  Bytes z;
  std::vector<std::string> order;
  while (at + 12 <= f.size()) {
    const std::uint32_t len = be32(f, at);
    if (at + 12 + len > f.size()) return fail("chunk length");
    const std::string type(f.begin() + static_cast<std::ptrdiff_t>(at + 4), f.begin() + static_cast<std::ptrdiff_t>(at + 8));
    const Bytes typed(f.begin() + static_cast<std::ptrdiff_t>(at + 4), f.begin() + static_cast<std::ptrdiff_t>(at + 8 + len));
    if (crc(typed) != be32(f, at + 8 + len)) return fail(type + " CRC");
    const Bytes data(typed.begin() + 4, typed.end());
    if (type == "IHDR") {
      if (len != 13) return fail("IHDR length");
      d.width = be32(data, 0);
      d.height = be32(data, 4);
      if (data[8] != 8 || data[9] != 6 || data[10] != 0 || data[11] != 0 || data[12] != 0) return fail("IHDR format");
    } else if (type == "IDAT") {
      z.insert(z.end(), data.begin(), data.end());
    }
    order.push_back(type);
    at += 12 + len;
  }
  if (at != f.size() || order != std::vector<std::string>{"IHDR", "IDAT", "IEND"}) return fail("chunk order");
  if (z.size() < 6 || z[0] != 0x78 || ((z[0] << 8) | z[1]) % 31 != 0) return fail("zlib header");
  Bytes raw;
  std::size_t p = 2;
  for (bool last = false; !last;) {
    if (p + 5 > z.size()) return fail("stored block header");
    last = (z[p] & 1) != 0;
    if ((z[p] & 6) != 0) return fail("not a stored block");
    const std::uint32_t n = static_cast<std::uint32_t>(z[p + 1] | (z[p + 2] << 8));
    const std::uint32_t nn = static_cast<std::uint32_t>(z[p + 3] | (z[p + 4] << 8));
    if ((n ^ 0xFFFFu) != nn || p + 5 + n > z.size()) return fail("stored block length");
    raw.insert(raw.end(), z.begin() + static_cast<std::ptrdiff_t>(p + 5), z.begin() + static_cast<std::ptrdiff_t>(p + 5 + n));
    p += 5 + n;
    ++d.stored_blocks;
  }
  if (p + 4 != z.size()) return fail("zlib trailer");
  std::uint32_t a = 1, b = 0;
  for (std::uint8_t v : raw) {
    a = (a + v) % 65521u;
    b = (b + a) % 65521u;
  }
  if (((b << 16) | a) != be32(z, p)) return fail("Adler-32");
  const std::size_t row = std::size_t{d.width} * 4;
  if (raw.size() != (row + 1) * d.height) return fail("scanline size");
  for (std::uint32_t y = 0; y < d.height; ++y) {
    if (raw[y * (row + 1)] != 0) return fail("filter byte");
    const auto start = raw.begin() + static_cast<std::ptrdiff_t>(y * (row + 1) + 1);
    d.rgba.insert(d.rgba.end(), start, start + static_cast<std::ptrdiff_t>(row));
  }
  return d;
}

Bytes bytesOf(const std::vector<std::uint32_t>& pixels) {
  Bytes out;
  for (std::uint32_t px : pixels)
    for (int s = 0; s < 32; s += 8) out.push_back(static_cast<std::uint8_t>(px >> s));  // R, G, B, A
  return out;
}

}  // namespace

TEST_CASE("png: CRC-32 check values") {
  const std::string check = "123456789";
  CHECK(crc(Bytes(check.begin(), check.end())) == 0xCBF43926u);  // the standard CRC-32 check value
  CHECK(crc(Bytes{'I', 'E', 'N', 'D'}) == 0xAE426082u);           // every PNG's IEND CRC
  CHECK(crc(Bytes{}) == 0u);
}

TEST_CASE("png: a 2x2 picture matches its golden byte for byte and decodes back") {
  const mpt::test::ScopedTempDir tmp("mpt_png");
  // In memory each pixel is R, G, B, A (little-endian words): red, green, blue, and a translucent one.
  const std::vector<std::uint32_t> pixels{0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0x80402010u};
  const auto path = tmp.path() / "frame.png";
  REQUIRE(mpt::dump_lane::write_png(path, 2, 2, pixels));
  const Bytes file = readAll(path);
  // Golden from an independent writer: Python's struct, zlib.compress(raw, 0) and zlib.crc32.
  CHECK(file.size() == 86);
  CHECK(crc(file) == 0x0F9007BFu);
  const Decoded d = decode(file);
  REQUIRE_MESSAGE(d.error.empty(), d.error);
  CHECK(d.width == 2);
  CHECK(d.height == 2);
  CHECK(d.stored_blocks == 1);
  CHECK(d.rgba == bytesOf(pixels));
}

TEST_CASE("png: a picture larger than one stored block round-trips") {
  const mpt::test::ScopedTempDir tmp("mpt_png");
  const std::uint32_t w = 200, h = 100;  // (200 * 4 + 1) * 100 = 80,100 raw bytes: two stored blocks
  std::vector<std::uint32_t> pixels(std::size_t{w} * h);
  for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<std::uint32_t>(i * 2654435761u);
  const auto path = tmp.path() / "big.png";
  REQUIRE(mpt::dump_lane::write_png(path, w, h, pixels));
  const Decoded d = decode(readAll(path));
  REQUIRE_MESSAGE(d.error.empty(), d.error);
  CHECK(d.width == w);
  CHECK(d.height == h);
  CHECK(d.stored_blocks == 2);
  CHECK(d.rgba == bytesOf(pixels));
}

TEST_CASE("png: bad arguments write nothing") {
  const mpt::test::ScopedTempDir tmp("mpt_png");
  const std::vector<std::uint32_t> three(3);
  CHECK_FALSE(mpt::dump_lane::write_png(tmp.path() / "a.png", 0, 1, three));
  CHECK_FALSE(mpt::dump_lane::write_png(tmp.path() / "b.png", 2, 2, three));  // too few pixels
  CHECK_FALSE(mpt::dump_lane::write_png(tmp.path() / "missing" / "c.png", 1, 1, three));  // no folder
  CHECK_FALSE(std::filesystem::exists(tmp.path() / "a.png"));
  CHECK_FALSE(std::filesystem::exists(tmp.path() / "b.png"));
}
