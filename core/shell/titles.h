#pragma once

// Titles the shell can run (P1-06 a). The list is generated at configure time from the
// runtime targets that are enabled in this build: each runtime calls
// mpt_register_title(TARGET <lib> FUNCTION <fn>) in CMake, and <fn>(ShellTitles&) adds
// its entry. Core never names a concrete title; only the generated file does.

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "mpt/asset_import/build.h"
#include "mpt/core/dump.h"
#include "mpt/core/title_id.h"
#include "mpt/core/title_registry.h"

namespace mpt::shell {

struct ShellTitle {
  TitleId id{};
  TitleRegistry::RuntimeFactory runtime{};
  TitleRegistry::AdapterFactory adapter{};
  // Builds the title from a verified dump (ADR 0009 §3). Null = this build cannot make
  // one, so --dump fails with a clear message.
  std::function<std::unique_ptr<asset_import::BuildRecipe>()> recipe{};
  // Test registrations only (FakeRuntime): the dump used when --dump is not given.
  // Real titles leave it empty; their VerifiedDump comes from asset_import::make_verified.
  std::optional<VerifiedDump> synthetic_dump{};
};

using ShellTitles = std::vector<ShellTitle>;

// Generated (registered_titles.cpp): registers every enabled runtime.
void register_titles(ShellTitles& titles);

}  // namespace mpt::shell
