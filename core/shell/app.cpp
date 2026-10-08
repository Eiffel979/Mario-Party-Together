#include "app.h"

#include <SDL3/SDL.h>

#include <charconv>
#include <cstdio>
#include <string_view>
#include <utility>

#include "mpt/asset_import/importer.h"
#include "mpt/core/input.h"
#include "mpt/core/title_registry.h"

namespace mpt::shell {
namespace {

constexpr std::string_view kUsage =
    "Usage: mario-party-together [options]\n"
    "  --headless         Run without a visible window or audio device\n"
    "  --frames N         Exit after N frames\n"
    "  --identify PATH    Identify a game dump against the known-dump list and exit\n"
    "  --version          Print the version and exit\n"
    "  --help             Print this help and exit\n";

constexpr std::uint64_t kFrameNs = 1'000'000'000ull / 60;  // shell presents at 60 Hz

int identify(const std::filesystem::path& path) {
  // known_dumps.json is copied next to the executable at build time.
  const char* base = SDL_GetBasePath();
  const std::filesystem::path db_path = std::filesystem::path(base ? base : "") / "data" / "known_dumps.json";
  std::string error;
  auto known = asset_import::KnownDumps::load(db_path, error);
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

}  // namespace

std::optional<AppOptions> parse_args(int argc, char** argv, std::string& message, int& exit_code) {
  AppOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
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
    } else if (arg == "--frames" && i + 1 < argc) {
      const std::string_view value = argv[++i];
      std::uint64_t frames = 0;
      const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), frames);
      if (ec != std::errc{} || end != value.data() + value.size()) {
        message = "--frames expects a non-negative integer\n";
        exit_code = 2;
        return std::nullopt;
      }
      options.max_frames = frames;
    } else if (arg == "--identify" && i + 1 < argc) {
      options.identify_path = std::filesystem::path(argv[++i]);
    } else {
      message = "Unknown or incomplete option: " + std::string(arg) + "\n" + std::string(kUsage);
      exit_code = 2;
      return std::nullopt;
    }
  }
  return options;
}

int run(const AppOptions& options) {
  if (options.identify_path) return identify(*options.identify_path);

  if (options.headless) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
  }
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  if (!SDL_CreateWindowAndRenderer("Mario Party Together", 1280, 720, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
    std::fprintf(stderr, "Could not create window: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_SetRenderVSync(renderer, options.headless ? 0 : 1);

  // No titles are registered yet (Phase 1 adds MP4). The loop below is the frame
  // scheduler a runtime will be stepped from.
  const TitleRegistry registry;
  std::printf("Mario Party Together %s: SDL %d.%d.%d, video driver '%s', %zu title(s) registered\n", MPT_VERSION,
              SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION, SDL_GetCurrentVideoDriver(),
              registry.titles().size());

  InputFrame input;
  std::uint64_t next_frame_ns = SDL_GetTicksNS();
  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) running = false;
    }
    if (options.max_frames && input.frame >= *options.max_frames) break;

    // Placeholder presenter: an empty frame until a runtime provides FrameOutput.
    SDL_SetRenderDrawColor(renderer, 24, 24, 32, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    ++input.frame;

    if (!options.headless) {
      // Frame pacing uses host time; game logic never sees it (CLAUDE.md rule 4).
      next_frame_ns += kFrameNs;
      const std::uint64_t now = SDL_GetTicksNS();
      if (next_frame_ns > now) {
        SDL_DelayNS(next_frame_ns - now);
      } else {
        next_frame_ns = now;
      }
    }
  }

  std::printf("Shell exited after %llu frame(s)\n", static_cast<unsigned long long>(input.frame));
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

}  // namespace mpt::shell
