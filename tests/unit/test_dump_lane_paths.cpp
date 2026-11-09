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

TEST_CASE("dump lane build cache: the shell's default, never inside the dump folder unless set explicitly") {
  using mpt::dump_lane::build_cache_root;
  using mpt::dump_lane::refuse_cache_in_dump;
  const mpt::test::ScopedTempDir tmp("mpt_path_guard");
  const fs::path dumps = tmp.path() / "dumps";
  fs::create_directories(dumps);
  const fs::path user_cache = tmp.path() / "user" / "cache";

  // Unset: the default root (the shell's UserDirs cache), and it is fine outside the dump folder.
  const auto by_default = build_cache_root("", user_cache);
  CHECK(by_default.path == user_cache);
  CHECK_FALSE(by_default.from_env);
  CHECK(refuse_cache_in_dump(by_default, dumps).empty());

  // MPT_BUILD_CACHE overrides it.
  const auto overridden = build_cache_root((tmp.path() / "elsewhere").string(), user_cache);
  CHECK(overridden.path == tmp.path() / "elsewhere");
  CHECK(overridden.from_env);

  // A default inside the dump folder (or the dump folder itself) is refused.
  const std::string inside = refuse_cache_in_dump(build_cache_root("", dumps / "mpt" / "cache"), dumps);
  CHECK(inside.find("inside MPT_DUMP_DIR") != std::string::npos);
  CHECK(inside.find("MPT_BUILD_CACHE") != std::string::npos);  // says how to fix it
  CHECK_FALSE(refuse_cache_in_dump(build_cache_root("", dumps), dumps).empty());
  CHECK(refuse_cache_in_dump(build_cache_root("", tmp.path() / "dumps2" / "cache"), dumps).empty());  // prefix only

  // An explicit MPT_BUILD_CACHE there is the user's choice and is allowed.
  CHECK(refuse_cache_in_dump(build_cache_root((dumps / "mpt-build-cache").string(), user_cache), dumps).empty());
}
