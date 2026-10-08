#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace mpt::shell {

struct AppOptions {
  bool headless = false;                       // dummy video/audio drivers, no frame pacing
  std::optional<std::uint64_t> max_frames;     // exit after this many frames
  std::optional<std::filesystem::path> identify_path;  // identify a dump and exit
};

// Parses argv. Returns nullopt and fills `message` when the program should exit
// right away (help, version or a usage error); `exit_code` says how.
std::optional<AppOptions> parse_args(int argc, char** argv, std::string& message, int& exit_code);

// Runs the shell until the window closes or max_frames is reached. Returns the process exit code.
int run(const AppOptions& options);

}  // namespace mpt::shell
