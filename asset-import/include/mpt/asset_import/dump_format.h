#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace mpt::asset_import {

enum class DumpFormat {
  unknown,
  n64_z64,  // big-endian, the canonical N64 byte order
  n64_v64,  // 16-bit byte-swapped
  n64_n64,  // 32-bit word-swapped (little-endian)
  gc_disc,  // raw GameCube disc image (.iso/.gcm)
  wii_disc, // raw Wii disc image (.iso)
  wbfs,     // compressed containers: recognized, but not read yet
  rvz,
  wia,
  gcz,
  ciso,
};

std::string_view to_string(DumpFormat format);

// How many leading bytes detect_format() needs to see.
inline constexpr std::size_t kFormatProbeSize = 0x40;

// Detects a dump's format from its first bytes (at least kFormatProbeSize when available).
DumpFormat detect_format(std::span<const std::byte> header);

bool is_n64(DumpFormat format);
bool is_compressed_container(DumpFormat format);

// Rewrites an N64 image in place into z64 (big-endian) byte order.
// Returns false if `format` is not an N64 format or the size is not a multiple of 4.
bool normalize_n64(std::span<std::byte> data, DumpFormat format);

}  // namespace mpt::asset_import
