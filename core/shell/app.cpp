#include "app.h"

#include <SDL3/SDL.h>

#include <charconv>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "audio_out.h"
#include "event_routing.h"
#include "frame_loop.h"
#include "mpt/asset_import/build.h"
#include "mpt/asset_import/importer.h"
#include "mpt/asset_import/process.h"
#include "mpt/core/input.h"
#include "mpt/core/replay.h"
#include "mpt/core/session.h"
#include "mpt/core/title_registry.h"
#include "mpt/input/sdl_input.h"
#include "overlay.h"
#include "pacer.h"
#include "presenter.h"
#include "save_store.h"

namespace mpt::shell {
namespace {

constexpr std::string_view kUsage =
    "Usage: mario-party-together [options]\n"
    "  --title ID             Start a session of a title (mp1-mp10, smp, mpss)\n"
    "  --dump PATH            The game dump to build and run for --title\n"
    "  --seed N               Session seed (default: chosen by the shell and recorded)\n"
    "  --scale N              Internal render scale (presentation only)\n"
    "  --widescreen           16:9 output (presentation only)\n"
    "  --record FILE          Record the session to a replay file (.mptr)\n"
    "  --embed-save           With --record: put the initial save image in the replay\n"
    "  --replay FILE          Play a replay in the window and check its hash trail\n"
    "  --verify-replay FILE   Check a replay headless and print its hash trail\n"
    "  --user-dir PATH        User data folder (default: the platform's preference folder)\n"
    "  --stats                Print frame timing at exit\n"
    "  --headless             Run without a visible window or audio device\n"
    "  --frames N             Exit after N frames\n"
    "  --identify PATH        Identify a game dump against the known-dump list and exit\n"
    "  --version              Print the version and exit\n"
    "  --help                 Print this help and exit\n";

// Behind by more than this, the pacer drops the backlog instead of fast-forwarding.
constexpr std::uint64_t kMaxLagNs = 250'000'000;
// Idle wait when no frame is due (no session, paused, UI focus).
constexpr std::uint64_t kIdleFrameNs = 1'000'000'000ull / 60;

struct UserDirs {
  std::filesystem::path root;
  std::filesystem::path saves;  // <root>/saves/<title>/<region>/ (ADR 0004 rule 5)
  std::filesystem::path cache;  // <root>/cache: the BuildCache root (P1-10)
};

std::optional<UserDirs> user_dirs(const AppOptions& options, std::string& error) {
  std::filesystem::path root;
  if (options.user_dir) {
    root = *options.user_dir;
  } else {
    // Short org and app names keep build-cache paths well inside Windows' 260-character
    // limit (P1-09/P1-10; see also the longPathAware manifest).
    char* pref = SDL_GetPrefPath("MarioPartyTogether", "mpt");
    if (!pref) {
      error = std::string("no user data folder: ") + SDL_GetError();
      return std::nullopt;
    }
    root = std::filesystem::path(reinterpret_cast<const char8_t*>(pref));
    SDL_free(pref);
  }
  return UserDirs{root, root / "saves", root / "cache"};
}

std::optional<std::vector<std::byte>> read_file(const std::filesystem::path& path, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot read " + display_path(path);
    return std::nullopt;
  }
  const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) out[i] = static_cast<std::byte>(chars[i]);
  return out;
}

std::string hex64(std::uint64_t v) {
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
  return buf;
}

// Same layout as the committed .trail.txt sidecars (ADR 0006): "<frame> <state_hash>".
void print_trail(const std::vector<ReplayCheckpoint>& trail) {
  for (const ReplayCheckpoint& c : trail) {
    std::printf("%llu %s\n", static_cast<unsigned long long>(c.frame), hex64(c.state_hash).c_str());
  }
}

const ShellTitle* find_title(const ShellTitles& titles, TitleId id) {
  for (const ShellTitle& t : titles) {
    if (t.id == id) return &t;
  }
  return nullptr;
}

TitleRegistry make_registry(const ShellTitles& titles) {
  TitleRegistry registry;
  for (const ShellTitle& t : titles) {
    if (Result added = registry.add(t.id, t.runtime, t.adapter); !added) {
      std::fprintf(stderr, "title registration: %s\n", added.error().c_str());
    }
  }
  return registry;
}

std::optional<asset_import::KnownDumps> load_known_dumps(std::string& error) {
  // known_dumps.json is copied next to the executable at build time.
  const char* base = SDL_GetBasePath();
  const std::filesystem::path db_path = std::filesystem::path(base ? base : "") / "data" / "known_dumps.json";
  return asset_import::KnownDumps::load(db_path, error);
}

