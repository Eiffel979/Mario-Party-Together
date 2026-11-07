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
// (path_guard.h).

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#include "dump_lane.h"
#include "crash_report.h"
#include "path_guard.h"
#include "png.h"
#include "mpt/core/title_id.h"
#include "real_titles.h"
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

struct BootOptions {
  std::string title;
  std::uint64_t frames = 600;
  std::uint64_t every = 60;
  std::uint64_t seed = 1;
  bool perturb = false;
  std::uint32_t perturb_samples = 32;  // sampled bytes per checkpoint with --perturb
  std::filesystem::path hash_items;    // --hash-items FILE: state_hash's parts at the last checkpoint
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
                   {MPT_MP4_PERTURB_FREE_HEAP, "free heap"},   {MPT_MP4_PERTURB_STACK, "process stack"}};
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
  auto started = mpt::Session::start(registry, config);
  if (mpt::Result* failure = std::get_if<mpt::Result>(&started)) {
    std::printf("dump lane: session: %s\n", failure->error().c_str());
    return 1;
  }
  std::unique_ptr<mpt::Session>& session = std::get<std::unique_ptr<mpt::Session>>(started);
  auto* mp4 = dynamic_cast<mpt::mp4::Mp4Runtime*>(&session->runtime());
  if (mp4 != nullptr) mp4->setReportSink(&print_report, nullptr);
  std::printf("dump lane: party autopilot, seed %llu, board %d, watchdog %llu frames\n",
              static_cast<unsigned long long>(options.seed), options.script.board,
              static_cast<unsigned long long>(options.watchdog));

  mpt::ReplayRecorder recorder(std::get<mpt::ReplayHeader>(header));
  mpt::dump_lane::PartyChangeTracker tracker(options.watchdog);
  tracker.observe(0, {session->director().party().phase, overlay_of(session->runtime()),
                      session->director().party().turn});
  std::size_t events_printed = 0;
  std::string failure;
  std::optional<std::uint64_t> finished_at;
  std::uint64_t hash_calls = 0;
  std::chrono::steady_clock::duration hash_time{};
  // Peak arena use (ABI v1.4 memory_use), sampled at every checkpoint: OSAlloc's allocated blocks, each
  // HuMem heap's allocated bytes and ARAM in use, each with the frame of its peak.
  struct Peak {
    std::uint64_t bytes = 0, frame = 0;
    void take(std::uint64_t b, std::uint64_t f) {
      if (b > bytes) bytes = b, frame = f;
    }
  };
  Peak peak_osalloc, peak_aram;
  std::array<Peak, MPT_MP4_MEMORY_HEAPS> peak_heap{};
  std::optional<MptMp4MemoryUse> last_use;
  for (std::uint64_t f = 0; f < options.frames; ++f) {
    mpt::InputFrame in;
    in.frame = f;
    in.pads[0] = mpt::dump_lane::party_autopilot(options.script, f, tracker.key(), tracker.sinceChange());
    const mpt::Result stepped = session->step(in);
    if (session->frame() == f + 1) {
      if (mpt::Result r = recorder.record(in, *session); !r) {
        failure = "record: " + r.error();
        break;
      }
    }
    if (!stepped) {
      failure = stepped.error();
      break;
    }
    const mpt::PartySnapshot& party = session->director().party();
    tracker.observe(f + 1, {party.phase, overlay_of(session->runtime()), party.turn});
    if (tracker.changedAt() == f + 1) {
      std::printf("frame %llu party overlay %s phase %s turn %d/%d\n", static_cast<unsigned long long>(f + 1),
                  tracker.key().overlay.empty() ? "-" : tracker.key().overlay.c_str(), phase_name(party.phase),
                  party.turn, party.max_turns);
    }
    const auto events = session->director().events();
    for (; events_printed < events.size(); ++events_printed) print_event(events[events_printed]);
    if ((f + 1) % interval == 0) {
      const auto t0 = std::chrono::steady_clock::now();
      const std::uint64_t hash = session->runtime().stateHash();
      hash_time += std::chrono::steady_clock::now() - t0;
      ++hash_calls;
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
  if (!options.replay_out.empty()) {
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
  if (mp4 != nullptr) mp4->setDebugHooks(options.perturb || !options.hash_items.empty());
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
  mpt::dump_lane::install_crash_report();  // a crash prints its stack (crash_report.h)
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
    } else if (arg == "--party-board" && has_value) {
      boot_options.script.board = static_cast<std::int32_t>(std::strtol(argv[++i], nullptr, 10));
    } else if (arg == "--party-wheel" && has_value) {
      // FROM:TO, frames since mentDll started
      const std::string w = argv[++i];
      const std::size_t colon = w.find(':');
      boot_options.script.wheel_from = std::strtoull(w.substr(0, colon).c_str(), nullptr, 10);
      boot_options.script.wheel_to =
          colon == std::string::npos ? 0 : std::strtoull(w.substr(colon + 1).c_str(), nullptr, 10);
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
                  "                     [--hash-items FILE] [--conformance] [--save-load-diff N]\n"
                  "                     [--press button@frame[+frames][,...]]... [--capture DIR [--capture-every N]]\n"
                  "                     [--party [--party-board N] [--party-wheel FROM:TO] [--watchdog N] [--replay FILE]]\n"
                  "                     [--verify-replay FILE]]\n");
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
  // Captures (the user's game output) and the caches (an extracted disc, built modules, decomp
  // checkouts) must stay outside the repository too, and so must --hash-items (it names the game's
  // symbols).
  const std::array<mpt::dump_lane::GuardedPath, 5> written = {{
      {"--capture", boot_options.capture_dir},
      {"--hash-items", boot_options.hash_items},
      {"--replay", boot_options.replay_out},
      {"MPT_BUILD_CACHE", std::filesystem::path(environment("MPT_BUILD_CACHE"))},
      {"MPT_SOURCE_CACHE", std::filesystem::path(environment("MPT_SOURCE_CACHE"))},
  }};
  if (const std::string refused = mpt::dump_lane::refuse_inside(written, MPT_REPO_ROOT); !refused.empty()) {
    std::printf("dump lane: %s\n", refused.c_str());
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
