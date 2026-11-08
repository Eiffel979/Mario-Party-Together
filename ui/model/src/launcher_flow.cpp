#include "mpt/ui/model/launcher_flow.h"

#include <system_error>

namespace mpt::ui {

namespace fs = std::filesystem;

namespace {

fs::path normal_absolute(const fs::path& path) {
  std::error_code ec;
  fs::path abs = fs::absolute(path, ec);
  if (ec) abs = path;
  return abs.lexically_normal();
}

std::string utf8(const std::u8string& s) { return std::string(s.begin(), s.end()); }

bool in_party(PartyPhase phase) {
  return phase == PartyPhase::board || phase == PartyPhase::minigame || phase == PartyPhase::results;
}

}  // namespace

std::string turn_text(const SessionView& status) {
  if (status.max_turns <= 0 || status.turn <= 0 || !in_party(status.phase)) return {};
  return "Turn " + std::to_string(status.turn) + "/" + std::to_string(status.max_turns);
}

std::string phase_text(PartyPhase phase) {
  switch (phase) {
    case PartyPhase::none: return "menus (no party yet)";
    case PartyPhase::setup: return "party setup";
    case PartyPhase::board: return "board";
    case PartyPhase::minigame: return "minigame";
    case PartyPhase::results: return "results";
    case PartyPhase::finished: return "finished";
  }
  return "unknown";
}

std::string replay_display_path(const fs::path& replay, const fs::path& user_dir) {
  if (replay.empty()) return {};
  const fs::path rel = normal_absolute(replay).lexically_relative(normal_absolute(user_dir));
  const bool inside = !user_dir.empty() && !rel.empty() && *rel.begin() != ".." && !rel.is_absolute() && rel != ".";
  return utf8(inside ? rel.generic_u8string() : replay.filename().generic_u8string());
}

SessionEnd describe_session_end(const SessionView& s, const fs::path& user_dir) {
  SessionEnd end;
  end.finished = s.phase == PartyPhase::finished;
  if (end.finished) {
    end.heading = "Party finished";
    if (s.turn > 0) {
      end.detail = s.max_turns > 0 ? "Turns played: " + std::to_string(s.turn) + " of " + std::to_string(s.max_turns) + "."
                                   : "Turns played: " + std::to_string(s.turn) + ".";
    }
  } else {
    end.heading = "The game session ended";
    end.detail = "It ended in this phase: " + phase_text(s.phase);
    if (s.turn > 0) {
      end.detail += s.max_turns > 0 ? " (turn " + std::to_string(s.turn) + " of " + std::to_string(s.max_turns) + ")"
                                    : " (turn " + std::to_string(s.turn) + ")";
    }
    end.detail += ".";
  }
  const std::string replay = replay_display_path(s.replay_path, user_dir);
  end.replay = replay.empty() ? std::string("No replay was saved.")
                              : "Replay saved in your user folder: " + replay;
  return end;
}

void LauncherFlow::go(Screen next) {
  if (next == screen_) return;
  stack_.push_back(screen_);
  screen_ = next;
}

bool LauncherFlow::back() {
  if (screen_ == Screen::session_end) {
    to_library();
    return true;
  }
  if (screen_ == Screen::pause || screen_ == Screen::playing || starting_ || stack_.empty()) return false;
  screen_ = stack_.back();
  stack_.pop_back();
  return true;
}

void LauncherFlow::to_library() {
  screen_ = Screen::library;
  stack_.clear();
}

void LauncherFlow::begin_start(std::uint64_t start_failures_now) {
  starting_ = true;
  failures_at_start_ = start_failures_now;
  message_.clear();
}

bool LauncherFlow::open_pause() {
  if (screen_ != Screen::playing) return false;
  screen_ = Screen::pause;
  return true;
}

bool LauncherFlow::resume() {
  if (screen_ != Screen::pause) return false;
  screen_ = Screen::playing;
  return true;
}

void LauncherFlow::quit_to_library() {
  stopping_ = true;
  ended_.reset();
  message_.clear();
  screen_ = Screen::session_end;
  stack_.clear();
}

void LauncherFlow::sync(const SessionView& status) {
  if (starting_ && status.start_failures != failures_at_start_) {
    starting_ = false;
    message_ = "The game could not start: " + status.last_error;
    return;
  }
  if (stopping_) {
    // The stop is applied on the shell's next iteration; until then the old session
    // still shows as running. Once stopped, the status keeps the final values.
    if (!status.running) {
      stopping_ = false;
      ended_ = status;
    }
    return;
  }
  if (status.running && screen_ != Screen::playing && screen_ != Screen::pause) {
    // Our start went through, or the shell started a session from the command line.
    starting_ = false;
    ended_.reset();
    screen_ = Screen::playing;
    stack_.clear();
    return;
  }
  if (!status.running && (screen_ == Screen::playing || screen_ == Screen::pause)) {
    // The session ended by itself.
    ended_ = status;
    screen_ = Screen::session_end;
    stack_.clear();
  }
}

}  // namespace mpt::ui
