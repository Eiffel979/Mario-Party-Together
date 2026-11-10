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
// original; for free heap space and stack contents require every hash to stay the same. The deep
// hash (ABI v1.3) is the oracle: a flip it sees must change stateHash() too. `--perturb-samples N`
// (default 32) also flips N sampled bytes per checkpoint across everything the deep hash covers.
// Any miss makes the exit 1. `--hash-items FILE` writes state_hash's parts with their digests at the
// last checkpoint (ABI v1.3 debug_hash_items), for diffing a Windows run against a Linux one.
//
// `--press button@frame[+frames][,...]` holds buttons on player 1, and `--capture DIR
// [--capture-every N]` writes checkpoint frames as PNG files to DIR. DIR, the --hash-items file,
// MPT_BUILD_CACHE and MPT_SOURCE_CACHE must lie outside the repository, like MPT_DUMP_DIR
// (path_guard.h). The build cache defaults to the shell's (its user cache folder), never MPT_DUMP_DIR.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "dump_lane.h"
#include "crash_report.h"
#include "path_guard.h"
#include "png.h"
#include "supervise.h"
#include "mpt/core/title_id.h"
#include "real_titles.h"
#include "watch.h"
#include "user_dirs.h"
#if defined(MPT_DUMP_LANE_MP4)
#include "../conformance/conformance.h"
#include "mpt/mp4/adapter.h"
#include "mpt/mp4/runtime.h"
#endif