int identify(const std::filesystem::path& path) {
  std::string error;
  auto known = load_known_dumps(error);
  if (!known) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  const asset_import::Importer importer(std::move(*known));
  const asset_import::IdentifyReport report = importer.identify(path);
  std::printf("format:  %s\n", std::string(asset_import::to_string(report.format)).c_str());
  if (!report.sha256.empty()) std::printf("sha256:  %s\n", report.sha256.c_str());
  if (report.match) {
    std::printf("match:   %s %s rev %s\n", std::string(to_string(report.match->title)).c_str(),
                report.match->region.c_str(), report.match->revision.c_str());
    return 0;
  }
  std::fprintf(stderr, "%s\n", report.message.c_str());
  return 1;
}

// The VerifiedDump for a title: built from --dump through asset-import (the only way to
// make one for a real title, ADR 0009), or a test registration's synthetic dump.
std::variant<VerifiedDump, Result> resolve_dump(const ShellTitle& title, const AppOptions& options,
                                                const UserDirs& dirs) {
  const std::string name(to_string(title.id));
  if (!options.dump) {
    if (title.synthetic_dump) return *title.synthetic_dump;
    return Result::failure("--dump is required to run " + name);
  }
  std::string error;
  auto known = load_known_dumps(error);
  if (!known) return Result::failure(error);
  const asset_import::Importer importer(std::move(*known));
  const asset_import::IdentifyReport report = importer.identify(*options.dump);
  if (!report.match) return Result::failure(report.message.empty() ? "unknown dump" : report.message);
  if (report.match->title != title.id) {
    return Result::failure("that dump is " + std::string(to_string(report.match->title)) + ", not " + name);
  }
  if (!title.recipe) return Result::failure("this build has no local build recipe for " + name);

  const std::unique_ptr<asset_import::BuildRecipe> recipe = title.recipe();
  std::optional<asset_import::DumpForBuild> dump = asset_import::dump_for_build(report, *options.dump, error);
  if (!dump) return Result::failure(error);
  asset_import::SystemProcessRunner runner;
  asset_import::BuildCache cache(dirs.cache, runner);
  asset_import::BuildRequest request;
  request.dump = std::move(*dump);
  request.progress = [](const asset_import::BuildProgress& p) {
    std::printf("build: [%zu/%zu] %s\n", p.stage_index + 1, p.stage_count, p.stage.c_str());
  };
  std::optional<asset_import::BuildOutcome> outcome = cache.build(*recipe, request, error);
  if (!outcome) return Result::failure("build failed: " + error);
  std::optional<VerifiedDump> verified = asset_import::make_verified(report, *outcome, error);
  if (!verified) return Result::failure(error);
  return *verified;
}

// Host randomness is fine here: the seed is chosen once, outside game logic, and recorded
// in the replay header (ADR 0010 §3.4, ADR 0006).
std::uint64_t pick_seed() {
  std::random_device device;
  const std::uint64_t hi = device();
  const std::uint64_t lo = device();
  return (hi << 32) ^ lo ^ SDL_GetTicksNS();
}

template <typename T>
std::optional<T> take_or_print(std::variant<T, Result> v, const char* context) {
  if (Result* failure = std::get_if<Result>(&v)) {
    std::fprintf(stderr, "%s: %s\n", context, failure->error().c_str());
    return std::nullopt;
  }
  return std::get<T>(std::move(v));
}

