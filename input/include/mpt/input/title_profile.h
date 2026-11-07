#pragma once

// Loading a title's installed input profile (input/profiles/<title>/default.json)
// into the Mapper, with its per-minigame overrides.
//
// The shell calls this once when a session starts; Mapper::set_minigame then
// picks the override for the Director's current minigame. Loading never fails:
// a missing, unreadable, oversized or invalid file, or one made for another
// title, gives a warning and the built-in default (builtin_default_profile()).
// It reads at most one small file per search directory and does no other I/O.
//
// Determinism: the profile only decides how local devices become pads. Replays
// record, and netplay sends, the mapped InputFrames, so the profile is not part
// of a replay or of the game state, and a different profile (or a fallback) on
// another machine cannot change what a runtime computes from a recorded run.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mpt/core/title_id.h"
#include "mpt/input/mapper.h"
#include "mpt/input/profile.h"

namespace mpt::input {

// "<search dir>/<title id>/default.json", e.g. "profiles/mp4/default.json".
inline constexpr std::string_view kTitleProfileFileName = "default.json";
// Larger files are rejected unread (shipped profiles are a few KiB).
inline constexpr std::uintmax_t kMaxProfileFileBytes = 1u << 20;

struct TitleProfileLoad {
  Profile profile;               // what was loaded, or builtin_default_profile()
  std::filesystem::path source;  // the file it came from; empty for the built-in default
  std::vector<std::string> warnings;  // one line each, already passed to the warning sink

  bool from_file() const { return !source.empty(); }
};

// Receives each warning line. Must not throw (if it does, the exception is swallowed).
using WarningSink = std::function<void(std::string_view message)>;
// Writes "input: <message>\n" to stderr.
void stderr_warning_sink(std::string_view message);

// The title's profile path inside one search directory.
std::filesystem::path title_profile_path(const std::filesystem::path& search_dir, TitleId title);

// Tries `search_dirs` in order and returns the first file that parses, has no
// "title" or the right one, and whose minigame overrides all belong to `title`.
// Invalid files are skipped with a warning (so a broken user copy does not hide
// the shipped one); a directory without the file is skipped silently. If none
// is usable the result is the built-in default with a warning. Never throws.
TitleProfileLoad find_title_profile(TitleId title, std::span<const std::filesystem::path> search_dirs);

// find_title_profile(), then installs the result on every port of `mapper`
// (replacing any per-port profile) and reports each warning to `warn`. The
// mapper keeps its current minigame, so the matching override applies at once,
// and later set_minigame() calls select the profile's overrides. Never throws.
TitleProfileLoad load_title_profile(Mapper& mapper, TitleId title, std::span<const std::filesystem::path> search_dirs,
                                    const WarningSink& warn = stderr_warning_sink);

}  // namespace mpt::input