#ifndef MPT_REPLAYS_DIR
#error "MPT_REPLAYS_DIR must point at tests/replays"
#endif
#ifndef MPT_REPO_ROOT
#error "MPT_REPO_ROOT must point at the repository root"
#endif
#ifndef MPT_DUMP_LANE_LAYOUT_PROBE
#define MPT_DUMP_LANE_LAYOUT_PROBE "none"  // no MP4 runtime in this build
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
// way to make one, ADR 0009 amendment after P1-10) into `cache`, the BuildCache root main() chose.
// See real_titles.h for MPT_BUILD_CACHE and MPT_SOURCE_CACHE.
std::variant<mpt::VerifiedDump, mpt::Result> resolve_dump(mpt::TitleId title, const std::filesystem::path& dump_dir,
                                                          const std::filesystem::path& cache) {
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

struct BootOptions {
  std::string title;
  std::uint64_t frames = 600;
  std::uint64_t every = 60;
  std::uint64_t seed = 1;
  bool perturb = false;
  std::uint32_t perturb_samples = 32;  // sampled bytes per checkpoint with --perturb
  std::filesystem::path hash_items;    // --hash-items FILE: state_hash's parts at the last checkpoint
  bool note_module_hits = false;       // --note-module-hits: with --hash-items, a note per module hit
  bool unlock_bowser = false;          // --party-unlock-bowser: Bowser's Gnarly Party on the wheel (diagnosis)
  bool frame_times = false;            // --frame-times: with --party, steady-state frame times
  bool conformance = false;            // --conformance: the runtime conformance suite instead of a boot run
  std::uint64_t save_load_diff = 0;    // --save-load-diff N: where a resumed run first differs, by hash item
  // M2: scripted input on player 1 and frame captures.
  struct Press {
    std::uint32_t buttons;
    std::uint64_t frame;
    std::uint64_t frames;
  };
  std::vector<Press> presses;
  std::filesystem::path capture_dir;  // empty: no captures
  std::uint64_t capture_every = 0;    // 0: at every checkpoint
  // M4: --party (the CPU-party autopilot) and --verify-replay FILE.
  bool party = false;
  mpt::dump_lane::PartyScript script{};
  std::uint64_t watchdog = 36'000;     // --watchdog N: frames with no party change before failing
  std::filesystem::path replay_out;    // --replay FILE: where --party writes its .mptr
  std::filesystem::path verify_replay; // --verify-replay FILE
  // Diagnosis: --party-save-at N FILE writes the state before frame N; --party-resume FILE goes on from it.
  std::uint64_t party_save_at = 0;
  std::filesystem::path party_save;
  std::filesystem::path party_resume;
  // Diagnosis (watch.h): --watch-word, --find-word and --watch-write read arena words in place.
  mpt::dump_lane::watch::Options watch;
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
// E4: what stateHash() covers, with the deep hash as the oracle (ADR 0008 amendment after P1-16 hash
// coverage). For each flip: a byte the deep hash sees must change stateHash() too, or the manifest
// misses an area; a byte neither may see (free heap, stack contents) must change neither; flipping
// back must restore both. Returns the number of failed checks.
struct Hashes {
  std::uint64_t state, deep;
  bool operator==(const Hashes&) const = default;
};

Hashes hashes(mpt::mp4::Mp4Runtime& mp4) {
  const std::optional<std::uint64_t> deep = mp4.debugDeepHash();
  return {mp4.stateHash(), deep.value_or(0)};
}

// One flip of `target`: "ok", or why it failed. `skipped` when the class has no byte right now.
const char* perturb_once(mpt::mp4::Mp4Runtime& mp4, std::uint32_t target, bool must_change, const Hashes& original,
                         bool& skipped) {
  skipped = false;
  if (!mp4.debugPerturb(target)) {
    skipped = true;
    return "no byte to flip";
  }
  const Hashes flipped = hashes(mp4);
  mp4.debugPerturb(target);
  const Hashes restored = hashes(mp4);
  if (restored != original) return "FAIL: not restored";
  const bool state_changed = flipped.state != original.state;
  const bool deep_changed = flipped.deep != original.deep;
  if (must_change) {
    if (!deep_changed) return "FAIL: the deep hash does not cover it";
    if (!state_changed) return "FAIL: the deep hash changed but stateHash did not (manifest misses an area)";
    return "ok";
  }
  if (state_changed || deep_changed) return "FAIL: a hash changed for a byte no hash may cover";
  return "ok";
}

int perturb_checks(mpt::mp4::Mp4Runtime& mp4, std::uint64_t frame, std::uint64_t checkpoint, std::uint32_t samples) {
  struct Target {
    std::uint32_t id;
    const char* name;
    bool must_change;
  };
  static constexpr Target kTargets[] = {
      {MPT_MP4_PERTURB_GAME_DATA, "game data", true},   {MPT_MP4_PERTURB_HEAP_BLOCK, "heap block", true},
      {MPT_MP4_PERTURB_ARAM, "aram in use", true},      {MPT_MP4_PERTURB_GLOBAL, "registered global", true},
      {MPT_MP4_PERTURB_FREE_HEAP, "free heap", false},  {MPT_MP4_PERTURB_STACK, "process stack", false},
      {MPT_MP4_PERTURB_ARMEM, "armem block", true},     {MPT_MP4_PERTURB_FREE_ARAM, "free aram", false},
  };
  int failed = 0;
  const Hashes original = hashes(mp4);
  const auto f = static_cast<unsigned long long>(frame);
  for (const Target& t : kTargets) {
    bool skipped = false;
    const char* result = perturb_once(mp4, t.id, t.must_change, original, skipped);
    if (!skipped && result[0] == 'F') ++failed;
    std::printf("frame %llu perturb %s: %s\n", f, t.name, result);
  }
  // Sampled bytes across everything the deep hash covers (ABI v1.3 MPT_MP4_PERTURB_SAMPLE).
  int sampled = 0, sample_failed = 0;
  for (std::uint32_t i = 0; i < samples; ++i) {
    const std::uint32_t k = static_cast<std::uint32_t>(checkpoint * samples + i);
    bool skipped = false;
    const char* result = perturb_once(mp4, MPT_MP4_PERTURB_SAMPLE | (k << 8), true, original, skipped);
    if (skipped) continue;
    ++sampled;
    if (result[0] == 'F') {
      ++sample_failed;
      std::printf("frame %llu perturb sample %u: %s\n", f, k, result);
    }
  }
  if (samples != 0) std::printf("frame %llu perturb samples: %d of %d ok\n", f, sampled - sample_failed, sampled);
  return failed + sample_failed;
}
#endif

#if defined(MPT_DUMP_LANE_MP4)
// `--conformance` (P1-20c on the dump lane): every conformance check (tests/conformance/conformance.h)
// against Mp4Runtime and Mp4Adapter on the real game, driven by the --press script, then the E4
// hash-coverage check against the deep hash on a runtime booted with debug hooks. Prints each problem;
// returns the number found.
int conformance_run(const BootOptions& options, const mpt::VerifiedDump& dump, mpt::TitleRegistry& registry) {
  using namespace mpt::conformance;
  Target t;
  t.name = "Mp4Runtime";
  t.runtime = [&registry] { return registry.createRuntime(mpt::TitleId::mp4); };
  t.adapter = [](mpt::ITitleRuntime& runtime) -> std::unique_ptr<mpt::ITitleAdapter> {
    auto* mp4 = dynamic_cast<mpt::mp4::Mp4Runtime*>(&runtime);
    if (mp4 == nullptr) return nullptr;
    return std::make_unique<mpt::mp4::Mp4Adapter>(*mp4);
  };
  t.dump = dump;
  t.config.seed = options.seed;
  t.inputs = [&options](std::uint64_t f) {
    mpt::InputFrame in;
    in.pads[0] = scripted_pad(options, f);
    return in;
  };
  t.frames = options.frames;
  t.interval = options.every != 0 ? options.every : 60;
  t.save_frame = options.frames / 2;
  struct Check {
    const char* name;
    Problems (*fn)(const Target&);
  };
  static constexpr Check kChecks[] = {
      {"frame rate validity", &check_frame_rate},     {"frame numbering and determinism", &check_frame_numbering},
      {"save state round trip", &check_save_load},    {"video and audio views", &check_views},
      {"presentation options", &check_presentation_options}, {"save media", &check_save_media},
      {"stick folding", &check_stick_folding},        {"adapter snapshots", &check_adapter},
      {"runtime status", &check_status},
  };
  int found = 0;
  for (const Check& c : kChecks) {
    const Problems problems = c.fn(t);
    std::printf("conformance %s: %s\n", c.name, problems.empty() ? "ok" : "FAIL");
    for (const std::string& p : problems) std::printf("  %s\n", p.c_str());
    found += static_cast<int>(problems.size());
  }

  // E4 against the deep hash, at the end of the script, on a runtime with debug hooks.
  std::unique_ptr<mpt::ITitleRuntime> runtime = registry.createRuntime(mpt::TitleId::mp4);
  auto* mp4 = dynamic_cast<mpt::mp4::Mp4Runtime*>(runtime.get());
  if (mp4 == nullptr) {
    std::puts("conformance hash coverage: FAIL (not an Mp4Runtime)");
    return found + 1;
  }
  mp4->setDebugHooks(true);
  if (mpt::Result r = runtime->boot(dump, t.config); !r) {
    std::printf("conformance hash coverage: FAIL (boot: %s)\n", r.error().c_str());
    return found + 1;
  }
  for (std::uint64_t f = 0; f < t.frames; ++f) runtime->stepFrame(detail::input_at(t.inputs, f));
  HashCoverageProbe probe;
  probe.state_hash = [mp4] { return mp4->stateHash(); };
  probe.deep_hash = [mp4] { return mp4->debugDeepHash().value_or(0); };
  probe.perturb = [mp4](std::uint32_t target) { return mp4->debugPerturb(target); };
  probe.targets = {{MPT_MP4_PERTURB_GAME_DATA, "game data"},   {MPT_MP4_PERTURB_HEAP_BLOCK, "heap block"},
                   {MPT_MP4_PERTURB_ARAM, "aram in use"},      {MPT_MP4_PERTURB_GLOBAL, "registered global"},
                   {MPT_MP4_PERTURB_FREE_HEAP, "free heap"},   {MPT_MP4_PERTURB_STACK, "process stack"},
                   {MPT_MP4_PERTURB_ARMEM, "armem block"},     {MPT_MP4_PERTURB_FREE_ARAM, "free aram"}};
  for (std::uint32_t k = 0; k < 64; ++k)
    probe.targets.push_back({MPT_MP4_PERTURB_SAMPLE | (k << 8), "sample " + std::to_string(k)});
  const HashCoverageResult coverage = check_hash_coverage(probe);
  std::printf("conformance hash coverage: %s (%zu of %zu targets had a byte to flip)\n",
              coverage.problems.empty() && coverage.checked >= 60 ? "ok" : "FAIL", coverage.checked,
              probe.targets.size());
  for (const std::string& p : coverage.problems) std::printf("  %s\n", p.c_str());
  found += static_cast<int>(coverage.problems.size()) + (coverage.checked >= 60 ? 0 : 1);
  std::printf("dump lane: conformance: %d problem(s)\n", found);
  return found;
}
#endif

#if defined(MPT_DUMP_LANE_MP4)
// `--save-load-diff N` (E5 diagnosis): save at frame N, run on to every checkpoint, then load the
// state into a fresh runtime booted with another seed and run the same frames. At the first checkpoint
// where stateHash() differs, prints the debug_hash_items lines that differ: the symbol or range whose
// state the save state does not carry, or carries wrongly.
int save_load_diff(const BootOptions& options, const mpt::VerifiedDump& dump, mpt::TitleRegistry& registry) {
  const std::uint64_t at = options.save_load_diff;
  const std::uint64_t every = options.every != 0 ? options.every : 60;
  auto input = [&options](std::uint64_t f) {
    mpt::InputFrame in;
    in.frame = f;
    in.pads[0] = scripted_pad(options, f);
    return in;
  };
  auto make = [&](std::uint64_t seed) {
    std::unique_ptr<mpt::ITitleRuntime> runtime = registry.createRuntime(mpt::TitleId::mp4);
    if (auto* mp4 = dynamic_cast<mpt::mp4::Mp4Runtime*>(runtime.get())) mp4->setDebugHooks(true);
    mpt::RuntimeConfig config;
    config.seed = seed;
    if (mpt::Result r = runtime->boot(dump, config); !r) {
      std::printf("dump lane: boot failed: %s\n", r.error().c_str());
      runtime.reset();
    }
    return runtime;
  };
  std::vector<std::pair<std::uint64_t, std::string>> straight;  // (frame, items) at each checkpoint
  mpt::SaveState saved;
  {
    std::unique_ptr<mpt::ITitleRuntime> producer = make(options.seed);
    if (!producer) return 1;
    auto* mp4 = dynamic_cast<mpt::mp4::Mp4Runtime*>(producer.get());
    for (std::uint64_t f = 0; f < options.frames; ++f) {
      if (f == at) saved = producer->saveState();
      producer->stepFrame(input(f));
      if (f >= at && (f + 1) % every == 0) straight.emplace_back(f + 1, mp4->debugHashItems());
    }
  }
  std::unique_ptr<mpt::ITitleRuntime> consumer = make(options.seed + 1);
  if (!consumer) return 1;
  if (mpt::Result r = consumer->loadState(saved); !r) {
    std::printf("dump lane: loadState failed: %s\n", r.error().c_str());
    return 1;
  }
  auto* mp4 = dynamic_cast<mpt::mp4::Mp4Runtime*>(consumer.get());
  std::size_t next = 0;
  for (std::uint64_t f = at; f < options.frames && next < straight.size(); ++f) {
    consumer->stepFrame(input(f));
    if ((f + 1) != straight[next].first) continue;
    const std::string resumed = mp4->debugHashItems();
    if (resumed == straight[next].second) {
      ++next;
      continue;
    }
    std::printf("dump lane: the resumed run differs at frame %llu; items that differ (straight | resumed):\n",
                static_cast<unsigned long long>(f + 1));
    auto lines = [](const std::string& text) {
      std::vector<std::string> out;
      for (std::size_t from = 0; from < text.size();) {
        const std::size_t nl = text.find('\n', from);
        out.push_back(text.substr(from, nl - from));
        from = nl == std::string::npos ? text.size() : nl + 1;
      }
      return out;
    };
    const std::vector<std::string> a = lines(straight[next].second), b = lines(resumed);
    int shown = 0;
    for (std::size_t i = 0; i < std::max(a.size(), b.size()) && shown < 40; ++i) {
      const std::string x = i < a.size() ? a[i] : "-", y = i < b.size() ? b[i] : "-";
      if (x != y) {
        std::printf("  %s | %s\n", x.c_str(), y.c_str());
        ++shown;
      }
    }
    return 1;
  }
  std::printf("dump lane: the resumed run matches at all %zu checkpoints\n", straight.size());
  return 0;
}

#endif

#if defined(MPT_DUMP_LANE_MP4)
const char* phase_name(mpt::PartyPhase phase) {
  static const char* const kPhases[] = {"none", "setup", "board", "minigame", "results", "finished"};
  const auto i = static_cast<std::size_t>(phase);
  return i < std::size(kPhases) ? kPhases[i] : "?";
}

const char* event_name(mpt::PartyEvent::Kind kind) {
  switch (kind) {
    case mpt::PartyEvent::Kind::phase_changed: return "phase_changed";
    case mpt::PartyEvent::Kind::turn_started: return "turn_started";
    case mpt::PartyEvent::Kind::minigame_started: return "minigame_started";
    case mpt::PartyEvent::Kind::minigame_ended: return "minigame_ended";
  }
  return "?";
}

void print_event(const mpt::PartyEvent& e) {
  const mpt::PartySnapshot& s = e.snapshot;
  std::printf("event %llu %s phase %s turn %d/%d", static_cast<unsigned long long>(e.frame), event_name(e.kind),
              phase_name(s.phase), s.turn, s.max_turns);
  if (e.minigame) std::printf(" minigame %s", e.minigame->str().c_str());
  if (e.kind == mpt::PartyEvent::Kind::phase_changed && s.phase == mpt::PartyPhase::finished) {
    for (std::size_t i = 0; i < mpt::kMaxPlayers; ++i)
      std::printf(" p%zu %s %d stars %d coins", i + 1, s.players[i].is_cpu ? "com" : "human", s.players[i].stars,
                  s.players[i].coins);
  }
  std::printf("\n");
}

// The running overlay's name, from the module's party read (presentation only: the oracle is the
// Director).
std::string overlay_of(mpt::ITitleRuntime& runtime) {
  auto* mp4 = dynamic_cast<mpt::mp4::Mp4Runtime*>(&runtime);
  MptMp4Party raw{};
  if (mp4 == nullptr || !mp4->readParty(raw)) return {};
  return raw.overlay_name;
}

bool write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

// `--party --hash-items FILE`: debug_hash_items at every checkpoint. FILE holds the last checkpoint's
// list, as in the boot mode. Over the whole run, each "count <rule>" is summed and its largest value
// kept, and every distinct "note" line is kept with the number of checkpoints it appeared at, written
// to FILE.notes. The notes name game symbols: both files stay in the directory given.
struct HashItemTally {
  std::uint64_t checkpoints = 0;
  std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> counts;  // rule -> (total, max)
  std::map<std::string, std::uint64_t> notes;                             // note -> checkpoints

  bool take(const std::filesystem::path& file, const std::string& items) {
    if (items.empty()) return false;
    ++checkpoints;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(items.data(), static_cast<std::streamsize>(items.size()));
    // Lines are "name size digest"; a name may hold spaces, so size and digest are the last two fields.
    for (std::size_t from = 0; from < items.size();) {
      const std::size_t eol = std::min(items.find('\n', from), items.size());
      const std::string line = items.substr(from, eol - from);
      from = eol + 1;
      const std::size_t d = line.rfind(' ');
      const std::size_t z = d == std::string::npos ? std::string::npos : line.rfind(' ', d - 1);
      if (z == std::string::npos) continue;
      const std::string name = line.substr(0, z);
      if (name.rfind("count ", 0) == 0) {
        const std::uint64_t n = std::strtoull(line.c_str() + z + 1, nullptr, 10);
        auto& [total, max] = counts[name.substr(6)];
        total += n;
        max = std::max(max, n);
      } else if (name.rfind("note ", 0) == 0) {
        ++notes[name.substr(5)];
      }
    }
    return static_cast<bool>(out);
  }

  bool finish(const std::filesystem::path& file) const {
    std::printf("dump lane: hash items at %llu checkpoints\n", static_cast<unsigned long long>(checkpoints));
    for (const auto& [rule, v] : counts)
      std::printf("dump lane: hash items count %s total %llu max %llu\n", rule.c_str(),
                  static_cast<unsigned long long>(v.first), static_cast<unsigned long long>(v.second));
    std::printf("dump lane: hash items %zu distinct notes\n", notes.size());
    std::filesystem::path path = file;
    path += ".notes";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const auto& [note, n] : notes) out << n << ' ' << note << '\n';
    return static_cast<bool>(out);
  }
};

// `--party --frame-times` (ADR 0008, "Stale heap bytes", point 5): how long each step takes, for the
// cost of a change measured against another build. Exempt frames are chosen by game-visible events
// only, so two builds exempt exactly the same frames: the boot frames until the first frame of
// bootDll, any frame in which the overlay changes, and any frame in which the SDK completes a DVD
// read or an ARAM transfer (ABI v1.4 memory_use's I/O counts, read after the step). Prints the total
// step time over every frame and the p99 over the rest.
struct FrameClock {
  std::vector<double> steady_ms;
  double total_ms = 0;
  std::uint64_t frames = 0, exempt = 0;
  bool booted = false;
  std::string overlay;
  std::uint64_t dvd = 0, aram = 0;

  void step(std::chrono::steady_clock::duration d, mpt::mp4::Mp4Runtime* mp4) {
    const double ms = std::chrono::duration<double, std::milli>(d).count();
    total_ms += ms;
    ++frames;
    MptMp4Party raw{};
    raw.struct_size = sizeof raw;
    std::uint64_t dvd_now = dvd, aram_now = aram;
    if (mp4 != nullptr) {
      mp4->readParty(raw);
      if (const std::optional<MptMp4MemoryUse> use = mp4->memoryUse()) {
        dvd_now = use->dvd_reads;
        aram_now = use->aram_transfers;
      }
    }
    const std::string now = raw.overlay_name;
    if (now == "bootDll") booted = true;
    const bool is_exempt = !booted || now != overlay || dvd_now != dvd || aram_now != aram;
    overlay = now;
    dvd = dvd_now;
    aram = aram_now;
    if (is_exempt) {
      ++exempt;
    } else {
      steady_ms.push_back(ms);
    }
  }

  void print() {
    double p99 = 0;
    if (!steady_ms.empty()) {
      const std::size_t k = (steady_ms.size() * 99) / 100;
      std::nth_element(steady_ms.begin(), steady_ms.begin() + static_cast<std::ptrdiff_t>(k), steady_ms.end());
      p99 = steady_ms[k];
    }
    std::printf("dump lane: frame times: %llu frames, total step time %.1f s, p99 %.3f ms over %zu steady frames "
                "(%llu exempt)\n",
                static_cast<unsigned long long>(frames), total_ms / 1000.0, p99, steady_ms.size(),
                static_cast<unsigned long long>(exempt));
  }
};

// `--party` (M4-01): a whole party on the autopilot, through a Session, recorded as an .mptr with blank
// media. The oracle is Session::director(): the run passes once its phase is `finished`. It fails on
// a halt or fault, on a soft lock (no change of phase, overlay or turn for --watchdog frames) or when
// --frames runs out first.
int party_run(const BootOptions& options, const mpt::VerifiedDump& dump, mpt::TitleRegistry& registry) {
  mpt::SessionConfig config;
  config.dump = dump;
  config.runtime.seed = options.seed;
  const std::uint32_t interval = options.every != 0 ? static_cast<std::uint32_t>(options.every) : 60;
  auto header = mpt::make_replay_header(config, /*embed_save_media=*/false, interval);
  if (mpt::Result* failure = std::get_if<mpt::Result>(&header)) {
    std::printf("dump lane: replay header: %s\n", failure->error().c_str());
    return 1;
  }
  // --hash-items needs MPT_MP4_BOOT_DEBUG_HOOKS, which is set before boot: the Session creates the
  // runtime, so it gets a registry whose MP4 runtime has the hooks on. They are never hashed, so the
  // trail is the same as without them.
  mpt::TitleRegistry hooked;
  const bool items_wanted = !options.hash_items.empty();
  // --party-unlock-bowser (the M4-03 sweep): the sixth board needs a gameplay boot flag, which no replay
  // records, so such a run is diagnosis only.
  const bool hooked_wanted = items_wanted || options.unlock_bowser;
  if (options.unlock_bowser && !options.replay_out.empty()) {
    std::printf("dump lane: --party-unlock-bowser is diagnosis only; a replay cannot record the unlock\n");
    return 2;
  }
  if (hooked_wanted) {
    const mpt::Result added = hooked.add(
        mpt::TitleId::mp4,
        [&registry, &options]() {
          std::unique_ptr<mpt::ITitleRuntime> r = registry.createRuntime(mpt::TitleId::mp4);
          if (auto* m = dynamic_cast<mpt::mp4::Mp4Runtime*>(r.get())) {
            m->setDebugHooks(!options.hash_items.empty());
            m->setNoteModuleHits(options.note_module_hits);
            if (options.unlock_bowser) m->setTestGameplayFlags(MPT_MP4_BOOT_UNLOCK_BOWSERS_GNARLY_PARTY);
          }
          return r;
        },
        [&registry](mpt::ITitleRuntime& r) { return registry.createAdapter(mpt::TitleId::mp4, r); });
    if (!added) {
      std::printf("dump lane: %s\n", added.error().c_str());
      return 1;
    }
  }
  auto started = mpt::Session::start(hooked_wanted ? hooked : registry, config);
  if (mpt::Result* failure = std::get_if<mpt::Result>(&started)) {
    std::printf("dump lane: session: %s\n", failure->error().c_str());
    return 1;
  }
  std::unique_ptr<mpt::Session>& session = std::get<std::unique_ptr<mpt::Session>>(started);
  auto* mp4 = dynamic_cast<mpt::mp4::Mp4Runtime*>(&session->runtime());
  if (mp4 != nullptr) mp4->setReportSink(&print_report, nullptr);
  HashItemTally tally;
  FrameClock frame_clock;
  std::printf("dump lane: party autopilot, seed %llu, board %d, watchdog %llu frames\n",
              static_cast<unsigned long long>(options.seed), options.script.board,
              static_cast<unsigned long long>(options.watchdog));

  mpt::ReplayRecorder recorder(std::get<mpt::ReplayHeader>(header));
  mpt::dump_lane::PartyChangeTracker tracker(options.watchdog);
  tracker.observe(0, {session->director().party().phase, overlay_of(session->runtime()),
                      session->director().party().turn});
  // --party-resume FILE (diagnosis only): continue from a --party-save-at state, with the tracker as it
  // was. A resumed run records no replay, and its result is not evidence.
  const bool resumed = !options.party_resume.empty();
  if (resumed) {
    const auto bytes = mpt::dump_lane::read_file(options.party_resume);
    std::uint64_t head[3] = {};
    if (!bytes || bytes->size() < sizeof head) {
      std::printf("dump lane: cannot read the resume state\n");
      return 1;
    }
    std::memcpy(head, bytes->data(), sizeof head);
    mpt::SaveState state;
    state.title = mpt::TitleId::mp4;
    state.frame = head[0];
    state.format_version = static_cast<std::uint32_t>(head[1]);
    state.data.assign(bytes->begin() + sizeof head, bytes->end());
    if (mpt::Result r = session->loadState(state); !r) {
      std::printf("dump lane: resume: %s\n", r.error().c_str());
      return 1;
    }
    tracker.restore({session->director().party().phase, overlay_of(session->runtime()),
                     session->director().party().turn},
                    head[2], state.frame);
    std::printf("dump lane: resumed at frame %llu (diagnosis only: no replay)\n",
                static_cast<unsigned long long>(state.frame));
  }
  std::size_t events_printed = 0;
  std::string party_line;
  std::string failure;
  std::optional<std::uint64_t> finished_at;
  std::uint64_t hash_calls = 0;
  std::chrono::steady_clock::duration hash_time{};
  // Peak arena use (ABI v1.4 memory_use), sampled at every checkpoint: OSAlloc's allocated blocks, each
  // HuMem heap's allocated bytes and ARAM in use, each with the frame of its peak.
  struct Peak {
    std::uint64_t bytes = 0, frame = 0;
    void take(std::uint64_t b, std::uint64_t f) {
      if (b > bytes) {
        bytes = b;
        frame = f;
      }
    }
  };
  Peak peak_osalloc, peak_aram;
  std::array<Peak, MPT_MP4_MEMORY_HEAPS> peak_heap{};
  std::optional<MptMp4MemoryUse> last_use;
  mpt::dump_lane::watch::Watcher watcher(options.watch);
  for (std::uint64_t f = session->frame(); f < options.frames; ++f) {
    if (watcher.active()) watcher.before(f);
    if (options.party_save_at != 0 && f == options.party_save_at && !options.party_save.empty()) {
      const mpt::SaveState state = session->runtime().saveState();
      const std::uint64_t head[3] = {state.frame, state.format_version, tracker.changedAt()};
      std::vector<std::byte> out(sizeof head + state.data.size());
      std::memcpy(out.data(), head, sizeof head);
      std::memcpy(out.data() + sizeof head, state.data.data(), state.data.size());
      std::printf("dump lane: saved the state at frame %llu: %s\n", static_cast<unsigned long long>(f),
                  write_bytes(options.party_save, out) ? "ok" : "FAILED");
    }
    mpt::dump_lane::crash_note_frame(f);  // crash report breadcrumbs (crash_report.h)
    mpt::InputFrame in;
    in.frame = f;
    in.pads[0] = mpt::dump_lane::party_autopilot(options.script, f, tracker.key(), tracker.sinceChange());
    // Diagnosis: --press adds buttons on top of the autopilot (to probe a soft lock, with --party-resume).
    in.pads[0].buttons |= scripted_pad(options, f).buttons;
    const auto step_t0 = std::chrono::steady_clock::now();
    const mpt::Result stepped = session->step(in);
    if (options.frame_times) frame_clock.step(std::chrono::steady_clock::now() - step_t0, mp4);
    if (!resumed && session->frame() == f + 1) {
      if (mpt::Result r = recorder.record(in, *session); !r) {
        failure = "record: " + r.error();
        break;
      }
    }
    if (watcher.active()) watcher.after(f + 1);
    if (!stepped) {
      failure = stepped.error();
      break;
    }
    const mpt::PartySnapshot& party = session->director().party();
    MptMp4Party raw{};
    raw.struct_size = sizeof raw;
    if (mp4 != nullptr) mp4->readParty(raw);
    tracker.observe(f + 1, {party.phase, raw.overlay_name, party.turn});
    // The board the party really plays (M4-03): once a party is in progress, the module's board read
    // must be the one asked for, or the run fails (a wheel window that missed would play board 0).
    if (mp4 != nullptr && (raw.flags & MPT_MP4_PARTY_IN_PROGRESS) != 0 && raw.board != options.script.board) {
      failure = "played board " + std::to_string(raw.board) + ", not --party-board " +
                std::to_string(options.script.board);
      break;
    }
    mpt::dump_lane::crash_note_party(phase_name(party.phase), raw.overlay_name, raw.board, party.turn, party.max_turns);
    // The Director's phase and the module's raw read, whenever either changes (the evidence for the
    // end-of-party flags, docs/titles/mp4.md section 21).
    char line[192];
    std::snprintf(line, sizeof line, "overlay %s phase %s turn %d/%d raw flags 0x%02x turn %d/%d board %d",
                  raw.overlay_name[0] != 0 ? raw.overlay_name : "-", phase_name(party.phase), party.turn,
                  party.max_turns, raw.flags, raw.turn, raw.max_turns, raw.board);
    if (party_line != line) {
      party_line = line;
      std::printf("frame %llu party %s\n", static_cast<unsigned long long>(f + 1), line);
    }
    const auto events = session->director().events();
    for (; events_printed < events.size(); ++events_printed) print_event(events[events_printed]);
    if ((f + 1) % interval == 0) {
      const auto t0 = std::chrono::steady_clock::now();
      const std::uint64_t hash = session->runtime().stateHash();
      hash_time += std::chrono::steady_clock::now() - t0;
      ++hash_calls;
      mpt::dump_lane::crash_note_checkpoint(f + 1, hash);
      if (mp4 != nullptr) {
        if (const std::optional<MptMp4MemoryUse> use = mp4->memoryUse()) {
          peak_osalloc.take(use->osalloc_used, f + 1);
          peak_aram.take(use->aram_used, f + 1);
          for (std::uint32_t h = 0; h < use->heap_count; ++h) peak_heap[h].take(use->heap_used[h], f + 1);
          last_use = use;
        }
      }
      if ((f + 1) % (interval * 100) == 0)
        std::printf("frame %llu hash %016llx\n", static_cast<unsigned long long>(f + 1),
                    static_cast<unsigned long long>(hash));
      if (items_wanted && mp4 != nullptr && !tally.take(options.hash_items, mp4->debugHashItems())) {
        failure = "could not write the hash items";
        break;
      }
    }
    if (party.phase == mpt::PartyPhase::finished && !finished_at) {
      finished_at = f + 1;
      // Stop on a checkpoint, so the replay's last frame carries one.
    }
    if (finished_at && (f + 1) % interval == 0) break;
    if (tracker.stuck()) {
      failure = "soft lock: phase " + std::string(phase_name(party.phase)) + ", overlay " + tracker.key().overlay +
                ", turn " + std::to_string(party.turn) + " unchanged since frame " +
                std::to_string(tracker.changedAt());
      break;
    }
  }
  const std::uint64_t frames = session->frame();
  std::printf("dump lane: %zu party events in %llu frames\n", session->director().events().size(),
              static_cast<unsigned long long>(frames));
  for (const mpt::PartyEvent& e : session->director().events()) print_event(e);
  if (hash_calls != 0)
    std::printf("dump lane: stateHash %.0f us per call (mean of %llu)\n",
                std::chrono::duration<double, std::micro>(hash_time).count() / static_cast<double>(hash_calls),
                static_cast<unsigned long long>(hash_calls));
  if (items_wanted && !tally.finish(options.hash_items) && failure.empty()) failure = "could not write the hash notes";
  if (options.frame_times) frame_clock.print();

  if (last_use) {
    const auto mib = [](std::uint64_t b) { return static_cast<double>(b) / (1024.0 * 1024.0); };
    std::printf("dump lane: peak arena use: OSAlloc %.2f of %.2f MiB main RAM (frame %llu), ARAM %.2f MiB (frame %llu)\n",
                mib(peak_osalloc.bytes), mib(last_use->main_ram), static_cast<unsigned long long>(peak_osalloc.frame),
                mib(peak_aram.bytes), static_cast<unsigned long long>(peak_aram.frame));
    std::uint64_t heaps = 0;
    for (std::uint32_t h = 0; h < last_use->heap_count; ++h) {
      heaps += last_use->heap_size[h];
      std::printf("dump lane: peak arena use: heap %u %.2f of %.2f MiB (%.1f%%, frame %llu)\n", h,
                  mib(peak_heap[h].bytes), mib(last_use->heap_size[h]),
                  last_use->heap_size[h] ? 100.0 * static_cast<double>(peak_heap[h].bytes) / static_cast<double>(last_use->heap_size[h]) : 0.0,
                  static_cast<unsigned long long>(peak_heap[h].frame));
    }
    std::printf("dump lane: peak arena use: SDK and other OSAlloc blocks %.2f MiB at the OSAlloc peak\n",
                mib(peak_osalloc.bytes > heaps ? peak_osalloc.bytes - heaps : 0));
  } else {
    std::puts("dump lane: peak arena use: the module has no memory_use (ABI older than v1.4)");
  }

  // Save and load times at the end (a harness measurement; nothing in the runtime reads a clock).
  if (failure.empty() || session->runtime().status().status == mpt::RuntimeStatus::running) {
    const auto s0 = std::chrono::steady_clock::now();
    const mpt::SaveState state = session->runtime().saveState();
    const auto s1 = std::chrono::steady_clock::now();
    const std::uint64_t before = session->runtime().stateHash();
    const auto l0 = std::chrono::steady_clock::now();
    const mpt::Result loaded = session->loadState(state);
    const auto l1 = std::chrono::steady_clock::now();
    const bool same = loaded && session->runtime().stateHash() == before;
    std::printf("dump lane: save state %zu bytes, save %.1f ms, load %.1f ms, %s\n", state.data.size(),
                std::chrono::duration<double, std::milli>(s1 - s0).count(),
                std::chrono::duration<double, std::milli>(l1 - l0).count(),
                same ? "hash unchanged" : (loaded ? "HASH CHANGED" : ("load failed: " + loaded.error()).c_str()));
    if (!same && failure.empty()) failure = "the end-of-run save state did not round-trip";
  }

  const mpt::Replay replay = recorder.finish();
  if (!options.replay_out.empty() && !resumed) {
    const std::vector<std::byte> bytes = mpt::encode_replay(replay);
    if (!write_bytes(options.replay_out, bytes)) {
      std::printf("dump lane: could not write the replay\n");
      return 1;
    }
    std::printf("dump lane: replay %llu frames, %zu input runs, %zu checkpoints, %zu bytes, last hash %016llx\n",
                static_cast<unsigned long long>(replay.frame_count), replay.runs.size(), replay.checkpoints.size(),
                bytes.size(),
                static_cast<unsigned long long>(replay.checkpoints.empty() ? 0 : replay.checkpoints.back().state_hash));
  }
  if (!failure.empty()) {
    std::printf("dump lane: party FAILED at frame %llu: %s\n", static_cast<unsigned long long>(frames),
                failure.c_str());
    return 1;
  }
  if (!finished_at) {
    std::printf("dump lane: party FAILED: phase %s after %llu frames, never finished\n",
                phase_name(session->director().party().phase), static_cast<unsigned long long>(frames));
    return 1;
  }
  std::printf("dump lane: party finished at frame %llu\n", static_cast<unsigned long long>(*finished_at));
  return 0;
}

// `--verify-replay FILE`: plays an .mptr on a fresh Session and checks every checkpoint's state hash and
// party digest (verify_replay). Needs no sidecar: the replay's own checkpoints are the reference.
int verify_replay_file(const BootOptions& options, const mpt::VerifiedDump& dump, mpt::TitleRegistry& registry) {
  const auto bytes = mpt::dump_lane::read_file(options.verify_replay);
  if (!bytes) {
    std::printf("dump lane: cannot read the replay\n");
    return 1;
  }
  if (mpt::Result r = mpt::validate_replay_fixture(*bytes); !r) {
    std::printf("dump lane: replay: %s\n", r.error().c_str());
    return 1;
  }
  auto decoded = mpt::decode_replay(*bytes);
  if (mpt::Result* failure = std::get_if<mpt::Result>(&decoded)) {
    std::printf("dump lane: replay: %s\n", failure->error().c_str());
    return 1;
  }
  const mpt::Replay& replay = std::get<mpt::Replay>(decoded);
  auto config = mpt::session_config_for(replay.header, dump);
  if (mpt::Result* failure = std::get_if<mpt::Result>(&config)) {
    std::printf("dump lane: replay: %s\n", failure->error().c_str());
    return 1;
  }
  auto started = mpt::Session::start(registry, std::get<mpt::SessionConfig>(config));
  if (mpt::Result* failure = std::get_if<mpt::Result>(&started)) {
    std::printf("dump lane: session: %s\n", failure->error().c_str());
    return 1;
  }
  mpt::Session& session = *std::get<std::unique_ptr<mpt::Session>>(started);
  const mpt::ReplayVerification v = mpt::verify_replay(session, replay);
  const bool all = v.status && v.frames_played == replay.frame_count && v.trail.size() == replay.checkpoints.size();
  std::printf("dump lane: verify-replay played %llu of %llu frames, %zu of %zu checkpoints, last hash %016llx, "
              "Director phase %s: %s\n",
              static_cast<unsigned long long>(v.frames_played), static_cast<unsigned long long>(replay.frame_count),
              v.trail.size(), replay.checkpoints.size(),
              static_cast<unsigned long long>(v.trail.empty() ? 0 : v.trail.back().state_hash),
              phase_name(session.director().party().phase), all ? "PASS" : "FAIL");
  if (!v.status) std::printf("dump lane: %s\n", v.status.error().c_str());
  return all ? 0 : 1;
}
#endif

// The M1 check: boot headless on the dump, no input, and step.
int boot(const BootOptions& options, const std::filesystem::path& dump_dir, const std::filesystem::path& cache) {
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
  mpt::dump_lane::crash_note_run(options.party                    ? "party"
                                 : !options.verify_replay.empty() ? "verify-replay"
                                 : options.save_load_diff != 0    ? "save-load-diff"
                                 : options.conformance            ? "conformance"
                                                                  : "boot",
                                 options.seed);  // crash report breadcrumbs
  auto dump = resolve_dump(*title, dump_dir, cache);
  if (mpt::Result* failure = std::get_if<mpt::Result>(&dump)) {
    std::printf("dump lane: %s\n", failure->error().c_str());
    return 1;
  }
  if (options.party || !options.verify_replay.empty()) {
#if defined(MPT_DUMP_LANE_MP4)
    if (*title != mpt::TitleId::mp4) {
      std::puts("dump lane: --party and --verify-replay run on mp4 only for now");
      return 1;
    }
    const mpt::VerifiedDump& verified = std::get<mpt::VerifiedDump>(dump);
    return options.party ? party_run(options, verified, registry) : verify_replay_file(options, verified, registry);
#else
    std::puts("dump lane: --party and --verify-replay need the MP4 runtime");
    return 1;
#endif
  }
  if (options.save_load_diff != 0) {
#if defined(MPT_DUMP_LANE_MP4)
    return save_load_diff(options, std::get<mpt::VerifiedDump>(dump), registry);
#else
    std::puts("dump lane: --save-load-diff needs the MP4 runtime");
    return 1;
#endif
  }
  if (options.conformance) {
#if defined(MPT_DUMP_LANE_MP4)
    if (*title != mpt::TitleId::mp4) {
      std::puts("dump lane: --conformance runs on mp4 only for now");
      return 1;
    }
    return conformance_run(options, std::get<mpt::VerifiedDump>(dump), registry) == 0 ? 0 : 1;
#else
    std::puts("dump lane: --conformance needs the MP4 runtime, which this build does not have");
    return 1;
#endif
  }
  std::unique_ptr<mpt::ITitleRuntime> runtime = registry.createRuntime(*title);
#if defined(MPT_DUMP_LANE_MP4)
  auto* mp4 = dynamic_cast<mpt::mp4::Mp4Runtime*>(runtime.get());
  if (mp4 != nullptr) mp4->setReportSink(&print_report, nullptr);
  // debug_perturb, debug_deep_hash and debug_hash_items need MPT_MP4_BOOT_DEBUG_HOOKS.
  if (mp4 != nullptr) {
    mp4->setDebugHooks(options.perturb || !options.hash_items.empty());
    mp4->setNoteModuleHits(options.note_module_hits);
  }
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
  std::string party_line;  // the last printed MP4 party read
  std::uint64_t hash_calls = 0, deep_calls = 0;
  std::chrono::steady_clock::duration hash_time{}, deep_time{};
  for (std::uint64_t f = 0; f < options.frames; ++f) {
    mpt::dump_lane::crash_note_frame(f);  // crash report breadcrumbs (crash_report.h)
    input.frame = f;
    input.pads[0] = scripted_pad(options, f);
    runtime->stepFrame(input);
#if defined(MPT_DUMP_LANE_MP4)
    // MP4's party read and the adapter's phase, printed whenever either changes: the evidence for the
    // party flags (docs/titles/mp4.md section 19) and the phases a script goes through.
    if (mp4 != nullptr) {
      MptMp4Party raw{};
      if (mp4->readParty(raw)) {
        const mpt::PartySnapshot snap = mpt::mp4::snapshot_of(raw);
        static const char* const kPhases[] = {"none", "setup", "board", "minigame", "results", "finished"};
        mpt::dump_lane::crash_note_party(kPhases[static_cast<int>(snap.phase)], raw.overlay_name, raw.board, raw.turn,
                                         raw.max_turns);
        char line[256];
        std::snprintf(line, sizeof line, "overlay %s flags 0x%02x phase %s board %d turn %d/%d minigame %s",
                      raw.overlay_name[0] != 0 ? raw.overlay_name : "-", raw.flags,
                      kPhases[static_cast<int>(snap.phase)], raw.board, raw.turn, raw.max_turns,
                      snap.current_minigame ? snap.current_minigame->str().c_str() : "-");
        if (party_line != line) {
          party_line = line;
          std::printf("frame %llu party %s\n", static_cast<unsigned long long>(f), line);
        }
      }
    }
#endif
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
      mpt::dump_lane::crash_note_checkpoint(f + 1, hash);
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
      if (options.perturb && mp4 != nullptr)
        perturb_failures += perturb_checks(*mp4, f + 1, hash_calls - 1, options.perturb_samples);
      // With debug hooks, time the deep hash too: the raw-memory digest stateHash() is compared with.
      if (mp4 != nullptr && (options.perturb || !options.hash_items.empty())) {
        const auto d0 = std::chrono::steady_clock::now();
        if (const std::optional<std::uint64_t> deep = mp4->debugDeepHash()) {
          deep_time += std::chrono::steady_clock::now() - d0;
          ++deep_calls;
          std::printf("frame %llu deep %016llx\n", static_cast<unsigned long long>(f + 1),
                      static_cast<unsigned long long>(*deep));
        }
      }
      // state_hash's parts, for diffing two platforms (E3c): rewritten at every checkpoint, so the file
      // holds the last one. It names the game's symbols: it stays in the directory given.
      if (!options.hash_items.empty() && mp4 != nullptr) {
        const std::string items = mp4->debugHashItems();
        std::ofstream out(options.hash_items, std::ios::binary | std::ios::trunc);
        out.write(items.data(), static_cast<std::streamsize>(items.size()));
        if (items.empty() || !out) std::printf("dump lane: could not write the hash items\n");
      }
      // Without debug hooks the module must refuse to perturb or give a deep hash, and change nothing
      // (PR #48).
      if (!options.perturb && options.hash_items.empty() && mp4 != nullptr && hash_calls == 1) {
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
  if (deep_calls != 0) {
    const double us = std::chrono::duration<double, std::micro>(deep_time).count() / static_cast<double>(deep_calls);
    std::printf("dump lane: deep hash %.0f us per call (mean of %llu)\n", us, static_cast<unsigned long long>(deep_calls));
  }
  if (!options.perturb && perturb_failures != 0) {
    std::puts("dump lane: the module perturbed a game booted without debug hooks");
    return 1;
  }
#if defined(MPT_DUMP_LANE_MP4)
  // ABI v1.4 memory_use on the real game (qa, #72): a caller with the 160-byte v1.4 struct gets at most
  // 160 bytes written and its struct_size echoed, and the I/O counts have counted the boot's reads.
  if (mp4 != nullptr && stopped.empty()) {
    std::array<unsigned char, 256> buf;
    buf.fill(0xCD);
    const std::int32_t rc = mp4->memoryUseInto(buf.data(), MPT_MP4_MEMORY_USE_SIZE_V1_4);
    std::uint32_t echoed = 0;
    std::memcpy(&echoed, buf.data(), sizeof echoed);
    bool guard_ok = true;
    for (std::size_t i = MPT_MP4_MEMORY_USE_SIZE_V1_4; i < buf.size(); ++i) guard_ok = guard_ok && buf[i] == 0xCD;
    const std::optional<MptMp4MemoryUse> use = mp4->memoryUse();
    const bool io_ok = use && use->struct_size == sizeof(MptMp4MemoryUse) && use->dvd_reads > 0;
    std::printf("dump lane: memory_use: v1.4 caller %s, I/O counts %s (%llu DVD reads, %llu ARAM transfers)\n",
                rc == MPT_MP4_OK && echoed == MPT_MP4_MEMORY_USE_SIZE_V1_4 && guard_ok ? "ok" : "FAILED",
                io_ok ? "ok" : "FAILED", use ? static_cast<unsigned long long>(use->dvd_reads) : 0ull,
                use ? static_cast<unsigned long long>(use->aram_transfers) : 0ull);
    if (rc != MPT_MP4_OK || echoed != MPT_MP4_MEMORY_USE_SIZE_V1_4 || !guard_ok || !io_ok) return 1;
  }
#endif
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
  // --supervise [--crash-report FILE]: rerun as a child and report the crashes it cannot (supervise.h).
  if (const std::optional<int> supervised = mpt::dump_lane::supervise_if_asked(argc, argv, MPT_REPO_ROOT))
    return *supervised;
  mpt::dump_lane::install_crash_report();  // a crash prints its stack (crash_report.h)
  if (const std::optional<int> selftest = mpt::dump_lane::crash_selftest_if_asked(argc, argv)) return *selftest;
  BootOptions boot_options;
  bool frames_given = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--boot" && has_value) {
      boot_options.title = argv[++i];
    } else if (arg == "--frames" && has_value) {
      boot_options.frames = std::strtoull(argv[++i], nullptr, 10);
      frames_given = true;
    } else if (arg == "--every" && has_value) {
      boot_options.every = std::strtoull(argv[++i], nullptr, 10);
    } else if (arg == "--seed" && has_value) {
      boot_options.seed = std::strtoull(argv[++i], nullptr, 10);
    } else if (arg == "--perturb") {
      boot_options.perturb = true;
    } else if (arg == "--save-load-diff" && has_value) {
      boot_options.save_load_diff = std::strtoull(argv[++i], nullptr, 10);
    } else if (arg == "--conformance") {
      boot_options.conformance = true;
    } else if (arg == "--hash-items" && has_value) {
      boot_options.hash_items = argv[++i];
    } else if (arg == "--layout-probe") {
      // The layout probe mode of the MP4 module this lane's recipe builds (MPT_MP4_RECIPE_LAYOUT_PROBE),
      // for layout_perturbation.cmake to check its two lanes. Needs no dump.
      std::printf("dump lane: layout probe %s\n", MPT_DUMP_LANE_LAYOUT_PROBE);
      return 0;
    } else if (arg == "--frame-times") {
      boot_options.frame_times = true;
    } else if (arg == "--note-module-hits") {
      boot_options.note_module_hits = true;
    } else if (arg == "--perturb-samples" && has_value) {
      boot_options.perturb_samples = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
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
    } else if (arg == "--party") {
      boot_options.party = true;
    } else if (arg == "--party-unlock-bowser") {
      boot_options.unlock_bowser = true;
    } else if (arg == "--party-board" && has_value) {
      boot_options.script.board = static_cast<std::int32_t>(std::strtol(argv[++i], nullptr, 10));
      if (boot_options.script.wheel_to == 0) {
        // The measured window (docs/titles/mp4.md, M4-03): the wheel takes the stick from frame 1,448
        // of mentDll on; one kBoardWheelPulse slot per move, then 30 frames before A confirms.
        // --party-wheel (after --party-board) overrides it.
        const int moves = mpt::dump_lane::board_wheel_moves(boot_options.script.board);
        boot_options.script.wheel_from = mpt::dump_lane::kBoardWheelFrom;
        boot_options.script.wheel_to = mpt::dump_lane::kBoardWheelFrom + 30u +
            mpt::dump_lane::kBoardWheelPulse * static_cast<std::uint64_t>(moves < 0 ? -moves : moves);
      }
    } else if (arg == "--party-wheel" && has_value) {
      // FROM:TO, frames since mentDll started
      const std::string w = argv[++i];
      const std::size_t colon = w.find(':');
      boot_options.script.wheel_from = std::strtoull(w.substr(0, colon).c_str(), nullptr, 10);
      boot_options.script.wheel_to =
          colon == std::string::npos ? 0 : std::strtoull(w.substr(colon + 1).c_str(), nullptr, 10);
    } else if (arg == "--party-save-at" && i + 2 < argc) {
      boot_options.party_save_at = std::strtoull(argv[++i], nullptr, 10);
      boot_options.party_save = argv[++i];
    } else if (arg == "--party-resume" && has_value) {
      boot_options.party_resume = argv[++i];
    } else if (arg == "--watch-word" && has_value) {
      boot_options.watch.words = mpt::dump_lane::watch::parse_list(argv[++i]);
    } else if (arg == "--find-word" && has_value) {
      const std::string w = argv[++i];  // VALUE@FRAME, VALUE in hex
      const std::size_t at = w.find('@');
      boot_options.watch.find_value = std::strtoull(w.substr(0, at).c_str(), nullptr, 16);
      boot_options.watch.find_frame = at == std::string::npos ? 0 : std::strtoull(w.substr(at + 1).c_str(), nullptr, 10);
    } else if (arg == "--watch-write" && has_value) {
      const std::string w = argv[++i];  // OFF[,OFF...]@FRAME, offsets in hex
      const std::size_t at = w.find('@');
      boot_options.watch.writes = mpt::dump_lane::watch::parse_list(w.substr(0, at));
      boot_options.watch.write_frame = at == std::string::npos ? 0 : std::strtoull(w.substr(at + 1).c_str(), nullptr, 10);
    } else if (arg == "--dump-arena" && i + 2 < argc) {
      boot_options.watch.dump_frame = std::strtoull(argv[++i], nullptr, 10);
      boot_options.watch.dump_path = argv[++i];
    } else if (arg == "--watchdog" && has_value) {
      boot_options.watchdog = std::strtoull(argv[++i], nullptr, 10);
    } else if (arg == "--replay" && has_value) {
      boot_options.replay_out = argv[++i];
    } else if (arg == "--verify-replay" && has_value) {
      boot_options.verify_replay = argv[++i];
    } else if (arg == "--capture" && has_value) {
      boot_options.capture_dir = argv[++i];
    } else if (arg == "--capture-every" && has_value) {
      boot_options.capture_every = std::strtoull(argv[++i], nullptr, 10);
    } else {
      std::printf("usage: mpt_dump_lane [--boot <title> [--frames N] [--every N] [--seed S] [--perturb [--perturb-samples N]]\n"
                  "                     [--hash-items FILE [--note-module-hits]] [--conformance] [--save-load-diff N]\n"
                  "                     [--press button@frame[+frames][,...]]... [--capture DIR [--capture-every N]]\n"
                  "                     [--party [--party-board N [--party-unlock-bowser]] [--party-wheel FROM:TO]\n"
                  "                              [--watchdog N] [--replay FILE]\n"
                  "                              [--hash-items FILE [--note-module-hits]] [--frame-times]\n"
                  "                              [--party-save-at N FILE] [--party-resume FILE]\n"
                  "                              [--watch-word OFF,...] [--find-word VALUE@FRAME]\n"
                  "                              [--watch-write OFF,...@FRAME] [--dump-arena FRAME FILE]]\n"
                  "                     [--verify-replay FILE]]\n"
                  "       mpt_dump_lane --layout-probe\n"
                  "       mpt_dump_lane --supervise [--crash-report FILE] <any of the above>\n"
                  "       mpt_dump_lane [--supervise [--crash-report FILE]] --crash-selftest MODULE gs|av|abort\n");
      return 2;
    }
  }
  // A party needs far more than the boot check's 600 frames: a 20-turn party is about 150,000.
  if (boot_options.party && !frames_given) boot_options.frames = 1'000'000;
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
  if (mpt::dump_lane::is_inside(dump_dir, MPT_REPO_ROOT)) {
    std::puts("dump lane: MPT_DUMP_DIR is inside the repository; dumps must live outside it (ADR 0006)");
    return 1;
  }
  // The build cache: MPT_BUILD_CACHE, else the shell's user cache folder (the same UserDirs call
  // as the shell, so both share one cache). Never MPT_DUMP_DIR by default: it holds dumps only.
  std::filesystem::path default_cache;
  const std::string cache_env = environment("MPT_BUILD_CACHE");
  if (cache_env.empty()) {
    std::string error;
    const std::optional<mpt::shell::UserDirs> dirs = mpt::shell::user_dirs(std::nullopt, error);
    if (!dirs) {
      std::printf("dump lane: %s; set MPT_BUILD_CACHE\n", error.c_str());
      return 1;
    }
    default_cache = dirs->cache;
  }
  const mpt::dump_lane::BuildCacheRoot cache = mpt::dump_lane::build_cache_root(cache_env, default_cache);
  if (const std::string refused = mpt::dump_lane::refuse_cache_in_dump(cache, dump_dir); !refused.empty()) {
    std::printf("dump lane: %s\n", refused.c_str());
    return 1;
  }
  if (cache.from_env && mpt::dump_lane::is_inside(cache.path, dump_dir))
    std::puts("dump lane: note: MPT_BUILD_CACHE is inside MPT_DUMP_DIR; builds are written next to your dumps");
  // Captures (the user's game output) and the caches (an extracted disc, built modules, decomp
  // checkouts) must stay outside the repository too, and so must --hash-items (it names the game's
  // symbols).
  const std::array<mpt::dump_lane::GuardedPath, 7> written = {{
      {"--party-save-at", boot_options.party_save},
      {"--dump-arena", boot_options.watch.dump_path},
      {"--capture", boot_options.capture_dir},
      {"--hash-items", boot_options.hash_items},
      {"--replay", boot_options.replay_out},
      {cache.from_env ? "MPT_BUILD_CACHE" : "the build cache", cache.path},
      {"MPT_SOURCE_CACHE", std::filesystem::path(environment("MPT_SOURCE_CACHE"))},
  }};
  if (const std::string refused = mpt::dump_lane::refuse_inside(written, MPT_REPO_ROOT); !refused.empty()) {
    std::printf("dump lane: %s\n", refused.c_str());
    return 1;
  }
  if (!boot_options.title.empty()) return boot(boot_options, dump_dir, cache.path);

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
    auto dump = resolve_dump(title, dump_dir, cache.path);
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