// --verify-replay: no window, no audio, no UI (ADR 0010 §3.5). Prints the trail and
// exits non-zero on the first mismatch.
int run_verify_replay(const AppOptions& options, const ShellTitles& titles) {
  std::string error;
  const std::optional<UserDirs> dirs = user_dirs(options, error);
  if (!dirs) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  const std::optional<std::vector<std::byte>> bytes = read_file(*options.verify_replay, error);
  if (!bytes) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  std::optional<Replay> replay = take_or_print(decode_replay(*bytes), "replay");
  if (!replay) return 1;
  const ShellTitle* title = find_title(titles, replay->header.title);
  if (!title) {
    std::fprintf(stderr, "replay: %s is not available in this build\n",
                 std::string(to_string(replay->header.title)).c_str());
    return 1;
  }
  std::optional<VerifiedDump> dump = take_or_print(resolve_dump(*title, options, *dirs), "dump");
  if (!dump) return 1;
  // The user's own save may be the image the replay needs; it is matched by digest and
  // never written back (ephemeral, ADR 0004 rule 6).
  const std::optional<SaveMedia> slot = load_save_media(save_slot_path(dirs->saves, title->id, dump->region), error);
  std::optional<SessionConfig> config = take_or_print(session_config_for(replay->header, *dump, slot), "replay");
  if (!config) return 1;
  config->runtime.internal_scale = options.internal_scale;
  config->runtime.widescreen = options.widescreen;

  const TitleRegistry registry = make_registry(titles);
  std::optional<std::unique_ptr<Session>> session = take_or_print(Session::start(registry, *config), "session");
  if (!session) return 1;
  const ReplayVerification v = verify_replay(**session, *replay);
  print_trail(v.trail);
  if (!v.status) {
    std::fprintf(stderr, "%s\n", v.status.error().c_str());
    return 1;
  }
  std::printf("replay OK: %llu frames, %zu checkpoints\n", static_cast<unsigned long long>(v.frames_played),
              replay->checkpoints.size());
  return 0;
}


class Shell final : public IShellCommands {
 public:
  Shell(const AppOptions& options, const ShellTitles& titles, UserDirs dirs, SDL_Renderer* renderer)
      : options_(options), titles_(titles), dirs_(std::move(dirs)), registry_(make_registry(titles)),
        presenter_(renderer) {}

  // IShellCommands: queued, applied at the top of the next loop iteration.
  void requestStart(const StartRequest& request) override { pending_start_ = request; }
  void requestPause() override { pending_pause_ = true; }
  void requestResume() override { pending_resume_ = true; }
  void requestStop() override { pending_stop_ = true; }
  void requestQuit() override { running_ = false; }

  void set_overlay(IOverlay* overlay) { overlay_ = overlay; }

  // Starts the session the command line asks for. False on failure (already printed).
  bool start_from_options() {
    if (options_.replay) return start_replay(*options_.replay);
    if (!options_.title) return true;  // no session: an empty window until the UI asks
    const ShellTitle* title = find_title(titles_, *options_.title);
    if (!title) {
      std::fprintf(stderr, "%s is not available in this build\n", std::string(to_string(*options_.title)).c_str());
      return false;
    }
    std::optional<VerifiedDump> dump = take_or_print(resolve_dump(*title, options_, dirs_), "dump");
    if (!dump) return false;
    return start_live(*dump, options_.seed);
  }

  int loop() {
    std::uint64_t presented = 0;
    while (running_) {
      apply_requests();
      poll_events();
      if (!running_) break;
      if (options_.max_frames && presented >= *options_.max_frames) break;

      bool stepped = false;
      const std::uint64_t t0 = SDL_GetTicksNS();
      if (loop_) {
        const bool ui_focus = overlay_ && overlay_->hasFocus();
        // Game input becoming live again (resume, a screen losing focus) masks what is
        // held, so the press that resumed never reaches the game (ADR 0010 §3.3), and
        // starts a new pacing segment, so the frames not stepped are not caught up in a
        // burst. This runs before tick(), so before the next sample().
        if (input_gate_.update(!loop_->paused() && !ui_focus, input_)) pacer_.reset(SDL_GetTicksNS());
        const FrameLoop::Tick tick = loop_->tick(ui_focus);
        if (tick == FrameLoop::Tick::stepped) {
          stepped = true;
          presenter_.upload(session_->runtime().video());
          if (!options_.headless) audio_.push(session_->runtime().audio());
        } else if (tick == FrameLoop::Tick::failed) {
          std::fprintf(stderr, "session: %s\n", loop_->failure()->error().c_str());
          exit_code_ = 1;
          running_ = false;
        } else if (tick == FrameLoop::Tick::input_ended) {
          running_ = false;  // a replay has played to its end
        }
      }
      if (stepped) note_step_time(SDL_GetTicksNS() - t0);

      if (overlay_) overlay_->update(SDL_GetTicksNS());
      presenter_.present(options_.widescreen, overlay_);
      ++presented;
      pace(stepped);
    }
    end_session();
    if (replay_failed_) exit_code_ = 1;
    std::printf("Shell exited after %llu frame(s)\n", static_cast<unsigned long long>(presented));
    if (options_.stats) print_stats();
    return exit_code_;
  }

