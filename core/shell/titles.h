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
  // Releases what the title keeps for the life of the process, such as a game module that
  // stays loaded between runtimes (ADR 0008, "Game module kept loaded for the process").
  // Called once, by shutdown_titles(), after every runtime and adapter of the title is
  // destroyed and before the process exits. It must be safe when the title never booted, and
  // must not need SDL (it may run after SDL_Quit). Null = nothing to release.
  std::function<void()> shutdown{};
};

using ShellTitles = std::vector<ShellTitle>;

// Generated (registered_titles.cpp): registers every enabled runtime.
void register_titles(ShellTitles& titles);

// Calls each title's shutdown, last registered first. Every executable that registers real
// titles (the shell, the dump lane) calls it once before it exits, on every normal exit path
// (a fail-fast exit runs no teardown).
inline void shutdown_titles(const ShellTitles& titles) {
  for (auto it = titles.rbegin(); it != titles.rend(); ++it)
    if (it->shutdown) it->shutdown();
}

}  // namespace mpt::shell
