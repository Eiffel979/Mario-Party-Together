#pragma once

// Live-session replays and their summaries (ADR 0006 amendment for M4-06, ruling M4-C).
// Shell-internal, in mpt_shell_core: no SDL calls, no title-specific code.
//
//   <user_dir>/replays/<title>-<seed>-<n>.mptr          a live session's replay
//   <user_dir>/replays/<title>-<seed>-<n>.summary.txt   what that session ended with
//   <user_dir>/replays/media/<sha256>.mptsave           non-blank initial save images,
//                                                       by digest, never embedded
//
// Nothing here writes at session start: the shell writes all three at session stop.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "mpt/core/party.h"
#include "mpt/core/party_director.h"
#include "mpt/core/sha256.h"
#include "mpt/core/title_id.h"

namespace mpt::shell {

// "none", "setup", "board", "minigame", "results" or "finished".
std::string_view phase_name(PartyPhase phase);

// The line the shell prints at every session stop and after --verify-replay:
// "party: phase finished, turn 3/3, 22 events".
std::string party_line(const PartyDirector& director);

// <user_dir>/replays
std::filesystem::path replays_dir(const std::filesystem::path& user_dir);

// <replays>/<title>-<seed>-<n>.mptr with the smallest n >= 1 for which neither that
// file nor its summary exists. Creates nothing.
std::filesystem::path next_replay_path(const std::filesystem::path& replays, TitleId title, std::uint64_t seed);

// The replay's path with its extension replaced by ".summary.txt".
std::filesystem::path summary_path_for(const std::filesystem::path& replay);

// <replays>/media/<64 lowercase hex>.mptsave
std::filesystem::path replay_media_path(const std::filesystem::path& replays, const Sha256Digest& digest);

// The replay path as a summary shows it: relative to `user_dir` with '/' separators, or
// the file name alone when it is not inside `user_dir`. Never another user path.
std::string summary_replay_path(const std::filesystem::path& replay, const std::filesystem::path& user_dir);

struct SessionSummary {
  std::string shell_version;
  std::string runtime_build = "none";  // the build manifest's runtime_build_version
  bool runtime_unpinned = false;
  TitleId title{};
  std::uint64_t seed = 0;
  std::string dump_sha256;
  std::string replay;  // summary_replay_path()
  std::uint64_t frames = 0;
  bool failed = false;  // a step failed (FrameLoop::Tick::failed)
  PartySnapshot party{};
  std::size_t events = 0;
};

// The .summary.txt text: "key value" lines, '\n' line ends, in a fixed order.
std::string format_summary(const SessionSummary& summary);

}  // namespace mpt::shell
