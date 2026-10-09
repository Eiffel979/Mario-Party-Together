#pragma once

// Build orchestration (Phase 1 work item P1-10, ADR 0009 §3).
//
// Runtime owners describe how a title is built with a BuildRecipe: its sources (a
// lock), whether it needs the disc extracted, a toolchain check and a list of
// steps. BuildCache runs a recipe for one verified dump and keeps the result in the
// user's local cache:
//
//   <cache_root>/
//     sources/<source name>/<commit>/        fetched, verified game-source checkouts
//     builds/<recipe id>/<key>/              one finished build
//       manifest.json                         what went into it (written last)
//       disc/                                 the extracted disc, if the recipe asks
//       ...                                   whatever the steps produce
//     builds/<recipe id>/.tmp-<key>/          a build in progress (renamed into place
//                                             when complete, removed if interrupted)
//     builds/<recipe id>/.lock                held while a build of that recipe runs
//
// <key> is derived from the dump's SHA-256, the recipe ID and version, the runtime
// build version and every source pin (or, for an unpinned override, a fingerprint
// of what was found). Changing any of them gives a new key, so a dump is built once
// and rebuilt only when one of them changes. A build directory without a complete,
// matching manifest is never used.
//
// Nothing here uploads, shares or copies the output anywhere else: build output is
// the user's own and stays on their machine (ADR 0001).

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mpt/asset_import/process.h"
#include "mpt/asset_import/source_lock.h"
#include "mpt/asset_import/sources.h"
#include "mpt/core/result.h"

namespace mpt::asset_import {

struct BuildProgress {
  std::string stage;            // "Checking sources", "Extracting disc", a step's name, "Finishing"
  std::size_t stage_index = 0;  // 0-based
  std::size_t stage_count = 0;
  double fraction = 0.0;        // progress within the stage, 0..1
  std::string detail;           // optional extra text for the UI
};
using ProgressCallback = std::function<void(const BuildProgress&)>;

// What a step sees. Paths are inside the temporary build directory, which becomes
// the build directory when every step has succeeded.
struct BuildContext {
  std::filesystem::path output_dir;  // write build output here
  std::filesystem::path disc_dir;    // extracted disc (sys/, files/), empty if the recipe needs none
  std::map<std::string, ResolvedSource> sources;
  std::string dump_sha256;
  std::string game_version;          // e.g. "GMPE01_00", as given in the request
  bool unpinned = false;             // true if any source is an unpinned override
  ProcessRunner* runner = nullptr;   // for steps that run tools
  // Reports progress within the current step (0..1).
  std::function<void(double fraction, std::string_view detail)> report;
  // True once the user has asked to cancel. Long steps should check it and stop.
  std::function<bool()> cancelled;
};

class BuildStep {
 public:
  virtual ~BuildStep() = default;
  virtual std::string name() const = 0;  // shown in progress, e.g. "Compiling game module"
  virtual Result run(BuildContext& context) = 0;
};

struct ToolchainInfo {
  std::string description;  // recorded in the manifest, e.g. "clang-cl 19.1.5"
};

class BuildRecipe {
 public:
  virtual ~BuildRecipe() = default;
  // Lowercase [a-z0-9_], e.g. "mp4". Names the recipe's folder in the cache.
  virtual std::string id() const = 0;
  // Bump whenever a change to the recipe changes its output.
  virtual std::uint32_t version() const = 0;
  // The version of the runtime this build is for; a new value forces a rebuild.
  virtual std::string runtime_build_version() const = 0;
  // Pinned game sources. Every entry is resolved before the steps run.
  virtual const SourceLock& sources() const = 0;
  // Whether the disc is extracted into <build>/disc before the steps run.
  virtual bool extracts_disc() const = 0;
  // Checks the tools the steps need. Fails with a message such as "clang-cl was not
  // found. Install LLVM 19 and try again." Not called when the cache already has the build.
  virtual Result check_toolchain(ProcessRunner& runner, ToolchainInfo& info) const = 0;
  virtual std::vector<std::unique_ptr<BuildStep>> steps() const = 0;
};

struct DumpForBuild {
  std::string sha256;           // the verified dump's SHA-256 (from the importer)
  std::uint64_t size = 0;
  std::filesystem::path path;   // the user's dump file, read only
  std::string game_version;     // e.g. "GMPE01_00"
};

struct BuildRequest {
  DumpForBuild dump;
  // Per source name; sources not listed are fetched by commit.
  std::map<std::string, SourceInput> source_inputs;
  ProgressCallback progress;
  std::function<bool()> cancelled;
  bool force_rebuild = false;
};

struct ManifestSource {
  std::string name;
  std::string commit;  // the lock's pin
  std::string origin;  // "fetched", "archive" or "override"
  bool pinned = true;
  std::string actual;  // what was found, see ResolvedSource::actual
};

struct BuildManifest {
  std::string key;  // full 64-hex build key
  std::string recipe;
  std::uint32_t recipe_version = 0;
  std::string runtime_build_version;
  std::string dump_sha256;
  std::string game_version;
  std::vector<ManifestSource> sources;
  bool unpinned = false;  // never produces golden trails or exit evidence, never joins netplay
  std::string toolchain;

  std::string to_json() const;
  static std::optional<BuildManifest> parse(std::string_view json_text, std::string& error);
};

struct BuildOutcome {
  std::filesystem::path build_dir;
  BuildManifest manifest;
  bool reused = false;  // true if the cache already had this build
};

// The build key for these inputs: SHA-256 over a canonical description of them.
std::string build_key(const BuildRecipe& recipe, std::string_view dump_sha256,
                      const std::vector<ManifestSource>& sources);

class BuildCache {
 public:
  BuildCache(std::filesystem::path cache_root, ProcessRunner& runner)
      : root_(std::move(cache_root)), runner_(runner) {}

  // Returns the cached build for these inputs, or builds it. On failure `error`
  // says what went wrong and nothing is left in the build folder.
  std::optional<BuildOutcome> build(const BuildRecipe& recipe, const BuildRequest& request, std::string& error);

  const std::filesystem::path& root() const { return root_; }

 private:
  std::filesystem::path root_;
  ProcessRunner& runner_;
};

}  // namespace mpt::asset_import
