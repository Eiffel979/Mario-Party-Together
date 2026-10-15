// The ADR 0010 bring-up gate (P1-23), against FakeRuntime:
//  1. the RmlUi render interface draws the launcher on the shell's SDL_Renderer;
//  2. the pause screen is navigated by (synthetic) gamepad D-pad, stick and buttons;
//  3. a replay's hash trail is identical with the real launcher on and off, including
//     a pause and resume mid-run (ADR 0010 §3.5).
//
// SDL runs with the dummy video driver and the software renderer, so no display is
// needed. Events are pushed into SDL's queue and routed the way core/shell/app.cpp
// routes them.

#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <variant>
#include <vector>

#include "frame_loop.h"
#include "mpt/core/replay.h"
#include "mpt/input/ports.h"
#include "mpt/ui/launcher.h"
#include "overlay.h"
#include "shell/fake_title.h"
#include "support/fake_party_script.h"
#include "ui_host.h"

using namespace mpt;
using namespace mpt::shell;

namespace {

constexpr std::uint64_t kFrames = 500;
constexpr std::uint64_t kTickNs = 16'666'667;
// Small, so the software renderer keeps the test quick; the layout scales with height.
constexpr int kWidth = 640;
constexpr int kHeight = 360;

struct SdlVideo {
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SdlVideo() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD));
    REQUIRE(SDL_CreateWindowAndRenderer("ui test", kWidth, kHeight, 0, &window, &renderer));
  }
  ~SdlVideo() {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
  }
};

TitleRegistry fake_registry() {
  TitleRegistry registry;
  const ShellTitle title = test::fake_shell_title();
  REQUIRE(registry.add(title.id, title.runtime, title.adapter).ok());
  return registry;
}

std::unique_ptr<Session> start_ok(const TitleRegistry& registry, const SessionConfig& config) {
  auto started = Session::start(registry, config);
  if (const Result* failure = std::get_if<Result>(&started)) FAIL(failure->error());
  return std::move(std::get<std::unique_ptr<Session>>(started));
}

Replay record_party() {
  const TitleRegistry registry = fake_registry();
  SessionConfig config;
  config.dump = test::fake_shell_dump();
  config.runtime.seed = 42;
  auto header = make_replay_header(config, false);
  REQUIRE(std::holds_alternative<ReplayHeader>(header));
  ReplayRecorder recorder(std::get<ReplayHeader>(header));
  auto session = start_ok(registry, config);
  FrameLoopHooks hooks;
  hooks.sample = [](std::uint64_t f) -> std::optional<InputFrame> {
    if (f >= kFrames) return std::nullopt;
    return test::fake_party_script(f);
  };
  FrameLoop loop(*session, std::move(hooks), &recorder);
  while (loop.tick(false) == FrameLoop::Tick::stepped) {
  }
  return recorder.finish();
}

// The shell side the launcher talks to: requests are queued and applied at the top of
// the next iteration, as in core/shell/app.cpp.
class TestShell final : public IShellCommands {
 public:
  void requestStart(const StartRequest&) override { ++starts; }
  void requestPause() override { pending_pause = true; }
  void requestResume() override { pending_resume = true; }
  void requestStop() override { pending_stop = true; }
  void requestQuit() override { quit = true; }

  bool pending_pause = false;
  bool pending_resume = false;
  bool pending_stop = false;
  bool quit = false;
  int starts = 0;
  int pauses = 0;
  int resumes = 0;
  int stops = 0;
  bool running = false;
  bool paused = false;
};

SDL_Event key_event(SDL_EventType type, SDL_Keycode key, SDL_Scancode scancode) {
  SDL_Event e{};
  e.type = type;
  e.key.key = key;
  e.key.scancode = scancode;
  e.key.down = type == SDL_EVENT_KEY_DOWN;
  return e;
}

SDL_Event pad_button(SDL_EventType type, SDL_GamepadButton button) {
  SDL_Event e{};
  e.type = type;
  e.gbutton.which = 77;  // a synthetic pad: the launcher never queries it
  e.gbutton.button = static_cast<Uint8>(button);
  e.gbutton.down = type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  return e;
}

SDL_Event pad_axis(SDL_GamepadAxis axis, Sint16 value) {
  SDL_Event e{};
  e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
  e.gaxis.which = 77;
  e.gaxis.axis = static_cast<Uint8>(axis);
  e.gaxis.value = value;
  return e;
}

// TODO(P1-23 follow-up): replace this copy of the shell's pause binding and the routing
// in play() with architect's shared route_event in mpt_shell_core once it lands.
bool is_host_pause_action(const SDL_Event& e) {
  if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_ESCAPE) return true;
  if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) return true;
  return false;
}

