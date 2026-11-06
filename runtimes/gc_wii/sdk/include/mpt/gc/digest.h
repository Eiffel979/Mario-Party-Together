#pragma once

// A fast 64-bit digest of raw game memory for the state hash manifest (ADR 0008 §8, P1-16).
//
// mpt::StateHasher (FNV-1a) works a byte at a time, which is fine for manifest fields but far too
// slow for the megabytes of main RAM and ARAM a GameCube title keeps. Runtimes digest those ranges
// with this function and feed the result (and the range's size) into their StateHasher.
//
// Four independent lanes over 64-bit little-endian words, each lane a multiply-rotate round, then
// a merge and an avalanche over the length; the structure follows XXH64, but the output is our
// own and needs no compatibility with it. It depends only on the bytes and their order, never on
// the address, so the same contents give the same digest wherever the arena is mapped. Like
// StateHasher it detects divergence and is not a security hash.
//
// Floats in memory are digested as their raw bits: memory is bytes, and nothing here knows which
// bytes are floats. See docs/titles/mp4.md §20 for how that squares with ADR 0008's NaN rule.
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace mpt::gc {

static_assert(std::endian::native == std::endian::little, "memoryDigest reads little-endian words");

namespace digest_detail {
inline constexpr std::uint64_t kP1 = 0x9E3779B185EBCA87ull;
inline constexpr std::uint64_t kP2 = 0xC2B2AE3D27D4EB4Full;
inline constexpr std::uint64_t kP3 = 0x165667B19E3779F9ull;
inline constexpr std::uint64_t kP4 = 0x85EBCA77C2B2AE63ull;
inline constexpr std::uint64_t kP5 = 0x27D4EB2F165667C5ull;

inline std::uint64_t load64(const std::byte* p) {
  std::uint64_t v;
  std::memcpy(&v, p, sizeof v);
  return v;
}
inline std::uint64_t round(std::uint64_t acc, std::uint64_t word) {
  acc += word * kP2;
  acc = std::rotl(acc, 31);
  return acc * kP1;
}
inline std::uint64_t merge(std::uint64_t acc, std::uint64_t lane) {
  acc ^= round(0, lane);
  return acc * kP1 + kP4;
}
}  // namespace digest_detail

inline std::uint64_t memoryDigest(std::span<const std::byte> bytes) {
  using namespace digest_detail;
  const std::byte* p = bytes.data();
  std::size_t left = bytes.size();
  std::uint64_t h;
  if (left >= 32) {
    std::uint64_t v1 = kP1 + kP2, v2 = kP2, v3 = 0, v4 = 0 - kP1;
    do {
      v1 = round(v1, load64(p));
      v2 = round(v2, load64(p + 8));
      v3 = round(v3, load64(p + 16));
      v4 = round(v4, load64(p + 24));
      p += 32;
      left -= 32;
    } while (left >= 32);
    h = std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
    h = merge(h, v1);
    h = merge(h, v2);
    h = merge(h, v3);
    h = merge(h, v4);
  } else {
    h = kP5;
  }
  h += static_cast<std::uint64_t>(bytes.size());
  while (left >= 8) {
    h ^= round(0, load64(p));
    h = std::rotl(h, 27) * kP1 + kP4;
    p += 8;
    left -= 8;
  }
  while (left > 0) {
    h ^= static_cast<std::uint64_t>(*p) * kP5;
    h = std::rotl(h, 11) * kP1;
    ++p;
    --left;
  }
  h ^= h >> 33;
  h *= kP2;
  h ^= h >> 29;
  h *= kP3;
  h ^= h >> 32;
  return h;
}

}  // namespace mpt::gc
