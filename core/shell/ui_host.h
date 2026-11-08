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
#include "mpt/core/party.h"
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

  // M4-06 (ADR 0006 amendment for M4-06). The Director's party during a session, and as
  // it ended once the session has stopped (running == false). none, 0 and 0 before the
  // first session and while a new one starts.
  PartyPhase phase = PartyPhase::none;
  std::int32_t turn = 0;       // 1-based current turn, 0 before the party starts
  std::int32_t max_turns = 0;  // 0 if the title does not report it
  // A live session's replay: where it is being recorded while running, and where it was
  // written once stopped. Empty for playback sessions, before any session, and when
  // writing the replay or its summary failed. The .summary.txt sits next to it.
  std::filesystem::path replay_path;
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
