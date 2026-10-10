#include "mpt/core/sha256.h"

#include <bit>

namespace mpt {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

Sha256::Sha256()
    : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::compress(const std::uint8_t* block) {
  std::array<std::uint32_t, 64> w;
  for (std::size_t i = 0; i < 16; ++i) {
    w[i] = (std::uint32_t{block[4 * i]} << 24) | (std::uint32_t{block[4 * i + 1]} << 16) |
           (std::uint32_t{block[4 * i + 2]} << 8) | std::uint32_t{block[4 * i + 3]};
  }
  for (std::size_t i = 16; i < 64; ++i) {
    const std::uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const std::uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  auto [a, b, c, d, e, f, g, h] = state_;
  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
    const std::uint32_t ch = (e & f) ^ (~e & g);
    const std::uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
    const std::uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Sha256::update(std::span<const std::byte> data) {
  total_bytes_ += data.size();
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
  std::size_t remaining = data.size();
  while (remaining > 0 && (buffered_ > 0 || remaining < buffer_.size())) {
    buffer_[buffered_++] = *bytes++;
    --remaining;
    if (buffered_ == buffer_.size()) {
      compress(buffer_.data());
      buffered_ = 0;
    }
  }
  for (; remaining >= buffer_.size(); remaining -= buffer_.size(), bytes += buffer_.size()) compress(bytes);
  while (remaining > 0) {
    buffer_[buffered_++] = *bytes++;
    --remaining;
  }
}

Sha256Digest Sha256::finish() {
  const std::uint64_t bit_length = total_bytes_ * 8;
  buffer_[buffered_++] = 0x80;
  if (buffered_ > 56) {
    while (buffered_ < 64) buffer_[buffered_++] = 0;
    compress(buffer_.data());
    buffered_ = 0;
  }
  while (buffered_ < 56) buffer_[buffered_++] = 0;
  for (int i = 7; i >= 0; --i) buffer_[buffered_++] = static_cast<std::uint8_t>(bit_length >> (8 * i));
  compress(buffer_.data());

  Sha256Digest digest;
  for (std::size_t i = 0; i < 8; ++i) {
    digest[4 * i] = static_cast<std::uint8_t>(state_[i] >> 24);
    digest[4 * i + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
    digest[4 * i + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
    digest[4 * i + 3] = static_cast<std::uint8_t>(state_[i]);
  }
  return digest;
}

Sha256Digest Sha256::of(std::span<const std::byte> data) {
  Sha256 hasher;
  hasher.update(data);
  return hasher.finish();
}

std::string to_hex(const Sha256Digest& digest) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (std::uint8_t b : digest) {
    out.push_back(kHex[b >> 4]);
    out.push_back(kHex[b & 0xf]);
  }
  return out;
}

std::optional<Sha256Digest> parse_sha256_hex(std::string_view hex) {
  if (hex.size() != 64) return std::nullopt;
  Sha256Digest out{};
  for (std::size_t i = 0; i < 32; ++i) {
    const int hi = hex_value(hex[2 * i]);
    const int lo = hex_value(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return std::nullopt;
    out[i] = static_cast<std::uint8_t>(hi * 16 + lo);
  }
  return out;
}

}  // namespace mpt
