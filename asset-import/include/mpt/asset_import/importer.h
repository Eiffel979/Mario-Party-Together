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
  std::string sha1;                // computed in the same read as sha256
  std::optional<KnownDump> match;  // set when a supported entry matches (sha256, sha1 and size)
  std::optional<KnownDump> unsupported;  // set when the dump is a known but unsupported version
  std::optional<GcDiscHeader> gc_disc;  // the disc header (ID, region, revision), if it has one
  std::string message;             // why identification failed, for the user
};

// Detect format → normalize → hash (SHA-256 and SHA-1 in one read) → match against
// known dumps. Building a verified dump is BuildCache's job (build.h).
class Importer {
 public:
  explicit Importer(KnownDumps known) : known_(std::move(known)) {}

  // Reads the file at `path`. Never modifies or copies it.
  IdentifyReport identify(const std::filesystem::path& path) const;

 private:
  KnownDumps known_;
};

}  // namespace mpt::asset_import
