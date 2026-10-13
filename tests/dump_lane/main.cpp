// mpt_dump_lane: the manual, local dump lane (P1-20d, ADR 0006, Phase 1 decision D4).
//
// Verifies every fixture under tests/replays/ against the developer's own dump. It exits
// with 77 (CTest SKIP_RETURN_CODE) when MPT_DUMP_DIR is unset, when there is no fixture,
// or when no fixture's title has a runtime yet, so public CI reports the test as skipped.
// See tests/dump_lane/README.md for the local command.

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <variant>

#include "dump_lane.h"
#include "mpt/core/title_id.h"

#ifndef MPT_REPLAYS_DIR
#error "MPT_REPLAYS_DIR must point at tests/replays"
#endif
#ifndef MPT_REPO_ROOT
#error "MPT_REPO_ROOT must point at the repository root"
#endif

namespace {

constexpr int kSkip = 77;

// Runtimes the lane can play. Mp4Runtime registers here in P1-16.
void register_titles(mpt::TitleRegistry& /*registry*/) {}

// The VerifiedDump for a fixture, from the dump in MPT_DUMP_DIR. A VerifiedDump comes
// only from a finished asset-import build (ADR 0009 amendment after P1-10), which needs
// the title's build recipe (P1-15). Until then no title is registered, so this is not
// reached.
std::variant<mpt::VerifiedDump, mpt::Result> resolve_dump(mpt::TitleId title, const std::filesystem::path& /*dump_dir*/) {
  return mpt::Result::failure("no dump resolver for " + std::string(mpt::to_string(title)) + " yet (P1-15, P1-16)");
}

// The value of an environment variable, empty if unset. _dupenv_s on Windows, where
// MSVC deprecates getenv.
std::string environment(const char* name) {
#if defined(_WIN32)
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) return {};
  std::string out(value);
  std::free(value);
  return out;
#else
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string();
#endif
}

bool inside(const std::filesystem::path& path, const std::filesystem::path& root) {
  std::error_code ec;
  const std::filesystem::path rel =
      std::filesystem::weakly_canonical(path, ec).lexically_relative(std::filesystem::weakly_canonical(root, ec));
  return !rel.empty() && *rel.begin() != "..";
}

}  // namespace

int main() {
  const std::string dump_dir_env = environment("MPT_DUMP_DIR");
  if (dump_dir_env.empty()) {
    std::puts("dump lane: MPT_DUMP_DIR is not set, skipping (manual local lane, ADR 0006)");
    return kSkip;
  }
  const std::filesystem::path dump_dir = dump_dir_env;
  if (!std::filesystem::is_directory(dump_dir)) {
    std::printf("dump lane: MPT_DUMP_DIR is not a directory: %s\n", dump_dir.string().c_str());
    return 1;
  }
  if (inside(dump_dir, MPT_REPO_ROOT)) {
    std::puts("dump lane: MPT_DUMP_DIR is inside the repository; dumps must live outside it (ADR 0006)");
    return 1;
  }

  const auto fixtures = mpt::dump_lane::find_fixtures(MPT_REPLAYS_DIR);
  if (fixtures.empty()) {
    std::puts("dump lane: no fixtures under tests/replays/ yet, skipping");
    return kSkip;
  }

  mpt::TitleRegistry registry;
  register_titles(registry);
  int passed = 0;
  int failed = 0;
  int skipped = 0;
  for (const auto& fixture : fixtures) {
    const auto bytes = mpt::dump_lane::read_file(fixture.replay);
    const auto sidecar = mpt::dump_lane::read_file(fixture.trail);
    if (!bytes || !sidecar) {
      std::printf("FAIL %s: cannot read the replay or its .trail.txt sidecar\n", fixture.name.c_str());
      ++failed;
      continue;
    }
    auto decoded = mpt::decode_replay(*bytes);
    if (mpt::Result* failure = std::get_if<mpt::Result>(&decoded)) {
      std::printf("FAIL %s: %s\n", fixture.name.c_str(), failure->error().c_str());
      ++failed;
      continue;
    }
    const mpt::TitleId title = std::get<mpt::Replay>(decoded).header.title;
    if (!registry.contains(title)) {
      std::printf("SKIP %s: no runtime registered for %s yet\n", fixture.name.c_str(), std::string(mpt::to_string(title)).c_str());
      ++skipped;
      continue;
    }
    auto dump = resolve_dump(title, dump_dir);
    if (mpt::Result* failure = std::get_if<mpt::Result>(&dump)) {
      std::printf("FAIL %s: %s\n", fixture.name.c_str(), failure->error().c_str());
      ++failed;
      continue;
    }
    const std::string text(reinterpret_cast<const char*>(sidecar->data()), sidecar->size());
    const mpt::Result result =
        mpt::dump_lane::verify_fixture(*bytes, text, registry, std::get<mpt::VerifiedDump>(dump));
    std::printf("%s %s%s%s\n", result ? "PASS" : "FAIL", fixture.name.c_str(), result ? "" : ": ",
                result ? "" : result.error().c_str());
    ++(result ? passed : failed);
  }
  std::printf("dump lane: %d passed, %d failed, %d skipped\n", passed, failed, skipped);
  if (failed > 0) return 1;
  return passed == 0 ? kSkip : 0;
}
