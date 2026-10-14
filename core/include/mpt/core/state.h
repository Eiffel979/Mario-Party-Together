#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "mpt/core/title_id.h"

namespace mpt {

// Opaque runtime snapshot. Only the runtime that wrote it can interpret `data`.
struct SaveState {
  TitleId title{};
  std::uint32_t format_version = 0;  // bumped by the runtime when its layout changes
  std::uint64_t frame = 0;           // frame number the state was taken at
  std::vector<std::byte> data{};
};

// 64-bit FNV-1a. Used for state hashes: cheap, platform independent, and good
// enough to detect divergence (it is not a security hash).
class StateHasher {
 public:
  static constexpr std::uint64_t kOffsetBasis = 0xcbf29ce484222325ull;
  static constexpr std::uint64_t kPrime = 0x100000001b3ull;

  void update(std::span<const std::byte> bytes) {
    for (std::byte b : bytes) {
      hash_ ^= static_cast<std::uint64_t>(b);
      hash_ *= kPrime;
    }
  }

  // Hashes the little-endian bytes of an integer so results match across platforms.
  void update_u64(std::uint64_t value) {
    std::byte bytes[8];
    for (int i = 0; i < 8; ++i) bytes[i] = static_cast<std::byte>((value >> (8 * i)) & 0xff);
    update(bytes);
  }

  // Floats are hashed as their IEEE 754 bits, little-endian (4 bytes for f32, 8 for
  // f64), with every NaN replaced by the canonical quiet NaN (0x7fc00000 /
  // 0x7ff8000000000000) first. IEEE 754 leaves a NaN's sign and payload unspecified, and
  // compilers may swap commutative operands, so those bits can differ between builds
  // and platforms without any difference in behaviour (ADR 0008, amendment after
  // P1-14). Everything else, including the sign of zero, infinities and subnormals, is
  // hashed exactly. Runtimes hash every float field through these, never as raw bytes.
  static constexpr std::uint32_t kCanonicalNanF32 = 0x7fc00000u;
  static constexpr std::uint64_t kCanonicalNanF64 = 0x7ff8000000000000ull;

  void update_f32(float value) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0) bits = kCanonicalNanF32;
    std::byte bytes[4];
    for (int i = 0; i < 4; ++i) bytes[i] = static_cast<std::byte>((bits >> (8 * i)) & 0xff);
    update(bytes);
  }

  void update_f64(double value) {
    std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    if ((bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull && (bits & 0x000fffffffffffffull) != 0) {
      bits = kCanonicalNanF64;
    }
    update_u64(bits);
  }

  std::uint64_t digest() const { return hash_; }

 private:
  std::uint64_t hash_ = kOffsetBasis;
};

}  // namespace mpt
