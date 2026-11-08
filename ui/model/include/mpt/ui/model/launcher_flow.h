#pragma once

// Which launcher screen is up, the back stack, and how the launcher follows the
// session: start, pause, resume, quit, and the screen after it ends (ADR 0010 §3-4,
// P1-23, M4-09). Plain C++: no RmlUi, no SDL, no clock. Nothing here reaches game state.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mpt/core/party.h"

namespace mpt::ui {

enum class Screen {
  library,       // installed titles, "Add game dump"
  add_dump,      // how to choose the file: native dialog or the in-UI list
  file_browser,  // the in-UI list (controllers)
  import,        // identify -> build progress -> done or rejected
  seating,       // devices to Player 1-4, then start
  playing,       // no screen: the game runs and the UI only watches for the pause action
  pause,         // the pause overlay: resume, quit to the library, exit
  session_end,   // after a session stops: "Party finished" or the phase it ended in
};

// What the launcher sees of the shell's session (a copy of shell::SessionStatus, so the
// model needs no shell or SDL header). After a stop, phase, turn, max_turns and
// replay_path keep the values the session ended with (M4-06).
struct SessionView {
  bool running = false;
  bool paused = false;
  std::uint64_t start_failures = 0;
  std::string last_error;
  PartyPhase phase = PartyPhase::none;
  std::int32_t turn = 0;       // 1-based, 0 before the party starts
  std::int32_t max_turns = 0;  // 0 if the title does not report it
  std::filesystem::path replay_path;  // the live session's replay; empty if none was written
};

// "Turn 3/20" while a party is running (board, minigame or results, with a turn), else
// empty. Empty when the title does not report max_turns.
std::string turn_text(const SessionView& status);

// A short name for a phase: "menus (no party yet)", "party setup", "board", "minigame",
// "results", "finished".
std::string phase_text(PartyPhase phase);

// The replay path as shown to the user: relative to the user dir, with '/' separators,
// and no other path. A replay outside the user dir shows its file name only. Same rule
// as the shell's .summary.txt (ruling M4-C).
std::string replay_display_path(const std::filesystem::path& replay, const std::filesystem::path& user_dir);

// What the post-session screen says.
struct SessionEnd {
  bool finished = false;  // the Director reported PartyPhase::finished
  std::string heading;    // "Party finished" / "The game session ended"
  std::string detail;     // turns played, or the phase it ended in
  std::string replay;     // "Replay saved: replays/..." or "No replay was saved."
};
SessionEnd describe_session_end(const SessionView& final_status, const std::filesystem::path& user_dir);

class LauncherFlow {
 public:
  Screen screen() const { return screen_; }
  // A UI screen has input focus everywhere except while playing: the shell then does
  // not step the session (ADR 0010 §3.2).
  bool has_focus() const { return screen_ != Screen::playing; }

  // Navigation. go() pushes the current screen; back() pops it. back() does nothing on
  // the library (the back action never exits the app) or while playing, and on the
  // pause screen it is handled by the caller as "resume" (see resume()). On the
  // post-session screen it goes to the library. Returns true if the screen changed.
  void go(Screen next);
  bool back();
  void to_library();

  // Starting a party from the seating screen. The shell applies the request on its next
  // loop iteration, so the launcher waits for the status to change.
  void begin_start(std::uint64_t start_failures_now);
  bool starting() const { return starting_; }
  const std::string& message() const { return message_; }  // why a start failed
  void clear_message() { message_.clear(); }

  // Pause overlay. open_pause() is the host pause action while playing: returns true if
  // the overlay opened (the caller then asks the shell to pause).
  bool open_pause();
  // "Resume" chosen on the pause screen: back to playing. Returns true if the caller
  // should ask the shell to resume. Buttons still held (the confirm press) are masked by
  // the shell's GameInputGate until released (ADR 0010 §3.3), so no wait is needed here.
  bool resume();
  // "Quit to library": the caller asks the shell to stop the session. The post-session
  // screen opens at once and fills in when the stop has been applied.
  void quit_to_library();

  // The post-session screen: true while the stop is still pending; ended() is the
  // session's final status once it has stopped.
  bool ending() const { return stopping_; }
  const std::optional<SessionView>& ended() const { return ended_; }

  // Follows the shell's status once per update.
  void sync(const SessionView& status);

 private:
  Screen screen_ = Screen::library;
  std::vector<Screen> stack_;
  bool starting_ = false;
  std::uint64_t failures_at_start_ = 0;
  bool stopping_ = false;
  std::string message_;
  std::optional<SessionView> ended_;
};

}  // namespace mpt::ui
