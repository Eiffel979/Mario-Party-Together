// StateHasher float hashing (ADR 0008, amendment after P1-14): NaNs are canonical,
// everything else is exact, and the integer and byte paths are unchanged.

#include <doctest/doctest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include "mpt/core/state.h"

using namespace mpt;

namespace {

std::uint64_t hash_f32(float v) {
  StateHasher h;
  h.update_f32(v);
  return h.digest();
}

std::uint64_t hash_f64(double v) {
  StateHasher h;
  h.update_f64(v);
  return h.digest();
}

std::uint64_t hash_bytes_le32(std::uint32_t bits) {
  StateHasher h;
  std::byte bytes[4];
  for (int i = 0; i < 4; ++i) bytes[i] = static_cast<std::byte>((bits >> (8 * i)) & 0xff);
  h.update(bytes);
  return h.digest();
}

}  // namespace

TEST_CASE("every f32 NaN hashes like the canonical quiet NaN") {
  const std::uint64_t canonical = hash_f32(std::bit_cast<float>(StateHasher::kCanonicalNanF32));
  for (std::uint32_t bits : {0x7fc00000u, 0xffc00000u, 0x7f800001u, 0xff800001u, 0x7fffffffu, 0xffffffffu,
                             0x7fa00000u, 0xffc12345u}) {
    CAPTURE(bits);
    CHECK(hash_f32(std::bit_cast<float>(bits)) == canonical);
  }
  CHECK(canonical == hash_bytes_le32(StateHasher::kCanonicalNanF32));
}

TEST_CASE("every f64 NaN hashes like the canonical quiet NaN") {
  const std::uint64_t canonical = hash_f64(std::bit_cast<double>(StateHasher::kCanonicalNanF64));
  for (std::uint64_t bits : {0x7ff8000000000000ull, 0xfff8000000000000ull, 0x7ff0000000000001ull,
                             0xfff0000000000001ull, 0x7fffffffffffffffull, 0xfff8deadbeef0000ull}) {
    CAPTURE(bits);
    CHECK(hash_f64(std::bit_cast<double>(bits)) == canonical);
  }
  StateHasher h;
  h.update_u64(StateHasher::kCanonicalNanF64);
  CHECK(canonical == h.digest());
}

TEST_CASE("non-NaN floats hash their exact bits") {
  // Signed zeros, infinities, subnormals and neighbours all stay distinct.
  CHECK(hash_f32(0.0f) != hash_f32(-0.0f));
  CHECK(hash_f32(std::numeric_limits<float>::infinity()) != hash_f32(-std::numeric_limits<float>::infinity()));
  CHECK(hash_f32(std::numeric_limits<float>::infinity()) != hash_f32(std::numeric_limits<float>::quiet_NaN()));
  CHECK(hash_f32(std::numeric_limits<float>::denorm_min()) != hash_f32(0.0f));
  CHECK(hash_f32(1.0f) != hash_f32(std::nextafter(1.0f, 2.0f)));
  CHECK(hash_f32(1.5f) == hash_bytes_le32(std::bit_cast<std::uint32_t>(1.5f)));
  CHECK(hash_f64(0.0) != hash_f64(-0.0));
  CHECK(hash_f64(std::numeric_limits<double>::infinity()) != hash_f64(std::numeric_limits<double>::quiet_NaN()));
  StateHasher h;
  h.update_u64(std::bit_cast<std::uint64_t>(2.25));
  CHECK(hash_f64(2.25) == h.digest());
}

TEST_CASE("FNV-1a output is unchanged (contract v1 golden)") {
  StateHasher h;
  CHECK(h.digest() == StateHasher::kOffsetBasis);
  h.update_u64(0x0123456789abcdefull);
  // FNV-1a over the bytes ef cd ab 89 67 45 23 01.
  std::uint64_t expected = StateHasher::kOffsetBasis;
  for (std::uint64_t v = 0x0123456789abcdefull, i = 0; i < 8; ++i, v >>= 8) {
    expected ^= v & 0xff;
    expected *= StateHasher::kPrime;
  }
  CHECK(h.digest() == expected);
}
