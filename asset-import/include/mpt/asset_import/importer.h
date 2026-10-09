#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "mpt/asset_import/dump_format.h"
#include "mpt/asset_import/gc_disc.h"
#include "mpt/asset_import/known_dumps.h"
#include "mpt/core/dump.h"

namespace mpt::asset_import {

struct IdentifyReport {
  DumpFormat format = DumpFormat::unknown;
  std::string sha256;              // empty if the file was not hashed
  std::optional<KnownDump> match;  // set when the hash is in known_dumps.json
  std::optional<GcDiscHeader> gc_disc;  // set for a structurally valid GameCube disc (ID, region, revision)
  std::string message;             // why identification failed, for the user
};

// Phase 0 pipeline: detect format → normalize → hash → match against known dumps.
// The local build step (recompile / decomp build) arrives with each runtime.
class Importer {
 public:
  explicit Importer(KnownDumps known) : known_(std::move(known)) {}

  // Reads the file at `path`. Never modifies or copies it.
  IdentifyReport identify(const std::filesystem::path& path) const;

 private:
  KnownDumps known_;
};

}  // namespace mpt::asset_import
