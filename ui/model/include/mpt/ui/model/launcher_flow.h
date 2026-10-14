#pragma once

// Which launcher screen is up, the back stack, and how the launcher follows the
// session: start, pause, resume, quit (ADR 0010 §3-4, P1-23). Plain C++: no RmlUi, no
// SDL. Host time is passed in by the caller and is only used to cap the wait for held
// buttons on resume; nothing here reaches game state.

#include <cstdint>
#include <string>
#include <vector>

namespace mpt::ui {

enum class Screen {
  library,       // installed titles, "Add game dump"
  add_dump,      // how to choose the file: native dialog or the in-UI list
  file_browser,  // the in-UI list (controllers)
  import,        // identify -> build progress -> done or rejected
  seating,       // devices to Player 1-4, then start
  playing,       // no screen: the game runs and the UI only watches for the pause action
  pause,         // the pause overlay: resume, quit to the library, exit
};

// What the launcher sees of the shell's session (a copy of shell::SessionStatus, so the
// model needs no shell or SDL header).
struct SessionView {
  bool running = false;
  bool paused = false;
  std::uint64_t start_failures = 0;
  std::string last_error;
};

class LauncherFlow {
 public:
  // Held buttons are waited for at most this long on resume, so a stuck button cannot
  // keep the game paused forever.
  static constexpr std::uint64_t kResumeHoldCapNs = 1'500'000'000;

  Screen screen() const { return screen_; }
  // A UI screen has input focus everywhere except while playing: the shell then does
  // not step the session (ADR 0010 §3.2).
  bool has_focus() const { return screen_ != Screen::playing; }

  // Navigation. go() pushes the current screen; back() pops it. back() does nothing on
  // the library (the back action never exits the app) or while playing, and on the
  // pause screen it means "resume". Returns true if something happened.
  void go(Screen next);
  bool back(std::uint64_t now_ns);
  void to_library();

  // Starting a party from the seating screen. The shell applies the request on its next
  // loop iteration, so the launcher waits for the status to change.
  void begin_start(std::uint64_t start_failures_now);
  bool starting() const { return starting_; }
  const std::string& message() const { return message_; }  // why a start failed, or the session ended
  void clear_message() { message_.clear(); }

  // Pause overlay. open_pause() is the host pause action while playing: returns true if
  // the overlay opened (the caller then asks the shell to pause).
  bool open_pause();
  // "Resume" chosen: waits until no button is held (ADR 0010 §3.3).
  void choose_resume(std::uint64_t now_ns);
  bool resuming() const { return resuming_; }
  // While resuming: true once the caller should ask the shell to resume. The screen is
  // then `playing`.
  bool resume_ready(bool any_input_held, std::uint64_t now_ns);
  // "Quit to library": the caller asks the shell to stop the session.
  void quit_to_library();

  // Follows the shell's status once per update.
  void sync(const SessionView& status);

 private:
  Screen screen_ = Screen::library;
  std::vector<Screen> stack_;
  bool starting_ = false;
  std::uint64_t failures_at_start_ = 0;
  bool stopping_ = false;
  bool resuming_ = false;
  std::uint64_t resume_since_ns_ = 0;
  std::string message_;
};

}  // namespace mpt::ui
