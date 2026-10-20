#pragma once

// What the shell hands ui/ when it creates the launcher (ADR 0010 §2.2, Review 1, P1-23).
// Shell-internal, like overlay.h: not a core contract, changed with architect review.
//
// ui/ gets read-only status, the IShellCommands requests, the seating bookkeeping and
// what asset-import needs to identify and build a dump. It never gets a runtime or
// adapter factory, a Session or the seed.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL_video.h>

#include "mpt/asset_import/build.h"
#include "mpt/core/title_id.h"
#include "mpt/input/ports.h"
#include "overlay.h"

namespace mpt::shell {

// The session as the UI may see it. Read once per update, on the main thread.
struct SessionStatus {
  bool running = false;          // a session exists (live or replay)
  bool paused = false;           // the shell's pause flag
  std::uint64_t start_failures = 0;  // counts requestStart calls that failed
  std::string last_error;        // why the last start failed, for the user
};

struct UiHost {
  IShellCommands* commands = nullptr;
  SDL_Window* window = nullptr;          // for the native file dialog and pixel density only
  input::PortAssigner* ports = nullptr;  // local seating; main thread only
  std::vector<TitleId> titles;           // titles this build can run
  // The local build recipe for a title, or null when this build cannot make one. Called
  // on the UI's import worker thread, so it must be thread-safe (the generated
  // registrations are plain factories).
  std::function<std::unique_ptr<asset_import::BuildRecipe>(TitleId)> recipe;
  std::filesystem::path user_dir;     // <user data>: the UI keeps its library list here
  std::filesystem::path cache_dir;    // BuildCache root
  std::filesystem::path known_dumps;  // known_dumps.json next to the executable
  std::function<SessionStatus()> status;
};

// Creates the launcher. The shell calls it once, after the window exists, and only for
// a windowed live run: never for --headless, --replay or --verify-replay (ADR 0010 §3.5).
using OverlayFactory = std::function<std::unique_ptr<IOverlay>(const UiHost&)>;

}  // namespace mpt::shell
