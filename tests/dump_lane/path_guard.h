#pragma once

// Paths the dump lane writes to or reads game material from must lie outside the repository
// (CLAUDE.md rule 1, ADR 0006): the dump itself, frame captures (the user's game output), the
// build cache (an extracted disc and a built module) and the source cache (pinned decomp
// checkouts, never vendored, ADR 0009). A path that is inside is refused before anything runs.
// The build cache also stays out of MPT_DUMP_DIR, which holds the user's dumps only.

#include <filesystem>
#include <span>
#include <string>

namespace mpt::dump_lane {

// Whether `path` is `root` or lies below it, after resolving `.`, `..` and symlinks of the parts
// that exist. A relative `path` is taken from the current directory. Paths on another drive or
// root are outside.
inline bool is_inside(const std::filesystem::path& path, const std::filesystem::path& root) {
  std::error_code ec;
  const std::filesystem::path p = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec), ec);
  const std::filesystem::path r = std::filesystem::weakly_canonical(std::filesystem::absolute(root, ec), ec);
  if (p.root_name() != r.root_name()) return false;
  const std::filesystem::path rel = p.lexically_relative(r);
  return !rel.empty() && *rel.begin() != "..";
}

struct GuardedPath {
  const char* what = "";       // for the message, e.g. "MPT_BUILD_CACHE"
  std::filesystem::path path;  // empty: not in use, never refused
};

// The refusal message for the first path in use that lies inside `root`, or "" if none does.
inline std::string refuse_inside(std::span<const GuardedPath> paths, const std::filesystem::path& root) {
  for (const GuardedPath& g : paths) {
    if (g.path.empty() || !is_inside(g.path, root)) continue;
    return std::string(g.what) + " (" + g.path.string() +
           ") is inside the repository; game material must stay outside it (ADR 0006)";
  }
  return {};
}

// The build cache root: MPT_BUILD_CACHE when set, else the shell's (`default_root`, its UserDirs
// cache), so the lane and the shell share one cache.
struct BuildCacheRoot {
  std::filesystem::path path;
  bool from_env = false;  // MPT_BUILD_CACHE named it
};

inline BuildCacheRoot build_cache_root(const std::string& env, const std::filesystem::path& default_root) {
  if (!env.empty()) return {std::filesystem::path(env), true};
  return {default_root, false};
}

// The refusal message if the default cache lies inside the dump folder, or "". An MPT_BUILD_CACHE
// there is the user's explicit choice of their own folder and is allowed (it is outside the
// repository, which refuse_inside checks); main.cpp prints a note for it.
inline std::string refuse_cache_in_dump(const BuildCacheRoot& cache, const std::filesystem::path& dump_dir) {
  if (cache.from_env || !is_inside(cache.path, dump_dir)) return {};
  return "the default build cache (" + cache.path.string() + ") is inside MPT_DUMP_DIR, which holds dumps only; " +
         "set MPT_BUILD_CACHE to a folder outside it";
}

}  // namespace mpt::dump_lane