struct Harness {
  SdlVideo& video;
  TestShell shell;
  input::PortAssigner ports;
  std::filesystem::path user_dir;
  std::unique_ptr<IOverlay> overlay;
  std::size_t bright_pixels = 0;  // from the last draw() with read-back

  explicit Harness(SdlVideo& v) : video(v) {
    ports.device_added(input::DeviceInfo{input::kKeyboardDevice, "Keyboard", "keyboard"});
    user_dir = std::filesystem::temp_directory_path() / "mpt_ui_overlay_tests";
    std::filesystem::remove_all(user_dir);
    UiHost host;
    host.commands = &shell;
    host.window = video.window;
    host.ports = &ports;
    host.titles = {TitleId::mp4};
    host.user_dir = user_dir;
    host.cache_dir = user_dir / "cache";
    host.status = [this] {
      SessionStatus s;
      s.running = shell.running;
      s.paused = shell.paused;
      return s;
    };
    overlay = ui::make_launcher(host, ui::LauncherOptions{std::filesystem::path(MPT_UI_SOURCE_DIR)});
    REQUIRE(overlay);
  }

  // One presented frame of the launcher: clear, overlay pass, read back. Returns the
  // number of distinct colours drawn.
  std::size_t draw(bool read_back = true) {
    SDL_SetRenderDrawColor(video.renderer, 0, 0, 0, 255);
    SDL_RenderClear(video.renderer);
    overlay->render(video.renderer, kWidth, kHeight);
    if (!read_back) {
      SDL_RenderPresent(video.renderer);
      return 0;
    }
    SDL_Surface* shot = SDL_RenderReadPixels(video.renderer, nullptr);
    std::set<std::uint32_t> colours;
    bright_pixels = 0;
    if (shot) {
      // For a manual look at the layout: MPT_UI_SCREENSHOT=<file.bmp>.
      if (const char* out = SDL_getenv("MPT_UI_SCREENSHOT")) SDL_SaveBMP(shot, out);
      SDL_Surface* rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
      for (int y = 0; rgba && y < rgba->h; y += 3) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(rgba->pixels) + y * rgba->pitch);
        for (int x = 0; x < rgba->w; x += 3) {
          colours.insert(row[x]);
          // Text is the only near-white ink in the theme (#f2efe6): bright pixels mean
          // the font loaded and glyph textures were drawn.
          const auto* px = reinterpret_cast<const std::uint8_t*>(&row[x]);
          if (px[0] > 190 && px[1] > 190 && px[2] > 190) ++bright_pixels;
        }
      }
      SDL_DestroySurface(rgba);
      SDL_DestroySurface(shot);
    }
    SDL_RenderPresent(video.renderer);
    return colours.size();
  }
};

void drain_events() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
  }
}

// Plays `replay` the way the shell does. `script` maps a host tick to the events pushed
// before that tick's poll. Returns the trail; `stepped_at` records frames per tick.
std::vector<ReplayCheckpoint> play(const Replay& replay, Harness* ui,
                                   const std::map<std::uint64_t, std::vector<SDL_Event>>& script,
                                   std::vector<std::uint64_t>* frames_at_tick = nullptr) {
  const TitleRegistry registry = fake_registry();
  auto config = session_config_for(replay.header, test::fake_shell_dump());
  REQUIRE(std::holds_alternative<SessionConfig>(config));
  auto session = start_ok(registry, std::get<SessionConfig>(config));
  ReplayReader reader(replay);
  std::vector<ReplayCheckpoint> trail;
  FrameLoopHooks hooks;
  hooks.sample = [&](std::uint64_t) -> std::optional<InputFrame> {
    InputFrame in;
    if (!reader.next(in)) return std::nullopt;
    return in;
  };
  hooks.after_step = [&](Session& s) {
    if (s.frame() % replay.header.checkpoint_interval == 0) trail.push_back(checkpoint_of(s));
  };
  FrameLoop loop(*session, std::move(hooks));
  IOverlay* overlay = ui ? ui->overlay.get() : nullptr;
  if (ui) ui->shell.running = true;
  drain_events();

  for (std::uint64_t tick = 0; tick < 20 * kFrames; ++tick) {
    if (ui) {
      TestShell& s = ui->shell;
      if (s.pending_stop) {
        s.pending_stop = false;
        ++s.stops;
        s.running = false;
        break;  // the session ends: the rest of the replay is not played
      }
      if (s.pending_pause) {
        s.pending_pause = false;
        ++s.pauses;
        loop.pause();
      }
      if (s.pending_resume) {
        s.pending_resume = false;
        ++s.resumes;
        loop.resume();
      }
      s.paused = loop.paused();
    }
    if (const auto it = script.find(tick); it != script.end()) {
      for (SDL_Event e : it->second) REQUIRE(SDL_PushEvent(&e));
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (!overlay) continue;
      if (overlay->hasFocus()) {
        overlay->handleEvent(e);
      } else if (is_host_pause_action(e)) {
        if (!overlay->handleEvent(e)) loop.paused() ? loop.resume() : loop.pause();
      }
    }
    const bool focus = overlay && overlay->hasFocus();
    const FrameLoop::Tick result = loop.tick(focus);
    if (overlay) {
      overlay->update(tick * kTickNs);
      ui->draw(/*read_back=*/false);
    }
    if (frames_at_tick) frames_at_tick->push_back(loop.frames_stepped());
    if (result == FrameLoop::Tick::input_ended) break;
    REQUIRE(result != FrameLoop::Tick::failed);
  }
  return trail;
}

}  // namespace

