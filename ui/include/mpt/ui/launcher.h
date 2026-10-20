#pragma once

// The player-facing launcher (P1-23, ADR 0010): library and import flow, local seating
// and the pause overlay, built on RmlUi and drawn through the shell's presenter.

#include <filesystem>
#include <memory>

#include "overlay.h"
#include "ui_host.h"

namespace mpt::ui {

struct LauncherOptions {
  // The folder holding screens/ and assets/. Empty: ui/ next to the executable, or
  // else the source tree this build came from (developer builds, Phase 1).
  std::filesystem::path asset_root;
};

// Null if RmlUi or the documents could not be loaded (the reason is printed). Call on
// the main thread, after the window and renderer exist. One launcher at a time.
std::unique_ptr<shell::IOverlay> make_launcher(const shell::UiHost& host, const LauncherOptions& options = {});

}  // namespace mpt::ui
