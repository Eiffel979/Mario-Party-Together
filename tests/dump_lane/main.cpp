// mpt_dump_lane: the manual, local dump lane (P1-20d, ADR 0006, Phase 1 decision D4).
//
// Verifies every fixture under tests/replays/ against the developer's own dump. It exits
// with 77 (CTest SKIP_RETURN_CODE) when MPT_DUMP_DIR is unset, when there is no fixture,
// or when no fixture's title has a runtime yet, so public CI reports the test as skipped.
// See tests/dump_lane/README.md for the local command.
//
// `mpt_dump_lane --boot <title> [--frames N] [--every N] [--seed S]` instead boots the title
// headless on the dump with no input and steps it N frames (default 600), printing the state hash
// every --every frames (default 60) and the game's OSReport text. It is the M1 check ("boots
// headless"): exit 0 if the runtime is still running at the end, 1 otherwise.

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <filesystem>
#include <string>
#include <variant>

#include "dump_lane.h"
#include "mpt/core/title_id.h"
#include "real_titles.h"
#if defined(MPT_DUMP_LANE_MP4)
#include "mpt/mp4/runtime.h"
#endif

#ifndef MPT_REPLAYS_DIR
#error "MPT_REPLAYS_DIR must point at tests/replays"
#endif
#ifndef MPT_REPO_ROOT
#error "MPT_REPO_ROOT must point at the repository root"
#endif
#ifndef MPT_KNOWN_DUMPS_JSON
#error "MPT_KNOWN_DUMPS_JSON must point at asset-import/known_dumps.json"
#endif

namespace {

constexpr int kSkip = 77;

std::string environment(const char* name);

// Runtimes the lane can play: every real title this build registers with the shell.
void register_titles(mpt::TitleRegistry& registry) { mpt::dump_lane::register_real_titles(registry); }

// The VerifiedDump for a title, from the dump in MPT_DUMP_DIR, built through asset-import (the only
// way to make one, ADR 0009 amendment after P1-10). See real_titles.h for MPT_BUILD_CACHE and
// MPT_SOURCE_CACHE.
std::variant<mpt::VerifiedDump, mpt::Result> resolve_dump(mpt::TitleId title, const std::filesystem::path& dump_dir) {
  const std::string cache_env = environment("MPT_BUILD_CACHE");
  const std::filesystem::path cache = cache_env.empty() ? dump_dir / "mpt-build-cache" : std::filesystem::path(cache_env);
  return mpt::dump_lane::build_dump(title, dump_dir, MPT_KNOWN_DUMPS_JSON, cache, environment("MPT_SOURCE_CACHE"));
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

struct BootOptions {
  std::string title;
  std::uint64_t frames = 600;
  std::uint64_t every = 60;
  std::uint64_t seed = 1;
};

void print_report(void*, const char* text) { std::fputs(text, stdout); }

// The M1 check: boot headless on the dump, no input, and step.
int boot(const BootOptions& options, const std::filesystem::path& dump_dir) {
  const std::optional<mpt::TitleId> title = mpt::parse_title_id(options.title);
  if (!title) {
    std::printf("dump lane: unknown title '%s'\n", options.title.c_str());
    return 1;
  }
  mpt::TitleRegistry registry;
  register_titles(registry);
  if (!registry.contains(*title)) {
    std::printf("dump lane: no runtime registered for %s\n", options.title.c_str());
    return 1;
  }
  auto dump = resolve_dump(*title, dump_dir);
  if (mpt::Result* failure = std::get_if<mpt::Result>(&dump)) {
    std::printf("dump lane: %s\n", failure->error().c_str());
    return 1;
  }
  std::unique_ptr<mpt::ITitleRuntime> runtime = registry.createRuntime(*title);
#if defined(MPT_DUMP_LANE_MP4)
  auto* mp4 = dynamic_cast<mpt::mp4::Mp4Runtime*>(runtime.get());
  if (mp4 != nullptr) mp4->setReportSink(&print_report, nullptr);
#endif
  mpt::RuntimeConfig config;
  config.seed = options.seed;
  if (mpt::Result r = runtime->boot(std::get<mpt::VerifiedDump>(dump), config); !r) {
    std::printf("dump lane: boot failed: %s\n", r.error().c_str());
    return 1;
  }
  const mpt::FrameRate rate = runtime->frameRate();
  std::printf("dump lane: booted %s, %u/%u Hz, seed %llu\n", options.title.c_str(), rate.numerator, rate.denominator,
              static_cast<unsigned long long>(options.seed));
  mpt::InputFrame input;
  std::string stopped;
  for (std::uint64_t f = 0; f < options.frames; ++f) {
    input.frame = f;
    runtime->stepFrame(input);
    const mpt::RuntimeHealth health = runtime->status();
    if (health.status != mpt::RuntimeStatus::running) {
      stopped = (health.status == mpt::RuntimeStatus::halted ? "halted: " : "faulted: ") + health.message;
      std::printf("frame %llu %s\n", static_cast<unsigned long long>(f), stopped.c_str());
      break;
    }
    if (options.every != 0 && (f + 1) % options.every == 0) {
      const mpt::FrameOutput video = runtime->video();
      std::printf("frame %llu hash %016llx video %ux%u\n", static_cast<unsigned long long>(f + 1),
                  static_cast<unsigned long long>(runtime->stateHash()), video.width, video.height);
    }
  }
  if (!stopped.empty()) return 1;
  std::printf("dump lane: %s still running after %llu frames\n", options.title.c_str(),
              static_cast<unsigned long long>(options.frames));
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  BootOptions boot_options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--boot" && has_value) {
      boot_options.title = argv[++i];
    } else if (arg == "--frames" && has_value) {
      boot_options.frames = std::strtoull(argv[++i], nullptr, 10);
    } else if (arg == "--every" && has_value) {
      boot_options.every = std::strtoull(argv[++i], nullptr, 10);
    } else if (arg == "--seed" && has_value) {
      boot_options.seed = std::strtoull(argv[++i], nullptr, 10);
    } else {
      std::printf("usage: mpt_dump_lane [--boot <title> [--frames N] [--every N] [--seed S]]\n");
      return 2;
    }
  }
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
  if (!boot_options.title.empty()) return boot(boot_options, dump_dir);

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
