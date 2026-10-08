#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace mpt::asset_import {

// Streaming SHA-256 (FIPS 180-4). Kept in-tree to avoid a crypto dependency for
// a single digest; it is only used to identify dumps.
class Sha256 {
 public:
  Sha256();
  void update(std::span<const std::byte> data);
  std::array<std::uint8_t, 32> finish();  // the hasher must not be reused afterwards
  std::string finish_hex();               // lowercase hex

 private:
  void compress(const std::uint8_t* block);

  std::array<std::uint32_t, 8> state_;
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t total_bytes_ = 0;
};

}  // namespace mpt::asset_import
