#include "mpt/asset_import/sha1.h"

#include <bit>

namespace mpt::asset_import {

Sha1::Sha1() : state_{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0} {}

void Sha1::compress(const std::uint8_t* block) {
  std::array<std::uint32_t, 80> w;
  for (int i = 0; i < 16; ++i) {
    w[i] = (std::uint32_t{block[4 * i]} << 24) | (std::uint32_t{block[4 * i + 1]} << 16) |
           (std::uint32_t{block[4 * i + 2]} << 8) | std::uint32_t{block[4 * i + 3]};
  }
  for (int i = 16; i < 80; ++i) w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

  auto [a, b, c, d, e] = state_;
  for (int i = 0; i < 80; ++i) {
    std::uint32_t f;
    std::uint32_t k;
    if (i < 20) {
      f = (b & c) | (~b & d);
      k = 0x5A827999;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDC;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6;
    }
    const std::uint32_t t = std::rotl(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = std::rotl(b, 30);
    b = a;
    a = t;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
}

void Sha1::update(std::span<const std::byte> data) {
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

void Sha1::update(std::string_view text) { update(std::as_bytes(std::span(text.data(), text.size()))); }

std::array<std::uint8_t, 20> Sha1::finish() {
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

  std::array<std::uint8_t, 20> digest;
  for (std::size_t i = 0; i < 5; ++i) {
    digest[4 * i] = static_cast<std::uint8_t>(state_[i] >> 24);
    digest[4 * i + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
    digest[4 * i + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
    digest[4 * i + 3] = static_cast<std::uint8_t>(state_[i]);
  }
  return digest;
}

std::string Sha1::finish_hex() {
  const auto digest = finish();
  return to_hex(digest);
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (std::uint8_t b : bytes) {
    out.push_back(kHex[b >> 4]);
    out.push_back(kHex[b & 0xf]);
  }
  return out;
}

}  // namespace mpt::asset_import
