#pragma once

// Running external tools (git, and later the recipe's compiler) without a shell.
// Arguments are passed as a list and never through a command interpreter, so
// paths with spaces or quotes cannot change what runs. Tests substitute a fake
// runner, so no test needs git, a compiler or the network.

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mpt::asset_import {

struct ProcessResult {
  bool started = false;  // false if the program could not be run at all
  int exit_code = -1;
  std::string output;    // stdout and stderr, interleaved
  bool ok() const { return started && exit_code == 0; }
};

struct ProcessRequest {
  std::vector<std::string> argv;              // argv[0] is the program, found on PATH
  std::filesystem::path cwd;                  // empty: inherit
  std::map<std::string, std::string> env;     // added to (or replacing in) the inherited environment
};

class ProcessRunner {
 public:
  virtual ~ProcessRunner() = default;
  virtual ProcessResult run(const ProcessRequest& request) = 0;
};

// Runs real processes (CreateProcessW on Windows, fork/exec elsewhere).
class SystemProcessRunner final : public ProcessRunner {
 public:
  ProcessResult run(const ProcessRequest& request) override;
};

// Finds an executable on PATH (with PATHEXT on Windows), like a shell would.
std::optional<std::filesystem::path> find_program(std::string_view name);

}  // namespace mpt::asset_import
