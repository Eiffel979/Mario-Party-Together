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
// headless"): exit 0 if the runtime is still running at the end, 1 otherwise. It also prints the
// mean wall time of a stateHash() call (a harness measurement; nothing in the runtime reads a clock).
//
// `--perturb` (MP4, E4 hash coverage) adds a check at every checkpoint: for each memory class the
// hash must cover, flip one byte, require a different stateHash(), flip it back and require the
// original; for free heap space require the hash to stay the same. Any miss makes the exit 1.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "dump_lane.h"
#include "png.h"
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
  bool perturb = false;
  // M2: scripted input on player 1 and frame captures.
  struct Press {
    std::uint32_t buttons;
    std::uint64_t frame;
    std::uint64_t frames;
  };
  std::vector<Press> presses;
  std::filesystem::path capture_dir;  // empty: no captures
  std::uint64_t capture_every = 0;    // 0: at every checkpoint
};

// "start@600", "a@900+10": a button, the first frame it is held, and for how many frames (default 6).
std::optional<BootOptions::Press> parse_press(const std::string& spec) {
  static const std::pair<const char*, std::uint32_t> kNames[] = {
      {"a", mpt::kButtonA},           {"b", mpt::kButtonB},
      {"x", mpt::kButtonX},           {"y", mpt::kButtonY},
      {"l", mpt::kButtonL},           {"r", mpt::kButtonR},
      {"z", mpt::kButtonZ},           {"start", mpt::kButtonStart},
      {"up", mpt::kButtonDpadUp},     {"down", mpt::kButtonDpadDown},
      {"left", mpt::kButtonDpadLeft}, {"right", mpt::kButtonDpadRight},
  };
  const std::size_t at = spec.find('@');
  if (at == std::string::npos) return std::nullopt;
  const std::string name = spec.substr(0, at);
  BootOptions::Press p{0, 0, 6};
  for (const auto& [n, bit] : kNames)
    if (name == n) p.buttons = bit;
  if (p.buttons == 0) return std::nullopt;
  const std::size_t plus = spec.find('+', at);
  p.frame = std::strtoull(spec.substr(at + 1, plus == std::string::npos ? std::string::npos : plus - at - 1).c_str(),
                          nullptr, 10);
  if (plus != std::string::npos) p.frames = std::strtoull(spec.substr(plus + 1).c_str(), nullptr, 10);
  return p;
}

// Player 1's pad for frame f: connected whenever a script is given, holding every press due then.
mpt::PadState scripted_pad(const BootOptions& options, std::uint64_t f) {
  mpt::PadState pad;
  if (options.presses.empty()) return pad;
  pad.connected = 1;
  for (const BootOptions::Press& p : options.presses)
    if (f >= p.frame && f < p.frame + p.frames) pad.buttons |= p.buttons;
  return pad;
}

void print_report(void*, const char* text) { std::fputs(text, stdout); }

