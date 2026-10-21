#pragma once

// Real titles in the dump lane (P1-16): their shell registrations, and the VerifiedDump for a title
// from the developer's dump directory, built through asset-import exactly as the shell's --dump does
// (identify, then the title's recipe in a BuildCache, then make_verified).
//
// Environment (all optional except MPT_DUMP_DIR, which main.cpp checks):
//   MPT_BUILD_CACHE   the BuildCache root; default <MPT_DUMP_DIR>/mpt-build-cache (outside the repository)
//   MPT_SOURCE_CACHE  a folder of pinned source checkouts (<name>/<commit>, as mpt_fetch_sources makes
//                     them). Used instead of fetching; each must still match the lock to count as pinned.

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mpt/asset_import/build.h"
#include "mpt/asset_import/importer.h"
#include "mpt/asset_import/known_dumps.h"
#include "mpt/core/dump.h"
#include "mpt/core/title_registry.h"
#include "titles.h"  // core/shell (mpt::shell_core): ShellTitles

namespace mpt::dump_lane {

inline const shell::ShellTitles& shell_titles() {
  static const shell::ShellTitles titles = [] {
    shell::ShellTitles t;
    shell::register_titles(t);
    return t;
  }();
  return titles;
}

inline void register_real_titles(TitleRegistry& registry) {
  for (const shell::ShellTitle& title : shell_titles()) {
    if (title.synthetic_dump) continue;  // test registrations never reach the dump lane
    (void)registry.add(title.id, title.runtime, title.adapter);
  }
}

// Finds the dump for `title` in `dump_dir` (its regular files, not subfolders) and builds it.
inline std::variant<VerifiedDump, Result> build_dump(TitleId title, const std::filesystem::path& dump_dir,
                                                     const std::filesystem::path& known_dumps_json,
                                                     const std::filesystem::path& build_cache,
                                                     const std::filesystem::path& source_cache) {
  const shell::ShellTitle* entry = nullptr;
  for (const shell::ShellTitle& t : shell_titles())
    if (t.id == title) entry = &t;
  const std::string name(to_string(title));
  if (entry == nullptr || !entry->recipe) return Result::failure("no build recipe for " + name + " in this build");

  std::string error;
  auto known = asset_import::KnownDumps::load(known_dumps_json, error);
  if (!known) return Result::failure(error);
  // Only files that can be this title's supported dump are read at all: the right size and, for a
  // disc, the right ID in its first bytes. Other titles' dumps in the folder are never hashed.
  std::vector<std::pair<std::uint64_t, std::string>> candidates;  // size, disc ID ("" for cartridges)
  for (const asset_import::KnownDump& k : known->entries())
    if (k.title == title && k.supported && k.size != 0) candidates.emplace_back(k.size, k.disc_id);
  auto could_be = [&](const std::filesystem::path& path) {
    std::error_code size_ec;
    const std::uint64_t size = std::filesystem::file_size(path, size_ec);
    if (size_ec) return false;
    for (const auto& [want_size, disc_id] : candidates) {
      if (size != want_size) continue;
      if (disc_id.empty()) return true;
      std::ifstream in(path, std::ios::binary);
      std::string head(disc_id.size(), '\0');
      if (in.read(head.data(), static_cast<std::streamsize>(head.size())) && head == disc_id) return true;
    }
    return false;
  };
  const asset_import::Importer importer(std::move(*known));
  std::error_code ec;
  for (const auto& file : std::filesystem::directory_iterator(dump_dir, ec)) {
    if (!file.is_regular_file(ec) || !could_be(file.path())) continue;
    const asset_import::IdentifyReport report = importer.identify(file.path());
    if (!report.match || report.match->title != title) continue;
    std::printf("dump lane: found %s %s %s\n", name.c_str(), report.match->region.c_str(),
                report.match->revision.c_str());
    std::unique_ptr<asset_import::BuildRecipe> recipe = entry->recipe();
    if (!recipe) return Result::failure("the " + name + " recipe could not be created (no decomp.lock?)");
    auto dump = asset_import::dump_for_build(report, file.path(), error);
    if (!dump) return Result::failure(error);
    asset_import::SystemProcessRunner runner;
    asset_import::BuildCache cache(build_cache, runner);
    asset_import::BuildRequest request;
    request.dump = std::move(*dump);
    if (!source_cache.empty()) {
      for (const auto& pin : recipe->sources().pins())
        request.source_inputs[pin.name] = {asset_import::SourceOrigin::override_dir, source_cache / pin.name / pin.commit};
    }
    request.progress = [](const asset_import::BuildProgress& p) {
      std::printf("build: [%zu/%zu] %s\n", p.stage_index + 1, p.stage_count, p.stage.c_str());
    };
    auto outcome = cache.build(*recipe, request, error);
    if (!outcome) return Result::failure("build failed: " + error);
    if (outcome->manifest.unpinned) std::puts("dump lane: WARNING: the build is unpinned (a source does not match the lock)");
    auto verified = asset_import::make_verified(report, *outcome, error);
    if (!verified) return Result::failure(error);
    return *verified;
  }
  return Result::failure("no " + name + " dump in " + dump_dir.string());
}

}  // namespace mpt::dump_lane