 private:
  bool start_live(const VerifiedDump& dump, std::optional<std::uint64_t> seed) {
    const ShellTitle* title = find_title(titles_, dump.title);
    if (!title) return false;
    slot_ = save_slot_path(dirs_.saves, dump.title, dump.region);
    // An unreadable slot is moved aside, never overwritten (save_store.h).
    SlotOpen opened = open_save_slot(*slot_);
    if (!opened.warning.empty()) std::fprintf(stderr, "save: %s\n", opened.warning.c_str());
    std::optional<SaveMedia> initial = std::move(opened.initial);
    SessionConfig config;
    config.dump = dump;
    config.runtime.seed = seed ? *seed : pick_seed();
    config.runtime.internal_scale = options_.internal_scale;
    config.runtime.widescreen = options_.widescreen;
    config.runtime.save_media = std::move(initial);

    if (options_.record) {
      std::optional<ReplayHeader> header =
          take_or_print(make_replay_header(config, options_.embed_save), "record");
      if (!header) return false;
      recorder_.emplace(std::move(*header));
    }
    FrameLoopHooks hooks;
    hooks.sample = [this](std::uint64_t frame) -> std::optional<InputFrame> { return input_.sample(frame); };
    if (opened.persist) {
      hooks.persist = [this](const SaveMedia& media) {
        if (Result stored = store_save_media(*slot_, media); !stored) {
          std::fprintf(stderr, "save: %s\n", stored.error().c_str());
        }
      };
    }
    return start_session(config, std::move(hooks), recorder_ ? &*recorder_ : nullptr);
  }

  bool start_replay(const std::filesystem::path& path) {
    std::string error;
    std::optional<std::vector<std::byte>> bytes = read_file(path, error);
    if (!bytes) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return false;
    }
    playback_ = take_or_print(decode_replay(*bytes), "replay");
    if (!playback_) return false;
    const ShellTitle* title = find_title(titles_, playback_->header.title);
    if (!title) {
      std::fprintf(stderr, "replay: %s is not available in this build\n",
                   std::string(to_string(playback_->header.title)).c_str());
      return false;
    }
    std::optional<VerifiedDump> dump = take_or_print(resolve_dump(*title, options_, dirs_), "dump");
    if (!dump) return false;
    const std::optional<SaveMedia> slot = load_save_media(save_slot_path(dirs_.saves, title->id, dump->region), error);
    std::optional<SessionConfig> config = take_or_print(session_config_for(playback_->header, *dump, slot), "replay");
    if (!config) return false;
    config->runtime.internal_scale = options_.internal_scale;
    config->runtime.widescreen = options_.widescreen;

