#pragma once

// The shell's per-user folders: the root (SDL_GetPrefPath, or --user-dir), saves and the BuildCache
// root. Shared with the dump lane so both build into the same cache (tests/dump_lane/README.md).

#include <filesystem>
#include <optional>
#include <string>

namespace mpt::shell {

struct UserDirs {
  std::filesystem::path root;
  std::filesystem::path saves;  // <root>/saves/<title>/<region>/ (ADR 0004 rule 5)
  std::filesystem::path cache;  // <root>/cache: the BuildCache root (P1-10)
};

// The folders under `user_dir`, or under the platform's preference folder when it is unset. SDL
// creates the preference folder; SDL need not be initialized. nullopt with `error` set if the
// platform has none.
std::optional<UserDirs> user_dirs(const std::optional<std::filesystem::path>& user_dir, std::string& error);

}  // namespace mpt::shell
