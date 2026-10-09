#pragma once

#include <filesystem>
#include <string>

#include "mpt/core/title_id.h"

namespace mpt {

// A user-supplied dump that asset-import has matched against known_dumps.json and
// built locally. Runtimes never see the user's original file, only this handle.
struct VerifiedDump {
  TitleId title{};
  std::string region{};        // e.g. "USA", "EUR", "JPN"
  std::string revision{};      // e.g. "1.0"
  std::string sha256{};        // lowercase hex of the normalized dump
  std::filesystem::path build_dir{};  // local cache with the built runtime and assets
};

}  // namespace mpt
