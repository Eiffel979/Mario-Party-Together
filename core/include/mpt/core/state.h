#pragma once

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

  std::uint64_t digest() const { return hash_; }

 private:
  std::uint64_t hash_ = kOffsetBasis;
};

}  // namespace mpt
