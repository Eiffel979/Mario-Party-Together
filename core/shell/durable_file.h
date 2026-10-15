#pragma once

// Durable whole-file writes for host-side user data: save slots (save_store.h), the
// launcher's library list (ui/), recorded replays. Shell-internal, in mpt_shell_core.

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

#include "mpt/core/result.h"

namespace mpt::shell {

// UTF-8 text of a path, for messages and logs. path::string() throws on Windows for
// names outside the ANSI code page, and user folders contain the user's name.
std::string display_path(const std::filesystem::path& path);

// Replaces the file at `path` with `bytes`, durably and atomically:
//   1. creates missing parent folders;
//   2. writes a temporary file next to `path`, named <path>.tmp-<pid>-<n>, unique per
//      process and call, so two processes writing the same file never share a temp;
//   3. flushes it to the disk (FlushFileBuffers / fsync);
//   4. renames it over `path` (MoveFileExW with MOVEFILE_REPLACE_EXISTING |
//      MOVEFILE_WRITE_THROUGH, or rename(2) followed by an fsync of the folder).
// A crash at any point leaves either the old file or the new one, never a mix. On
// failure the temporary file is removed and `path` is unchanged. Concurrent writers to
// the same path: the last rename wins.
Result write_file_atomic(const std::filesystem::path& path, std::span<const std::byte> bytes);

}  // namespace mpt::shell
