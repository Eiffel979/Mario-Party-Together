#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "mpt/core/title_id.h"
#include "titles.h"

namespace mpt::shell {

struct AppOptions {
  bool headless = false;                               // dummy video/audio drivers, no frame pacing
  std::optional<std::uint64_t> max_frames;             // exit after this many presented frames
  std::optional<std::filesystem::path> identify_path;  // identify a dump and exit

  std::optional<TitleId> title;                        // --title: start a session of this title
  std::optional<std::filesystem::path> dump;           // --dump: the user's dump for --title
  std::optional<std::uint64_t> seed;                   // --seed: otherwise the shell picks one
  bool widescreen = false;                             // --widescreen (presentation only)
  std::uint32_t internal_scale = 1;                    // --scale N (presentation only)

  std::optional<std::filesystem::path> record;         // --record FILE: write a replay of the live session
  bool embed_save = false;                             // --embed-save: put the initial save image in it
  std::optional<std::filesystem::path> replay;         // --replay FILE: play a replay in the window
  std::optional<std::filesystem::path> verify_replay;  // --verify-replay FILE: headless check, prints the trail

  std::optional<std::filesystem::path> user_dir;       // --user-dir: instead of SDL_GetPrefPath (tests)
  bool stats = false;                                  // --stats: print timing at exit
};

// Parses argv. Returns nullopt and fills `message` when the program should exit right
// away (help, version or a usage error); `exit_code` says how.
std::optional<AppOptions> parse_args(int argc, char** argv, std::string& message, int& exit_code);

// Runs the shell until the window closes, max_frames is reached or a replay ends.
// `titles` comes from register_titles() (or a test registration). Returns the exit code.
int run(const AppOptions& options, const ShellTitles& titles);

}  // namespace mpt::shell
