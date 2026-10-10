// mpt_replay_check: validates .mptr files (ADR 0006 amendment, point 6).
//
//   mpt_replay_check [--fixture] <file>...
//
// Exits 0 if every file passes, 1 if any fails (each failure is printed), 2 on usage
// errors. --fixture applies the committed-fixture rule: no embedded save media.

#include <cstdio>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mpt/core/replay.h"

int main(int argc, char** argv) {
  bool fixture = false;
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--fixture") {
      fixture = true;
    } else if (arg.starts_with("--")) {
      std::fprintf(stderr, "unknown option %s\n", argv[i]);
      return 2;
    } else {
      files.emplace_back(arg);
    }
  }
  if (files.empty()) {
    std::fprintf(stderr, "usage: mpt_replay_check [--fixture] <file>...\n");
    return 2;
  }

  int status = 0;
  for (const std::string& path : files) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      std::printf("%s: cannot be read\n", path.c_str());
      status = 1;
      continue;
    }
    const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto bytes = std::as_bytes(std::span(chars.data(), chars.size()));
    const mpt::Result result = fixture ? mpt::validate_replay_fixture(bytes) : mpt::validate_replay(bytes);
    if (!result) {
      std::printf("%s: %s\n", path.c_str(), result.error().c_str());
      status = 1;
    }
  }
  return status;
}
