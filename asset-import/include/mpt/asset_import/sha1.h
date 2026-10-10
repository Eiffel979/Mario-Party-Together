#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace mpt::asset_import {

// Streaming SHA-1 (FIPS 180-4). Used only where an outside format fixes the hash:
// git object IDs (ADR 0009 source pins) and public dump-database records (D8).
// It is never used as a security boundary on its own; dump identity is SHA-256.
class Sha1 {
 public:
  Sha1();
  void update(std::span<const std::byte> data);
  void update(std::string_view text);
  std::array<std::uint8_t, 20> finish();  // the hasher must not be reused afterwards
  std::string finish_hex();               // lowercase hex

 private:
  void compress(const std::uint8_t* block);

  std::array<std::uint32_t, 5> state_;
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t total_bytes_ = 0;
};

// Lowercase hex of a byte string.
std::string to_hex(std::span<const std::uint8_t> bytes);

}  // namespace mpt::asset_import
