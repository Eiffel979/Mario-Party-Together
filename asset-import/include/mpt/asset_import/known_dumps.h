#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mpt/core/result.h"
#include "mpt/core/title_id.h"

namespace mpt::asset_import {

// One known-good dump. Hashes and metadata only, never content.
struct KnownDump {
  TitleId title{};
  std::string region;
  std::string revision;
  std::string sha256;  // of the normalized image (z64 for N64, raw disc image for GC/Wii)
  std::uint64_t size = 0;
};

class KnownDumps {
 public:
  // Parses and validates known_dumps.json text. On failure `error` describes the first problem.
  static std::optional<KnownDumps> parse(std::string_view json_text, std::string& error);
  static std::optional<KnownDumps> load(const std::filesystem::path& path, std::string& error);

  const KnownDump* find_by_sha256(std::string_view sha256) const;
  const std::vector<KnownDump>& entries() const { return entries_; }

 private:
  std::vector<KnownDump> entries_;
};

}  // namespace mpt::asset_import
