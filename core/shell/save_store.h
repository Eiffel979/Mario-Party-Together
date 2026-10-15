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

#include "durable_file.h"
#include "mpt/core/result.h"
#include "mpt/core/save_media.h"
#include "mpt/core/title_id.h"

namespace mpt::shell {

// <saves_root>/<title>/<region>/slot0.mptsave. A region that is not plain
// [A-Za-z0-9_-] (or is empty) becomes "unknown", so it can never escape the folder.
std::filesystem::path save_slot_path(const std::filesystem::path& saves_root, TitleId title, std::string_view region);


// nullopt if the file does not exist (a blank card). A file that exists but cannot be
// read or is shorter than 4 bytes is an error.
std::optional<SaveMedia> load_save_media(const std::filesystem::path& path, std::string& error);

Result store_save_media(const std::filesystem::path& path, const SaveMedia& media);

// What a live session starts from (P1-06 g). A slot that is missing is a blank card. A
// slot that exists but cannot be read is never overwritten: it is renamed to
// slot0.mptsave.bak (or .bak2, .bak3, ... if those exist), and the session starts blank.
// If that rename fails, the session starts blank and ephemeral (`persist` false), so the
// user's file is left exactly as it was.
struct SlotOpen {
  std::optional<SaveMedia> initial{};
  bool persist = true;
  std::string warning{};  // empty unless the slot was unreadable
};
SlotOpen open_save_slot(const std::filesystem::path& path);

}  // namespace mpt::shell
