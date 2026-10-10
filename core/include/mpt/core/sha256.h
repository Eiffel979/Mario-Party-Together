#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mpt {

using Sha256Digest = std::array<std::uint8_t, 32>;

// Streaming SHA-256 (FIPS 180-4), for identities that core records: the replay
// header's dump and save-media digests (ADR 0006). Not used for state hashes.
class Sha256 {
 public:
  Sha256();
  void update(std::span<const std::byte> data);
  Sha256Digest finish();  // the hasher must not be reused afterwards

  static Sha256Digest of(std::span<const std::byte> data);

 private:
  void compress(const std::uint8_t* block);

  std::array<std::uint32_t, 8> state_;
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t total_bytes_ = 0;
};

std::string to_hex(const Sha256Digest& digest);  // lowercase
// Parses exactly 64 hex digits (either case). nullopt otherwise.
std::optional<Sha256Digest> parse_sha256_hex(std::string_view hex);

}  // namespace mpt
