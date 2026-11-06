// The dump lane's repository guard (tests/dump_lane/path_guard.h): a capture folder, build cache or
// source cache inside the repository is refused, so game material never lands in the working tree.
#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <string>

#include "../dump_lane/path_guard.h"
#include "../support/temp_dir.h"

namespace fs = std::filesystem;
using mpt::dump_lane::GuardedPath;
using mpt::dump_lane::is_inside;
using mpt::dump_lane::refuse_inside;

TEST_CASE("dump lane path guard: what counts as inside the repository") {
  const mpt::test::ScopedTempDir tmp("mpt_path_guard");
  const fs::path repo = tmp.path() / "repo";
  fs::create_directories(repo / "tests");
  fs::create_directories(tmp.path() / "elsewhere");

  CHECK(is_inside(repo, repo));
  CHECK(is_inside(repo / "tests", repo));
  CHECK(is_inside(repo / "captures" / "run1", repo));  // not created yet
  CHECK(is_inside(repo / "tests" / ".." / "out", repo));
  CHECK_FALSE(is_inside(tmp.path() / "elsewhere", repo));
  CHECK_FALSE(is_inside(repo / ".." / "elsewhere", repo));  // leaves through ..
  CHECK_FALSE(is_inside(tmp.path() / "repo2", repo));       // a shared name prefix is not inside
  CHECK_FALSE(is_inside(tmp.path(), repo));                 // the parent
}

TEST_CASE("dump lane path guard: the first path in use inside the repository is refused") {
  const mpt::test::ScopedTempDir tmp("mpt_path_guard");
  const fs::path repo = tmp.path() / "repo";
  fs::create_directories(repo);
  const fs::path outside = tmp.path() / "scratch";

  const std::array<GuardedPath, 3> fine = {{
      {"--capture", outside / "frames"},
      {"MPT_BUILD_CACHE", {}},  // unset: not in use
      {"MPT_SOURCE_CACHE", outside / "sources"},
  }};
  CHECK(refuse_inside(fine, repo).empty());

  const std::array<GuardedPath, 3> bad = {{
      {"--capture", outside / "frames"},
      {"MPT_BUILD_CACHE", repo / "build" / "mpt-build-cache"},
      {"MPT_SOURCE_CACHE", repo / "external"},
  }};
  const std::string message = refuse_inside(bad, repo);
  CHECK(message.find("MPT_BUILD_CACHE") == 0);  // the first offender, by name
  CHECK(message.find("inside the repository") != std::string::npos);
  CHECK(message.find("MPT_SOURCE_CACHE") == std::string::npos);
}