    reader_.emplace(*playback_);
    next_checkpoint_ = 0;
    FrameLoopHooks hooks;
    hooks.sample = [this](std::uint64_t) -> std::optional<InputFrame> {
      InputFrame in;
      if (!reader_->next(in)) return std::nullopt;
      return in;
    };
    // No persist hook: a replay session is ephemeral (ADR 0004 rule 6).
    hooks.after_step = [this](Session& session) { check_replay_checkpoint(session); };
    return start_session(*config, std::move(hooks), nullptr);
  }

  bool start_session(const SessionConfig& config, FrameLoopHooks hooks, ReplayRecorder* recorder) {
    std::optional<std::unique_ptr<Session>> started = take_or_print(Session::start(registry_, config), "session");
    if (!started) return false;
    session_ = std::move(*started);
    hooks.minigame_changed = [this](const std::optional<MinigameId>& id) {
      input_.mapper().set_minigame(id ? id->str() : std::string());
    };
    loop_ = std::make_unique<FrameLoop>(*session_, std::move(hooks), recorder);
    // A pad plugged in mid-party must not add a player (input/ ports.h).
    input_.ports().set_auto_assign(false);
    // Whatever is held as the session starts (the launcher's confirm press) is not a press
    // in the game. Explicit here because a stop and a start can fall in one iteration,
    // which the gate below would not see as an edge.
    input_.mask_held_until_release();
    input_gate_ = GameInputGate{};
    pacer_.reset(SDL_GetTicksNS());
    std::printf("session: %s, seed %llu\n", std::string(to_string(config.dump.title)).c_str(),
                static_cast<unsigned long long>(config.runtime.seed));
    return true;
  }

  void end_session() {
    if (!session_) return;
    if (recorder_ && options_.record) {
      const std::vector<std::byte> bytes = encode_replay(recorder_->finish());
      if (Result written = write_file_atomic(*options_.record, bytes); !written) {
        std::fprintf(stderr, "record: %s\n", written.error().c_str());
        exit_code_ = 1;
      } else {
        std::printf("recorded %s\n", display_path(*options_.record).c_str());
      }
      recorder_.reset();
    }
    if (playback_ && !replay_failed_) {
      if (next_checkpoint_ != playback_->checkpoints.size()) {
        std::fprintf(stderr, "replay: stopped before checkpoint %zu of %zu\n", next_checkpoint_,
                     playback_->checkpoints.size());
        replay_failed_ = true;
      } else {
        std::printf("replay OK: %zu checkpoints\n", playback_->checkpoints.size());
      }
    }
    loop_.reset();
    session_.reset();  // the adapter dies before the runtime (Session's destructor)
    input_.ports().set_auto_assign(true);
    input_.mapper().set_minigame("");
    presenter_.clear_frame();
    audio_.pause();
  }

  void check_replay_checkpoint(Session& session) {
    if (replay_failed_ || next_checkpoint_ >= playback_->checkpoints.size()) return;
    const ReplayCheckpoint& want = playback_->checkpoints[next_checkpoint_];
    if (session.frame() != want.frame) return;
    const ReplayCheckpoint got = checkpoint_of(session);
    if (!(got == want)) {
      std::fprintf(stderr, "replay: %s differs at frame %llu\n",
                   want.state_hash != got.state_hash ? "state hash" : "party digest",
                   static_cast<unsigned long long>(want.frame));
      replay_failed_ = true;
      running_ = false;
    }
    ++next_checkpoint_;
  }

  void apply_requests() {
    if (pending_stop_) {
      pending_stop_ = false;
      end_session();
    }
    if (pending_start_) {
      const StartRequest request = std::move(*pending_start_);
      pending_start_.reset();
      end_session();
      if (!start_live(request.dump, request.seed)) exit_code_ = 1;
    }
    if (pending_pause_) {
      pending_pause_ = false;
      set_paused(true);
    }
    if (pending_resume_) {
      pending_resume_ = false;
      set_paused(false);
    }
  }

  void set_paused(bool paused) {
    if (!loop_ || loop_->paused() == paused) return;
    if (paused) {
      loop_->pause();
      audio_.pause();
    } else {
      loop_->resume();  // the input gate masks held inputs and resets pacing before the next step
      audio_.resume();
    }
  }

  void poll_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (route_event(e, overlay_, input_)) {  // event_routing.h
        case EventOutcome::quit:
          running_ = false;
          break;
        case EventOutcome::toggle_pause:
          if (loop_) set_paused(!loop_->paused());
          break;
        case EventOutcome::none:
          break;
      }
    }
  }

  void pace(bool stepped) {
    if (options_.headless) return;
    const std::uint64_t now = SDL_GetTicksNS();
    if (!stepped) {
      SDL_DelayNS(kIdleFrameNs);
      return;
    }
    // Pacing uses host time; game logic never sees it (ADR 0003 rule 3, CLAUDE.md rule 4).
    const std::uint64_t deadline = pacer_.advance(session_->runtime().frameRate());
    if (deadline > now) {
      SDL_DelayNS(deadline - now);
    } else if (now - deadline > kMaxLagNs) {
      pacer_.reset(now);
      ++late_resets_;
    }
  }

  void note_step_time(std::uint64_t ns) {
    ++steps_;
    total_step_ns_ += ns;
    if (ns > max_step_ns_) max_step_ns_ = ns;
  }

  void print_stats() const {
    const double avg_us = steps_ ? static_cast<double>(total_step_ns_) / static_cast<double>(steps_) / 1000.0 : 0.0;
    std::printf("stats: %llu steps, avg %.1f us, max %.1f us, %llu pacing resets\n",
                static_cast<unsigned long long>(steps_), avg_us, static_cast<double>(max_step_ns_) / 1000.0,
                static_cast<unsigned long long>(late_resets_));
  }

  const AppOptions& options_;
  const ShellTitles& titles_;
  UserDirs dirs_;
  TitleRegistry registry_;
  input::SdlInput input_;
  Presenter presenter_;
  AudioOut audio_;
  FramePacer pacer_;
  IOverlay* overlay_ = nullptr;
  GameInputGate input_gate_;

  std::unique_ptr<Session> session_;
  std::unique_ptr<FrameLoop> loop_;
  std::optional<ReplayRecorder> recorder_;
  std::optional<std::filesystem::path> slot_;
  std::optional<Replay> playback_;
  std::optional<ReplayReader> reader_;
  std::size_t next_checkpoint_ = 0;
  bool replay_failed_ = false;

  std::optional<StartRequest> pending_start_;
  bool pending_pause_ = false;
  bool pending_resume_ = false;
  bool pending_stop_ = false;
  bool running_ = true;
  int exit_code_ = 0;

  std::uint64_t steps_ = 0;
  std::uint64_t total_step_ns_ = 0;
  std::uint64_t max_step_ns_ = 0;
  std::uint64_t late_resets_ = 0;
};

