#pragma once

// Getting each pinned game source onto the user's machine and proving it is the
// pinned one (ADR 0009 §1):
// - fetch:    git clone by URL and check out the locked commit into the cache, then
//             check HEAD, its tree and each submodule gitlink against the lock.
// - archive:  a user-supplied, already extracted archive (for example a GitHub zip).
//             Accepted only if its computed git tree hash equals the lock's tree,
//             with submodule paths treated as the locked gitlinks. Otherwise refused
//             with a request for a git checkout.
// - override: a developer's own folder (development only). Used as it is; marked
//             `unpinned` unless it is a clean checkout of the locked commit or its
//             tree hash matches the lock.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "mpt/asset_import/process.h"
#include "mpt/asset_import/source_lock.h"

namespace mpt::asset_import {

enum class SourceOrigin { fetched, archive, override_dir };
std::string_view to_string(SourceOrigin origin);

struct SourceInput {
  SourceOrigin origin = SourceOrigin::fetched;
  std::filesystem::path path;  // archive or override folder; unused for fetched
};

struct ResolvedSource {
  std::string name;
  std::filesystem::path dir;  // where the recipe reads the source from
  SourceOrigin origin = SourceOrigin::fetched;
  bool pinned = false;        // false only for overrides that do not match the lock
  std::string commit;         // the lock's commit
  std::string actual;         // what was found: "<commit>", "<commit>-dirty" or "tree:<tree id>"
};

class SourceResolver {
 public:
  // Fetched sources live under `cache_dir`/<name>/<commit>.
  SourceResolver(std::filesystem::path cache_dir, ProcessRunner& runner)
      : cache_dir_(std::move(cache_dir)), runner_(runner) {}

  std::optional<ResolvedSource> resolve(const SourcePin& pin, const SourceInput& input, std::string& error);

 private:
  std::optional<ResolvedSource> fetch(const SourcePin& pin, std::string& error);
  std::optional<ResolvedSource> from_archive(const SourcePin& pin, const std::filesystem::path& dir,
                                             std::string& error);
  std::optional<ResolvedSource> from_override(const SourcePin& pin, const std::filesystem::path& dir,
                                              std::string& error);
  ProcessResult git(const std::filesystem::path& cwd, std::vector<std::string> args);
  bool check_checkout(const SourcePin& pin, const std::filesystem::path& dir, std::string& error);

  std::filesystem::path cache_dir_;
  ProcessRunner& runner_;
};

}  // namespace mpt::asset_import
