#include "mpt/asset_import/dump_format.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace mpt::asset_import {
namespace {

bool bytes_at(std::span<const std::byte> data, std::size_t offset, std::initializer_list<std::uint8_t> expected) {
  if (data.size() < offset + expected.size()) return false;
  std::size_t i = offset;
  for (std::uint8_t b : expected) {
    if (data[i++] != static_cast<std::byte>(b)) return false;
  }
  return true;
}

}  // namespace

std::string_view to_string(DumpFormat format) {
  switch (format) {
    case DumpFormat::unknown: return "unknown";
    case DumpFormat::n64_z64: return "N64 (z64, big-endian)";
    case DumpFormat::n64_v64: return "N64 (v64, byte-swapped)";
    case DumpFormat::n64_n64: return "N64 (n64, little-endian)";
    case DumpFormat::gc_disc: return "GameCube disc image";
    case DumpFormat::wii_disc: return "Wii disc image";
    case DumpFormat::wbfs: return "WBFS";
    case DumpFormat::rvz: return "RVZ";
    case DumpFormat::wia: return "WIA";
    case DumpFormat::gcz: return "GCZ";
    case DumpFormat::ciso: return "CISO";
  }
  return "unknown";
}

DumpFormat detect_format(std::span<const std::byte> header) {
  // N64 images start with the PI domain config word 0x80371240 in some byte order.
  if (bytes_at(header, 0, {0x80, 0x37, 0x12, 0x40})) return DumpFormat::n64_z64;
  if (bytes_at(header, 0, {0x37, 0x80, 0x40, 0x12})) return DumpFormat::n64_v64;
  if (bytes_at(header, 0, {0x40, 0x12, 0x37, 0x80})) return DumpFormat::n64_n64;

  if (bytes_at(header, 0, {'W', 'B', 'F', 'S'})) return DumpFormat::wbfs;
  if (bytes_at(header, 0, {'R', 'V', 'Z', 0x01})) return DumpFormat::rvz;
  if (bytes_at(header, 0, {'W', 'I', 'A', 0x01})) return DumpFormat::wia;
  if (bytes_at(header, 0, {0x01, 0xC0, 0x0B, 0xB1})) return DumpFormat::gcz;
  if (bytes_at(header, 0, {'C', 'I', 'S', 'O'})) return DumpFormat::ciso;

  // Disc header magic words.
  if (bytes_at(header, 0x18, {0x5D, 0x1C, 0x9E, 0xA3})) return DumpFormat::wii_disc;
  if (bytes_at(header, 0x1C, {0xC2, 0x33, 0x9F, 0x3D})) return DumpFormat::gc_disc;

  return DumpFormat::unknown;
}

bool is_n64(DumpFormat format) {
  return format == DumpFormat::n64_z64 || format == DumpFormat::n64_v64 || format == DumpFormat::n64_n64;
}

bool is_compressed_container(DumpFormat format) {
  switch (format) {
    case DumpFormat::wbfs:
    case DumpFormat::rvz:
    case DumpFormat::wia:
    case DumpFormat::gcz:
    case DumpFormat::ciso:
      return true;
    default:
      return false;
  }
}

bool normalize_n64(std::span<std::byte> data, DumpFormat format) {
  if (!is_n64(format) || data.size() % 4 != 0) return false;
  if (format == DumpFormat::n64_v64) {
    for (std::size_t i = 0; i < data.size(); i += 2) std::swap(data[i], data[i + 1]);
  } else if (format == DumpFormat::n64_n64) {
    for (std::size_t i = 0; i < data.size(); i += 4) std::reverse(data.begin() + i, data.begin() + i + 4);
  }
  return true;
}

}  // namespace mpt::asset_import
