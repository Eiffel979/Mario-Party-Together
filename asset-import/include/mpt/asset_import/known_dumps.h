#pragma once

// known_dumps.json: the dumps asset-import recognizes. Hashes and metadata only,
// never content (ADR 0001). Schema version 2 (Phase 1 work item P1-11):
//
//   {
//     "version": 2,
//     "dumps": [
//       {
//         "title": "mp4",                 // title ID
//         "region": "USA",
//         "revision": "Rev 0",
//         "supported": true,              // false: known, but refused with `reason`
//         "game_version": "GMPE01_00",    // optional: decomp-style version tag
//         "disc_id": "GMPE01",            // optional (disc titles): header disc ID
//         "disc_version": 0,              // optional: header version byte; absent = any
//         "size": 1459978240,             // bytes; required if supported
//         "sha256": "<64 hex>",           // the identity; required if supported, and
//                                         // computed locally from a verified dump
//         "sha1": "<40 hex>",             // required if supported; may come from a public
//         "md5": "<32 hex>",              // dump database (D8), as may md5 (optional)
//         "reason": "...",                // required if unsupported: shown to the user
//         "note": "..."                   // optional: provenance, never shown
//       }
//     ]
//   }
//
// A supported entry matches only on SHA-256, SHA-1 and size together. Unsupported
// entries exist so a known revision or region is named, not called "unknown" (E11):
// they match by SHA-1 if they have one, or by disc ID and version byte.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mpt/core/result.h"
#include "mpt/core/title_id.h"

namespace mpt::asset_import {

struct KnownDump {
  TitleId title{};
  std::string region;
  std::string revision;
  bool supported = false;
  std::string game_version;                 // e.g. "GMPE01_00"; may be empty
  std::string disc_id;                      // e.g. "GMPE01"; empty for cartridges
  std::optional<std::uint8_t> disc_version; // header version byte, if the entry names one
  std::uint64_t size = 0;                   // 0 if unknown (unsupported entries only)
  std::string sha256;  // of the normalized image (z64 for N64, raw disc image for GC/Wii)
  std::string sha1;
  std::string md5;
  std::string reason;  // why an unsupported entry is refused
};

class KnownDumps {
 public:
  // Parses and validates known_dumps.json text. On failure `error` describes the first problem.
  static std::optional<KnownDumps> parse(std::string_view json_text, std::string& error);
  static std::optional<KnownDumps> load(const std::filesystem::path& path, std::string& error);

  // The supported entry with exactly these hashes and size, if any.
  const KnownDump* find_supported(std::string_view sha256, std::string_view sha1, std::uint64_t size) const;
  // Any entry with this SHA-1 (and size, if the entry records one).
  const KnownDump* find_by_sha1(std::string_view sha1, std::uint64_t size) const;
  // Entries whose disc ID matches and whose version byte matches or is unspecified.
  std::vector<const KnownDump*> find_by_disc(std::string_view disc_id, std::uint8_t disc_version) const;

  const std::vector<KnownDump>& entries() const { return entries_; }

 private:
  std::vector<KnownDump> entries_;
};

// The title's name as plain text for messages, e.g. "Mario Party 4".
std::string_view title_display_name(TitleId title);

}  // namespace mpt::asset_import
