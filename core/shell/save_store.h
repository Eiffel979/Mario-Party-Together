#pragma once

// Host-owned save media on disk (ADR 0004 rule 5). The shell is the only code that
// touches these files; runtimes see only SaveMedia bytes.
//
//   <saves root>/<title>/<region>/slot0.mptsave
//
// The file holds the same image bytes a replay header digests (ADR 0006 amendment,
// point 2): u32 format_version (little-endian) followed by the data. Phase 1 has one slot
// per title and region. Save files are user data with game-made content: they live under
// SDL_GetPrefPath, never in the repo or the build cache.

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "mpt/core/result.h"
#include "mpt/core/save_media.h"
#include "mpt/core/title_id.h"

namespace mpt::shell {

// <saves_root>/<title>/<region>/slot0.mptsave. A region that is not plain
// [A-Za-z0-9_-] (or is empty) becomes "unknown", so it can never escape the folder.
std::filesystem::path save_slot_path(const std::filesystem::path& saves_root, TitleId title, std::string_view region);

// Writes `bytes` to a temporary file next to `path`, then renames it over `path`, so a
// crash never leaves a half-written save. Creates missing parent folders.
Result write_file_atomic(const std::filesystem::path& path, std::span<const std::byte> bytes);

// nullopt if the file does not exist (a blank card). A file that exists but cannot be
// read or is shorter than 4 bytes is an error.
std::optional<SaveMedia> load_save_media(const std::filesystem::path& path, std::string& error);

Result store_save_media(const std::filesystem::path& path, const SaveMedia& media);

}  // namespace mpt::shell
