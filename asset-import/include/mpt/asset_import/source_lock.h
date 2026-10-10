#pragma once

// Game-source lock files (ADR 0009 §1), for example runtimes/gc_wii/mp4/decomp.lock.
//
// A lock pins every external a recipe builds from by full upstream commit hash.
// Nothing from those sources is ever committed: the lock holds names, URLs, hashes
// and license IDs only. Format (JSON):
//
//   {
//     "lock_version": 1,
//     "sources": [
//       {
//         "name": "partyboard",                          // [a-z0-9_-], unique
//         "repo": "https://github.com/mariopartyrd/partyboard",
//         "commit": "<40 lowercase hex>",                  // the pin
//         "tree": "<40 lowercase hex>",                    // the commit's root tree; needed to
//                                                          // verify user-supplied archives
//         "license": "CC0-1.0",                            // SPDX ID
//         "modes": { "tools/build.sh": "100755" },         // optional: entries that are not
//                                                          // plain files (100755, 120000), so
//                                                          // trees hash right where the file
//                                                          // system cannot say (Windows, zips)
//         "submodules": [                                  // optional: gitlinks in this tree
//           { "path": "extern/musyx", "source": "musyx",   // the lock entry that pins it
//             "gitlink": "<40 hex>",                       // optional: the commit the parent
//             "reason": "..." }                            // records, if it differs from the
//         ],                                               // source's pin (reason required)
//         "deps": [                                        // lock_version 2: dependencies the
//           { "path": "third_party/abseil-cpp",            // source's own lock file (Dawn's
//             "source": "dawn_abseil_cpp" }                // DEPS) pins; each source is a
//         ],                                               // full entry of this lock
//         "skipped_deps": [                                // lock_version 2: DEPS entries we
//           { "path": "third_party/dxc",                   // deliberately do not fetch
//             "reason": "We compile shaders with FXC." }
//         ]
//       }
//     ]
//   }
//
// lock_version 1 files are still read; they cannot use deps or skipped_deps. Unknown
// fields are errors in both versions. When a source with deps or skipped_deps is
// resolved, its DEPS file is cross-checked against them (see sources.h).

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mpt::asset_import {

struct SubmodulePin {
  std::string path;     // '/'-separated, relative to the parent's root
  std::string source;   // name of the lock entry that pins the submodule's contents
  std::string gitlink;  // commit the parent's tree records at `path` (defaults to the source's commit)
  std::string reason;   // why gitlink differs from the source's commit, if it does
};

struct DepPin {
  std::string path;    // the DEPS key, e.g. "third_party/abseil-cpp"
  std::string source;  // name of the lock entry that pins it
};

struct SkippedDep {
  std::string path;
  std::string reason;  // required
};

struct SourcePin {
  std::string name;
  std::string repo;
  std::string commit;
  std::string tree;  // empty if the lock does not record it
  std::string license;
  std::map<std::string, std::string> modes;  // path → git mode ("100755" or "120000")
  std::vector<SubmodulePin> submodules;
  std::vector<DepPin> deps;               // lock_version 2
  std::vector<SkippedDep> skipped_deps;   // lock_version 2
};

struct LockParseOptions {
  // Tests point repos at local bare repositories (file://). Real locks must use https.
  bool allow_file_urls = false;
};

class SourceLock {
 public:
  static std::optional<SourceLock> parse(std::string_view json_text, std::string& error,
                                         const LockParseOptions& options = {});
  static std::optional<SourceLock> load(const std::filesystem::path& path, std::string& error,
                                        const LockParseOptions& options = {});

  const std::vector<SourcePin>& pins() const { return pins_; }
  const SourcePin* find(std::string_view name) const;
  bool empty() const { return pins_.empty(); }

 private:
  std::vector<SourcePin> pins_;
};

bool is_git_object_id(std::string_view text);  // 40 lowercase hex characters

}  // namespace mpt::asset_import
