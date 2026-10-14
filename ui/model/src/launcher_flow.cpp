#include "mpt/ui/model/launcher_flow.h"

namespace mpt::ui {

void LauncherFlow::go(Screen next) {
  if (next == screen_) return;
  stack_.push_back(screen_);
  screen_ = next;
}

bool LauncherFlow::back(std::uint64_t now_ns) {
  if (screen_ == Screen::pause) {
    if (!resuming_) choose_resume(now_ns);
    return true;
  }
  if (screen_ == Screen::playing || starting_ || stack_.empty()) return false;
  screen_ = stack_.back();
  stack_.pop_back();
  return true;
}

void LauncherFlow::to_library() {
  screen_ = Screen::library;
  stack_.clear();
  resuming_ = false;
}

void LauncherFlow::begin_start(std::uint64_t start_failures_now) {
  starting_ = true;
  failures_at_start_ = start_failures_now;
  message_.clear();
}

bool LauncherFlow::open_pause() {
  if (screen_ != Screen::playing) return false;
  screen_ = Screen::pause;
  resuming_ = false;
  return true;
}

void LauncherFlow::choose_resume(std::uint64_t now_ns) {
  if (screen_ != Screen::pause) return;
  resuming_ = true;
  resume_since_ns_ = now_ns;
}

bool LauncherFlow::resume_ready(bool any_input_held, std::uint64_t now_ns) {
  if (!resuming_ || screen_ != Screen::pause) return false;
  if (any_input_held && now_ns - resume_since_ns_ < kResumeHoldCapNs) return false;
  resuming_ = false;
  screen_ = Screen::playing;
  return true;
}

void LauncherFlow::quit_to_library() {
  stopping_ = true;
  to_library();
}

void LauncherFlow::sync(const SessionView& status) {
  if (starting_ && status.start_failures != failures_at_start_) {
    starting_ = false;
    message_ = "The game could not start: " + status.last_error;
    return;
  }
  if (stopping_) {
    // The stop is applied on the shell's next iteration; until then the old session
    // still shows as running.
    if (!status.running) stopping_ = false;
    return;
  }
  if (status.running && screen_ != Screen::playing && screen_ != Screen::pause) {
    // Our start went through, or the shell started a session from the command line.
    starting_ = false;
    screen_ = Screen::playing;
    stack_.clear();
    return;
  }
  if (!status.running && (screen_ == Screen::playing || screen_ == Screen::pause)) {
    to_library();
    message_ = "The game session ended.";
  }
}

}  // namespace mpt::ui
