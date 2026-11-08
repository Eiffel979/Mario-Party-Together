#include "session_record.h"

#include <string>
#include <system_error>

namespace mpt::shell {
namespace {

std::string utf8(const std::u8string& text) { return std::string(text.begin(), text.end()); }

bool path_exists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

// Absolute and normalized, without touching the filesystem beyond the current folder.
std::filesystem::path normal_absolute(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::path abs = std::filesystem::absolute(path, ec);
  if (ec) abs = path;
  return abs.lexically_normal();
}

}  // namespace

std::string_view phase_name(PartyPhase phase) {
  switch (phase) {
    case PartyPhase::none: return "none";
    case PartyPhase::setup: return "setup";
    case PartyPhase::board: return "board";
    case PartyPhase::minigame: return "minigame";
    case PartyPhase::results: return "results";
    case PartyPhase::finished: return "finished";
  }
  return "unknown";
}

std::string party_line(const PartyDirector& director) {
  const PartySnapshot& party = director.party();
  return "party: phase " + std::string(phase_name(party.phase)) + ", turn " + std::to_string(party.turn) + "/" +
         std::to_string(party.max_turns) + ", " + std::to_string(director.events().size()) + " events";
}

std::filesystem::path replays_dir(const std::filesystem::path& user_dir) { return user_dir / "replays"; }

std::filesystem::path next_replay_path(const std::filesystem::path& replays, TitleId title, std::uint64_t seed) {
  const std::string stem = std::string(to_string(title)) + "-" + std::to_string(seed) + "-";
  for (std::uint64_t n = 1;; ++n) {
    const std::filesystem::path candidate = replays / (stem + std::to_string(n) + ".mptr");
    if (!path_exists(candidate) && !path_exists(summary_path_for(candidate))) return candidate;
  }
}

std::filesystem::path summary_path_for(const std::filesystem::path& replay) {
  std::filesystem::path summary = replay;
  summary.replace_extension(".summary.txt");
  return summary;
}

std::filesystem::path replay_media_path(const std::filesystem::path& replays, const Sha256Digest& digest) {
  return replays / "media" / (to_hex(digest) + ".mptsave");
}

std::string summary_replay_path(const std::filesystem::path& replay, const std::filesystem::path& user_dir) {
  const std::filesystem::path rel = normal_absolute(replay).lexically_relative(normal_absolute(user_dir));
  const bool inside = !rel.empty() && *rel.begin() != ".." && !rel.is_absolute() && rel != ".";
  return utf8(inside ? rel.generic_u8string() : replay.filename().generic_u8string());
}

std::string format_summary(const SessionSummary& s) {
  std::string out;
  const auto line = [&out](std::string_view key, const std::string& value) {
    out.append(key);
    out.push_back(' ');
    out.append(value);
    out.push_back('\n');
  };
  out += "# Mario Party Together session summary (ADR 0006, amendment for M4-06)\n";
  line("summary_version", "1");
  line("shell_version", s.shell_version);
  line("runtime_build", s.runtime_build.empty() ? "none" : s.runtime_build);
  line("runtime_unpinned", s.runtime_unpinned ? "true" : "false");
  line("title", std::string(to_string(s.title)));
  line("seed", std::to_string(s.seed));
  line("dump_sha256", s.dump_sha256);
  line("replay", s.replay);
  line("frames", std::to_string(s.frames));
  line("end", s.failed ? "failed" : "ok");
  line("phase", std::string(phase_name(s.party.phase)));
  line("turn", std::to_string(s.party.turn) + "/" + std::to_string(s.party.max_turns));
  line("board", s.party.board ? s.party.board->str() : std::string("-"));
  line("events", std::to_string(s.events));
  return out;
}

}  // namespace mpt::shell