TEST_CASE("bring-up gate: the launcher renders, is gamepad-navigable and leaves the trail unchanged") {
  SdlVideo video;
  const Replay replay = record_party();
  REQUIRE(replay.frame_count == kFrames);
  const std::vector<ReplayCheckpoint> plain = play(replay, nullptr, {});
  REQUIRE(plain == replay.checkpoints);

  SUBCASE("the library screen draws through the SDL_Renderer interface") {
    Harness ui(video);
    CHECK(ui.overlay->hasFocus());  // no session: the launcher has focus
    for (std::uint64_t t = 0; t < 5; ++t) ui.overlay->update(t * kTickNs);
    // Background, panel, focus ring and anti-aliased text give many colours; an
    // interface that drew nothing would leave one.
    CHECK(ui.draw() > 20);
    CHECK(ui.bright_pixels > 10);
  }

  SUBCASE("pause with Esc, navigate with the D-pad, resume with south: same trail") {
    Harness ui(video);
    std::vector<std::uint64_t> frames;
    const std::map<std::uint64_t, std::vector<SDL_Event>> script = {
        {100, {key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)}},
        {101, {key_event(SDL_EVENT_KEY_UP, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)}},
        {120, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN)}},
        {121, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN)}},
        {130, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_DPAD_UP)}},
        {131, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_DPAD_UP)}},
        {140, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_SOUTH)}},
        {141, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_SOUTH)}},
    };
    CHECK(play(replay, &ui, script, &frames) == plain);
    CHECK(ui.shell.pauses == 1);
    CHECK(ui.shell.resumes == 1);
    CHECK(ui.shell.stops == 0);
    // Nothing was stepped while the pause screen had focus.
    REQUIRE(frames.size() > 140);
    CHECK(frames[139] == frames[101]);
    CHECK(frames.back() == kFrames);
  }

  SUBCASE("the D-pad moves focus: down then south chooses Quit to library") {
    Harness ui(video);
    const std::map<std::uint64_t, std::vector<SDL_Event>> script = {
        {50, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_GUIDE)}},
        {51, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_GUIDE)}},
        {60, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN)}},
        {61, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN)}},
        {70, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_SOUTH)}},
        {71, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_SOUTH)}},
    };
    play(replay, &ui, script);
    CHECK(ui.shell.pauses == 1);
    CHECK(ui.shell.resumes == 0);
    CHECK(ui.shell.stops == 1);
    CHECK(ui.overlay->hasFocus());  // back on the library
  }

  SUBCASE("the left stick moves focus too, and East on the pause screen resumes") {
    Harness ui(video);
    const std::map<std::uint64_t, std::vector<SDL_Event>> script = {
        {50, {key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)}},
        {51, {key_event(SDL_EVENT_KEY_UP, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)}},
        {60, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, 30000)}},  // down
        {61, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, 0)}},
        {62, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, 30000)}},  // down again: Exit
        {63, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, 0)}},
        {64, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, -30000)}},  // up: Quit to library
        {65, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, 0)}},
        {66, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, -30000)}},  // up: Resume
        {67, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, 0)}},
        {68, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, 30000)}},  // down: Quit to library
        {69, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, 0)}},
        {80, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_EAST)}},  // back = resume
        {81, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_EAST)}},
    };
    CHECK(play(replay, &ui, script) == plain);
    CHECK(ui.shell.pauses == 1);
    CHECK(ui.shell.resumes == 1);
    CHECK(ui.shell.stops == 0);
    CHECK(!ui.shell.quit);
  }

  SUBCASE("the stick then south confirms the focused item") {
    Harness ui(video);
    const std::map<std::uint64_t, std::vector<SDL_Event>> script = {
        {50, {key_event(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)}},
        {60, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, 30000)}},
        {61, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, 0)}},
        {62, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, 30000)}},
        {63, {pad_axis(SDL_GAMEPAD_AXIS_LEFTY, 0)}},
        {70, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_SOUTH)}},  // Exit
        {71, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_SOUTH)}},
    };
    play(replay, &ui, script);
    CHECK(ui.shell.quit);
  }
}
