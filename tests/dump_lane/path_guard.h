#pragma once

// Paths the dump lane writes to or reads game material from must lie outside the repository
// (CLAUDE.md rule 1, ADR 0006): the dump itself, frame captures (the user's game output), the
// build cache (an extracted disc and a built module) and the source cache (pinned decomp
// checkouts, never vendored, ADR 0009). A path that is inside is refused before anything runs.

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

}  // namespace mpt::dump_lane