bool parse_u64(std::string_view value, std::uint64_t& out) {
  const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), out);
  return ec == std::errc{} && end == value.data() + value.size();
}

}  // namespace

std::optional<AppOptions> parse_args(int argc, char** argv, std::string& message, int& exit_code) {
  AppOptions options;
  const auto usage_error = [&](const std::string& text) {
    message = text + "\n" + std::string(kUsage);
    exit_code = 2;
    return std::nullopt;
  };
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--help" || arg == "-h") {
      message = kUsage;
      exit_code = 0;
      return std::nullopt;
    }
    if (arg == "--version") {
      message = "mario-party-together " MPT_VERSION "\n";
      exit_code = 0;
      return std::nullopt;
    }
    if (arg == "--headless") {
      options.headless = true;
    } else if (arg == "--widescreen") {
      options.widescreen = true;
    } else if (arg == "--embed-save") {
      options.embed_save = true;
    } else if (arg == "--stats") {
      options.stats = true;
    } else if (arg == "--frames" && has_value) {
      std::uint64_t frames = 0;
      if (!parse_u64(argv[++i], frames)) return usage_error("--frames expects a non-negative integer");
      options.max_frames = frames;
    } else if (arg == "--seed" && has_value) {
      std::uint64_t seed = 0;
      if (!parse_u64(argv[++i], seed)) return usage_error("--seed expects a non-negative integer");
      options.seed = seed;
    } else if (arg == "--scale" && has_value) {
      std::uint64_t scale = 0;
      if (!parse_u64(argv[++i], scale) || scale < 1 || scale > 8) return usage_error("--scale expects 1 to 8");
      options.internal_scale = static_cast<std::uint32_t>(scale);
    } else if (arg == "--title" && has_value) {
      const std::optional<TitleId> title = parse_title_id(argv[++i]);
      if (!title) return usage_error("--title expects a title ID such as mp4");
      options.title = title;
    } else if (arg == "--dump" && has_value) {
      options.dump = std::filesystem::path(argv[++i]);
    } else if (arg == "--record" && has_value) {
      options.record = std::filesystem::path(argv[++i]);
    } else if (arg == "--replay" && has_value) {
      options.replay = std::filesystem::path(argv[++i]);
    } else if (arg == "--verify-replay" && has_value) {
      options.verify_replay = std::filesystem::path(argv[++i]);
    } else if (arg == "--user-dir" && has_value) {
      options.user_dir = std::filesystem::path(argv[++i]);
    } else if (arg == "--identify" && has_value) {
      options.identify_path = std::filesystem::path(argv[++i]);
    } else {
      return usage_error("Unknown or incomplete option: " + std::string(arg));
    }
  }
  const int modes = (options.title ? 1 : 0) + (options.replay ? 1 : 0) + (options.verify_replay ? 1 : 0);
  if (modes > 1) return usage_error("--title, --replay and --verify-replay are mutually exclusive");
  if (options.record && !options.title) return usage_error("--record needs --title");
  if (options.embed_save && !options.record) return usage_error("--embed-save needs --record");
  return options;
}

int run(const AppOptions& options, const ShellTitles& titles) {
  if (options.identify_path) return identify(*options.identify_path);
  if (options.verify_replay) return run_verify_replay(options, titles);

  if (options.headless) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
  }
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  std::string error;
  std::optional<UserDirs> dirs = user_dirs(options, error);
  if (!dirs) {
    std::fprintf(stderr, "%s\n", error.c_str());
    SDL_Quit();
    return 1;
  }

  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  if (!SDL_CreateWindowAndRenderer("Mario Party Together", 1280, 720, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
    std::fprintf(stderr, "Could not create window: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_SetRenderVSync(renderer, 0);  // the pacer sets the rate, not the display

  std::printf("Mario Party Together %s: SDL %d.%d.%d, video driver '%s', %zu title(s) registered\n", MPT_VERSION,
              SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION, SDL_GetCurrentVideoDriver(), titles.size());

  int code = 0;
  {
    Shell shell(options, titles, std::move(*dirs), renderer);
    code = shell.start_from_options() ? shell.loop() : 1;
  }
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return code;
}

}  // namespace mpt::shell