#if defined(MPT_DUMP_LANE_MP4)
// E4: what stateHash() covers. Returns the number of failed checks.
int perturb_checks(mpt::mp4::Mp4Runtime& mp4, std::uint64_t frame) {
  struct Target {
    std::uint32_t id;
    const char* name;
    bool must_change;
  };
  static constexpr Target kTargets[] = {
      {MPT_MP4_PERTURB_GAME_DATA, "game data", true},   {MPT_MP4_PERTURB_HEAP_BLOCK, "heap block", true},
      {MPT_MP4_PERTURB_ARAM, "aram in use", true},      {MPT_MP4_PERTURB_GLOBAL, "registered global", true},
      {MPT_MP4_PERTURB_FREE_HEAP, "free heap", false},  {MPT_MP4_PERTURB_STACK, "process stack", false},
  };
  int failed = 0;
  const std::uint64_t original = mp4.stateHash();
  for (const Target& t : kTargets) {
    if (!mp4.debugPerturb(t.id)) {
      std::printf("frame %llu perturb %s: no byte to flip\n", static_cast<unsigned long long>(frame), t.name);
      continue;
    }
    const std::uint64_t flipped = mp4.stateHash();
    mp4.debugPerturb(t.id);
    const std::uint64_t restored = mp4.stateHash();
    const bool changed = flipped != original;
    const bool ok = changed == t.must_change && restored == original;
    if (!ok) ++failed;
    std::printf("frame %llu perturb %s: %s, %s -> %s\n", static_cast<unsigned long long>(frame), t.name,
                changed ? "hash changed" : "hash unchanged", restored == original ? "restored" : "NOT restored",
                ok ? "ok" : "FAIL");
  }
  return failed;
}
#endif

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
  if (mp4 != nullptr) mp4->setDebugHooks(options.perturb);  // debug_perturb needs MPT_MP4_BOOT_DEBUG_HOOKS
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
  int perturb_failures = 0;
  std::uint64_t hash_calls = 0;
  std::chrono::steady_clock::duration hash_time{};
  for (std::uint64_t f = 0; f < options.frames; ++f) {
    input.frame = f;
    input.pads[0] = scripted_pad(options, f);
    runtime->stepFrame(input);
    const mpt::RuntimeHealth health = runtime->status();
    if (health.status != mpt::RuntimeStatus::running) {
      stopped = (health.status == mpt::RuntimeStatus::halted ? "halted: " : "faulted: ") + health.message;
      std::printf("frame %llu %s\n", static_cast<unsigned long long>(f), stopped.c_str());
      break;
    }
    if (options.every != 0 && (f + 1) % options.every == 0) {
      const mpt::FrameOutput video = runtime->video();
      const auto t0 = std::chrono::steady_clock::now();
      const std::uint64_t hash = runtime->stateHash();
      hash_time += std::chrono::steady_clock::now() - t0;
      ++hash_calls;
      std::printf("frame %llu hash %016llx video %ux%u\n", static_cast<unsigned long long>(f + 1),
                  static_cast<unsigned long long>(hash), video.width, video.height);
      const std::uint64_t cap = options.capture_every != 0 ? options.capture_every : options.every;
      if (!options.capture_dir.empty() && (f + 1) % cap == 0 && video.kind == mpt::FrameOutput::Kind::cpu_rgba8) {
        char name[32];
        std::snprintf(name, sizeof name, "frame_%06llu.png", static_cast<unsigned long long>(f + 1));
        if (!mpt::dump_lane::write_png(options.capture_dir / name, video.width, video.height, video.pixels))
          std::printf("dump lane: could not write capture %s\n", name);
      }
#if defined(MPT_DUMP_LANE_MP4)
      if (options.perturb && mp4 != nullptr) perturb_failures += perturb_checks(*mp4, f + 1);
      // Without debug hooks the module must refuse to perturb or give a deep hash, and change nothing
      // (PR #48).
      if (!options.perturb && mp4 != nullptr && hash_calls == 1) {
        const bool refused =
            !mp4->debugPerturb(MPT_MP4_PERTURB_GAME_DATA) && !mp4->debugDeepHash() && mp4->stateHash() == hash;
        std::printf("dump lane: debug hooks off, perturb and deep hash %s\n", refused ? "refused" : "NOT refused");
        if (!refused) ++perturb_failures;
      }
#endif
    }
  }
  if (hash_calls != 0) {
    const double us = std::chrono::duration<double, std::micro>(hash_time).count() / static_cast<double>(hash_calls);
    std::printf("dump lane: stateHash %.0f us per call (mean of %llu)\n", us, static_cast<unsigned long long>(hash_calls));
  }
  if (!options.perturb && perturb_failures != 0) {
    std::puts("dump lane: the module perturbed a game booted without debug hooks");
    return 1;
  }
  if (options.perturb) {
#if defined(MPT_DUMP_LANE_MP4)
    if (mp4 == nullptr) std::puts("dump lane: --perturb needs the MP4 runtime");
    std::printf("dump lane: perturbation checks: %d failed\n", perturb_failures);
    if (mp4 == nullptr || perturb_failures != 0) return 1;
#else
    std::puts("dump lane: --perturb needs the MP4 runtime, which this build does not have");
    return 1;
#endif
  }
  if (!stopped.empty()) return 1;
  std::printf("dump lane: %s still running after %llu frames\n", options.title.c_str(),
              static_cast<unsigned long long>(options.frames));
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  // Unbuffered, so a crash still leaves everything printed before it (the boot mode is a diagnostic).
  std::setvbuf(stdout, nullptr, _IONBF, 0);
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
    } else if (arg == "--perturb") {
      boot_options.perturb = true;
    } else if (arg == "--press" && has_value) {
      // One press or a comma-separated list: start@900,a@1500+10
      const std::string list = argv[++i];
      for (std::size_t from = 0; from <= list.size();) {
        const std::size_t comma = std::min(list.find(',', from), list.size());
        const std::string spec = list.substr(from, comma - from);
        const std::optional<BootOptions::Press> p = parse_press(spec);
        if (!p) {
          std::printf("dump lane: bad --press '%s' (button@frame[+frames], comma-separated)\n", spec.c_str());
          return 2;
        }
        boot_options.presses.push_back(*p);
        from = comma + 1;
      }
    } else if (arg == "--capture" && has_value) {
      boot_options.capture_dir = argv[++i];
    } else if (arg == "--capture-every" && has_value) {
      boot_options.capture_every = std::strtoull(argv[++i], nullptr, 10);
    } else {
      std::printf("usage: mpt_dump_lane [--boot <title> [--frames N] [--every N] [--seed S] [--perturb]\n"
                  "                     [--press button@frame[+frames][,...]]... [--capture DIR [--capture-every N]]]\n");
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
