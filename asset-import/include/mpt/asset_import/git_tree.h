#pragma once

// Git object IDs computed without git (SHA-1 object format), so a source folder
// can be checked against a lock's tree hash however it arrived: a git checkout, an
// extracted archive or a local override (ADR 0009 §1).
//
// What the hash covers is exactly what git would store for the same files:
// - Empty directories do not count (git does not track them).
// - File bytes are hashed as they are on disk. A checkout whose line endings were
//   converted (core.autocrlf) or an archive that dropped export-ignore files will not
//   match, which is the point: the bytes differ from the pinned commit.
// - Modes: real symlinks are 120000. Every other file is 100644 unless `modes` says
//   otherwise, because Windows and many extractors lose the executable bit.
// - Paths in `gitlinks` become submodule entries (160000) with the given commit, and
//   whatever is on disk under them is ignored.
// - A top-level ".git" (folder or file) is ignored.

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>

namespace mpt::asset_import {

struct TreeHashOptions {
  std::map<std::string, std::string> modes;     // relative path → "100644", "100755" or "120000"
  std::map<std::string, std::string> gitlinks;  // relative path → 40-hex commit
};

std::string git_blob_id(std::span<const std::byte> content);

// The root tree ID of `dir`, or nullopt with `error` set.
std::optional<std::string> git_tree_id(const std::filesystem::path& dir, const TreeHashOptions& options,
                                       std::string& error);

}  // namespace mpt::asset_import
