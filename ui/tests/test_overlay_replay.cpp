// The ADR 0010 bring-up gate (P1-23), against FakeRuntime:
//  1. the RmlUi render interface draws the launcher on the shell's SDL_Renderer;
//  2. the pause screen is navigated by (synthetic) gamepad D-pad, stick and buttons;
//  3. a replay's hash trail is identical with the real launcher on and off, including
//     a pause and resume mid-run (ADR 0010 §3.5).
//
// SDL runs with the dummy video driver and the software renderer, so no display is
// needed. Events are pushed into SDL's queue and routed with the shell's own route_event
// and GameInputGate (core/shell/event_routing.h).

#include <doctest/doctest.h>

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "event_routing.h"
#include "focus_rml.h"
#include "frame_loop.h"
#include "mpt/core/replay.h"
#include "mpt/input/sdl_input.h"
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
  // What app.cpp reports from the Director (M4-06), kept after a stop.
  PartyPhase phase = PartyPhase::none;
  std::int32_t turn = 0;
  std::int32_t max_turns = 0;
  std::filesystem::path replay_path;
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

struct Harness {
  SdlVideo& video;
  TestShell shell;
  input::SdlInput input;  // the shell's input: seating for the launcher, routing below
  std::filesystem::path user_dir;
  std::unique_ptr<IOverlay> overlay;
  std::size_t bright_pixels = 0;  // from the last draw() with read-back

  explicit Harness(SdlVideo& v) : video(v) {
    user_dir = std::filesystem::temp_directory_path() / "mpt_ui_overlay_tests";
    std::filesystem::remove_all(user_dir);
    UiHost host;
    host.commands = &shell;
    host.window = video.window;
    host.ports = &input.ports();
    host.titles = {TitleId::mp4};
    host.user_dir = user_dir;
    host.cache_dir = user_dir / "cache";
    host.status = [this] {
      SessionStatus s;
      s.running = shell.running;
      s.paused = shell.paused;
      s.phase = shell.phase;
      s.turn = shell.turn;
      s.max_turns = shell.max_turns;
      s.replay_path = shell.replay_path;
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
// `after_tick` runs after each tick's overlay update.
std::vector<ReplayCheckpoint> play(const Replay& replay, Harness* ui,
                                   const std::map<std::uint64_t, std::vector<SDL_Event>>& script,
                                   std::vector<std::uint64_t>* frames_at_tick = nullptr,
                                   const std::function<void(std::uint64_t)>& after_tick = {}) {
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
    if (ui) {
      // As app.cpp fills SessionStatus from the Director.
      const PartySnapshot& party = s.director().party();
      ui->shell.phase = party.phase;
      ui->shell.turn = party.turn;
      ui->shell.max_turns = party.max_turns;
    }
  };
  FrameLoop loop(*session, std::move(hooks));
  IOverlay* overlay = ui ? ui->overlay.get() : nullptr;
  std::optional<input::SdlInput> own_input;
  if (!ui) own_input.emplace();
  input::SdlInput& input = ui ? ui->input : *own_input;
  GameInputGate gate;
  if (ui) {
    ui->shell.running = true;
    ui->shell.replay_path = ui->user_dir / "replays" / "fake-42-0.mptr";
  }
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
    // Routing and the input gate are the shell's own (event_routing.h), as in app.cpp.
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (route_event(e, overlay, input) == EventOutcome::toggle_pause) {
        loop.paused() ? loop.resume() : loop.pause();
      }
    }
    const bool focus = overlay && overlay->hasFocus();
    gate.update(!loop.paused() && !focus, input);
    const FrameLoop::Tick result = loop.tick(focus);
    if (overlay) {
      overlay->update(tick * kTickNs);
      ui->draw(/*read_back=*/false);
    }
    if (after_tick) after_tick(tick);
    if (frames_at_tick) frames_at_tick->push_back(loop.frames_stepped());
    if (result == FrameLoop::Tick::input_ended) break;
    REQUIRE(result != FrameLoop::Tick::failed);
  }
  return trail;
}

// The launcher's documents, read back through RmlUi (its context is named "launcher").
// Data bindings are evaluated by the overlay's update().
Rml::ElementDocument* shown_document() {
  Rml::Context* context = Rml::GetContext("launcher");
  if (!context) return nullptr;
  for (int i = 0; i < context->GetNumDocuments(); ++i) {
    Rml::ElementDocument* doc = context->GetDocument(i);
    if (doc->IsVisible()) return doc;
  }
  return nullptr;
}

std::string shown_title() {
  Rml::ElementDocument* doc = shown_document();
  return doc ? doc->GetTitle() : std::string();
}

std::string shown_rml() {
  Rml::ElementDocument* doc = shown_document();
  return doc ? doc->GetInnerRML() : std::string();
}

std::string focused_rml() {
  Rml::Context* context = Rml::GetContext("launcher");
  Rml::Element* focus = context ? context->GetFocusElement() : nullptr;
  return focus ? focus->GetInnerRML() : std::string();
}

// True if a visible heading or paragraph of the shown document contains `needle`.
bool shows(const std::string& needle) {
  Rml::ElementDocument* doc = shown_document();
  if (!doc) return false;
  for (const char* selector : {"h1", "p"}) {
    Rml::ElementList elements;
    doc->QuerySelectorAll(elements, selector);
    for (Rml::Element* el : elements) {
      if (el->IsVisible(true) && el->GetInnerRML().find(needle) != std::string::npos) return true;
    }
  }
  return false;
}

// A few more launcher updates after the session loop, as the shell's main loop keeps
// doing between sessions.
void settle(Harness& ui, std::uint64_t from_tick) {
  for (std::uint64_t t = 0; t < 3; ++t) {
    ui.overlay->update((from_tick + t) * kTickNs);
    ui.draw(/*read_back=*/false);
  }
}

std::pair<const std::uint64_t, std::vector<SDL_Event>> press(std::uint64_t tick, SDL_GamepadButton button) {
  return {tick, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, button)}};
}
std::pair<const std::uint64_t, std::vector<SDL_Event>> release(std::uint64_t tick, SDL_GamepadButton button) {
  return {tick, {pad_button(SDL_EVENT_GAMEPAD_BUTTON_UP, button)}};
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

// M4-09: a FakeRuntime party walked through the launcher's E1 path by controller:
// "Turn x/y" on the pause screen, then the post-session screen after Quit to library.
// FakeRuntime has 3 turns: setup at frame 10, board turn 1 at 60, turn 2 at 160, results
// at 360 and finished at 410 (tests/support/fake_party_script.h).
TEST_CASE("walkthrough: Turn x/y while paused, then the post-session screen") {
  SdlVideo video;
  const Replay replay = record_party();
  const std::vector<ReplayCheckpoint> plain = play(replay, nullptr, {});
  REQUIRE(plain == replay.checkpoints);
  // A session stopped early has played a prefix of the UI-off trail.
  const auto prefix_of_plain = [&](const std::vector<ReplayCheckpoint>& trail) {
    return !trail.empty() && trail.size() <= plain.size() && std::equal(trail.begin(), trail.end(), plain.begin());
  };

  SUBCASE("a finished party shows Party finished and the replay path") {
    Harness ui(video);
    const std::map<std::uint64_t, std::vector<SDL_Event>> script = {
        press(180, SDL_GAMEPAD_BUTTON_GUIDE), release(181, SDL_GAMEPAD_BUTTON_GUIDE),
        press(190, SDL_GAMEPAD_BUTTON_EAST),  release(191, SDL_GAMEPAD_BUTTON_EAST),  // resume
        // After the party has finished: pause, down to "Quit to library", south.
        press(450, SDL_GAMEPAD_BUTTON_GUIDE), release(451, SDL_GAMEPAD_BUTTON_GUIDE),
        press(460, SDL_GAMEPAD_BUTTON_DPAD_DOWN), release(461, SDL_GAMEPAD_BUTTON_DPAD_DOWN),
        press(470, SDL_GAMEPAD_BUTTON_SOUTH), release(471, SDL_GAMEPAD_BUTTON_SOUTH),
    };
    std::string paused_title;
    bool turn_shown = false;
    std::string ending_title;
    const std::vector<ReplayCheckpoint> trail = play(replay, &ui, script, nullptr, [&](std::uint64_t tick) {
      if (tick == 185) {
        paused_title = shown_title();
        turn_shown = shows("Turn 2/3");
      }
      // Quit chosen; the shell applies the stop on its next iteration.
      if (ui.shell.pending_stop && ending_title.empty()) ending_title = shown_title();
    });
    CHECK(paused_title == "Paused");
    CHECK(turn_shown);
    CHECK(ending_title == "Party over");
    CHECK(prefix_of_plain(trail));
    CHECK(ui.shell.pauses == 2);
    CHECK(ui.shell.resumes == 1);
    CHECK(ui.shell.stops == 1);
    CHECK(ui.shell.phase == PartyPhase::finished);

    settle(ui, 20 * kFrames);
    CHECK(shown_title() == "Party over");
    CHECK(shows("Party finished"));
    CHECK(shows("Turns played: 3 of 3."));
    CHECK(shows("Replay saved in your user folder: replays/fake-42-0.mptr"));
    // Relative to the user dir, and no other path.
    const std::string rml = shown_rml();
    CHECK(rml.find("mpt_ui_overlay_tests") == std::string::npos);
    CHECK(rml.find(ui.user_dir.string()) == std::string::npos);
    CHECK(focused_rml().find("Back to library") != std::string::npos);

    // East goes back to the library.
    const SDL_Event east = pad_button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_EAST);
    CHECK(ui.overlay->handleEvent(east));
    settle(ui, 20 * kFrames + 3);
    CHECK(shown_title() == "Library");
  }

  SUBCASE("a party quit mid-board shows the phase it ended in") {
    Harness ui(video);
    const std::map<std::uint64_t, std::vector<SDL_Event>> script = {
        press(180, SDL_GAMEPAD_BUTTON_GUIDE), release(181, SDL_GAMEPAD_BUTTON_GUIDE),
        press(190, SDL_GAMEPAD_BUTTON_DPAD_DOWN), release(191, SDL_GAMEPAD_BUTTON_DPAD_DOWN),
        press(200, SDL_GAMEPAD_BUTTON_SOUTH), release(201, SDL_GAMEPAD_BUTTON_SOUTH),  // Quit to library
    };
    CHECK(prefix_of_plain(play(replay, &ui, script)));
    CHECK(ui.shell.stops == 1);
    CHECK(ui.shell.phase == PartyPhase::board);
    CHECK(ui.shell.turn == 2);
    settle(ui, 20 * kFrames);
    CHECK(shown_title() == "Party over");
    CHECK(shows("The game session ended"));
    CHECK(shows("It ended in this phase: board (turn 2 of 3)."));
    CHECK(shows("replays/fake-42-0.mptr"));
    CHECK_FALSE(shows("Party finished"));
    // Back with the confirm button on "Back to library".
    const SDL_Event south = pad_button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_SOUTH);
    const SDL_Event south_up = pad_button(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_SOUTH);
    ui.overlay->handleEvent(south);
    ui.overlay->handleEvent(south_up);
    settle(ui, 20 * kFrames + 3);
    CHECK(shown_title() == "Library");
  }

  SUBCASE("the pause screen leaves the turn out when the title reports no max_turns") {
    Harness ui(video);
    const std::map<std::uint64_t, std::vector<SDL_Event>> script = {
        press(180, SDL_GAMEPAD_BUTTON_GUIDE), release(181, SDL_GAMEPAD_BUTTON_GUIDE),
        press(190, SDL_GAMEPAD_BUTTON_EAST), release(191, SDL_GAMEPAD_BUTTON_EAST),
    };
    bool any_turn = true;
    std::string title;
    const std::vector<ReplayCheckpoint> trail = play(replay, &ui, script, nullptr, [&](std::uint64_t tick) {
      ui.shell.max_turns = 0;  // as a title that does not report it (read while paused: no step resets it)
      if (tick == 185) {
        title = shown_title();
        any_turn = shows("Turn ");
      }
    });
    CHECK(title == "Paused");
    CHECK_FALSE(any_turn);
    CHECK(trail == plain);  // played to the end: the whole UI-off trail
  }
}

// ---- Focus navigation: every focusable element of every screen is reachable ----------

namespace {

// The focused button's text, without the markup of any spans inside it.
std::string focused_text() {
  const std::string rml = focused_rml();
  std::string out;
  bool tag = false;
  for (char c : rml) {
    if (c == '<') {
      tag = true;
    } else if (c == '>') {
      tag = false;
    } else if (!tag) {
      out += c;
    }
  }
  return out;
}

void tap(Harness& ui, std::uint64_t& tick, SDL_GamepadButton button) {
  ui.overlay->handleEvent(pad_button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, button));
  ui.overlay->update(tick++ * kTickNs);
  ui.overlay->handleEvent(pad_button(SDL_EVENT_GAMEPAD_BUTTON_UP, button));
  ui.overlay->update(tick++ * kTickNs);
}

// Plain data for the screens, bound under the launcher's names in a context of the
// test's own, so each real document can be shown with any data (a verified game,
// several controllers, a long file list) without a dump.
struct TitleRow {
  Rml::String name, detail, status;
};
struct FileRow {
  Rml::String label, kind;
};
struct DeviceRow {
  Rml::String name, port;
};
struct PortRow {
  Rml::String label, summary;
  bool seated = false;
};
struct ScreenData {
  std::vector<TitleRow> titles;
  bool has_titles = false;
  Rml::String library_message, files_location, files_error;
  std::vector<FileRow> files;
  Rml::String imp_title, imp_phase = "busy", imp_file, imp_stage, imp_width = "0%", imp_heading, imp_message;
  Rml::String seat_title, seat_note, seat_status;
  std::vector<DeviceRow> devices;
  std::vector<PortRow> ports;
  bool seat_ready = false, seat_starting = false, seat_quick = false;
  Rml::String pause_title, pause_turn;
  bool has_turn = false, end_pending = false;
  Rml::String end_heading, end_detail, end_replay;
};

class ScreenContext {
 public:
  ScreenContext() {
    context_ = Rml::CreateContext("focus-test", Rml::Vector2i(1280, 720));
    REQUIRE(context_);
    Rml::DataModelConstructor c = context_->CreateDataModel("ui");
    REQUIRE(static_cast<bool>(c));
    if (auto t = c.RegisterStruct<TitleRow>()) {
      t.RegisterMember("name", &TitleRow::name);
      t.RegisterMember("detail", &TitleRow::detail);
      t.RegisterMember("status", &TitleRow::status);
    }
    if (auto t = c.RegisterStruct<FileRow>()) {
      t.RegisterMember("label", &FileRow::label);
      t.RegisterMember("kind", &FileRow::kind);
    }
    if (auto t = c.RegisterStruct<DeviceRow>()) {
      t.RegisterMember("name", &DeviceRow::name);
      t.RegisterMember("port", &DeviceRow::port);
    }
    if (auto t = c.RegisterStruct<PortRow>()) {
      t.RegisterMember("label", &PortRow::label);
      t.RegisterMember("summary", &PortRow::summary);
      t.RegisterMember("seated", &PortRow::seated);
    }
    c.RegisterArray<std::vector<TitleRow>>();
    c.RegisterArray<std::vector<FileRow>>();
    c.RegisterArray<std::vector<DeviceRow>>();
    c.RegisterArray<std::vector<PortRow>>();
    ScreenData& d = data;
    c.Bind("titles", &d.titles);
    c.Bind("has_titles", &d.has_titles);
    c.Bind("library_message", &d.library_message);
    c.Bind("files_location", &d.files_location);
    c.Bind("files", &d.files);
    c.Bind("files_error", &d.files_error);
    c.Bind("imp_title", &d.imp_title);
    c.Bind("imp_phase", &d.imp_phase);
    c.Bind("imp_file", &d.imp_file);
    c.Bind("imp_stage", &d.imp_stage);
    c.Bind("imp_width", &d.imp_width);
    c.Bind("imp_heading", &d.imp_heading);
    c.Bind("imp_message", &d.imp_message);
    c.Bind("seat_title", &d.seat_title);
    c.Bind("devices", &d.devices);
    c.Bind("ports", &d.ports);
    c.Bind("seat_ready", &d.seat_ready);
    c.Bind("seat_starting", &d.seat_starting);
    c.Bind("seat_note", &d.seat_note);
    c.Bind("seat_status", &d.seat_status);
    c.Bind("seat_quick", &d.seat_quick);
    c.Bind("pause_title", &d.pause_title);
    c.Bind("pause_turn", &d.pause_turn);
    c.Bind("has_turn", &d.has_turn);
    c.Bind("end_pending", &d.end_pending);
    c.Bind("end_heading", &d.end_heading);
    c.Bind("end_detail", &d.end_detail);
    c.Bind("end_replay", &d.end_replay);
    for (const char* name : {"back", "exit_app", "add_dump", "play", "browse_native", "browse_list", "file_open",
                             "import_cancel", "import_retry", "import_library", "import_play", "seat_cycle", "seat_quick",
                             "start_party", "end_library", "resume", "quit_library"}) {
      c.BindEventCallback(name, [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {});
    }
  }
  ~ScreenContext() { Rml::RemoveContext("focus-test"); }
  ScreenContext(const ScreenContext&) = delete;
  ScreenContext& operator=(const ScreenContext&) = delete;

  // Shows one real screen with `data`, laid out, with its first target focused.
  void show(const char* screen) {
    if (doc_) doc_->Close();
    context_->Update();
    const std::filesystem::path file = std::filesystem::path(MPT_UI_SOURCE_DIR) / "screens" / (std::string(screen) + ".rml");
    doc_ = context_->LoadDocument(file.generic_string());
    REQUIRE(doc_);
    doc_->Show();
    context_->Update();
    context_->Update();
    const ui::FocusTargets found = ui::collect_focus_targets(doc_);
    if (!found.elements.empty()) found.elements.front()->Focus(true);
    context_->Update();
  }

  std::string focused() const {
    Rml::Element* f = context_->GetFocusElement();
    return f ? f->GetInnerRML() : std::string();
  }

  // Steps `dir` and lays out again (scrolling moves the boxes).
  bool step(ui::NavDir dir) {
    const bool moved = ui::step_focus(doc_, dir);
    context_->Update();
    return moved;
  }

  // Every target of the shown screen is reachable from each one of them.
  void check_all_reachable(std::size_t expected_targets) {
    const ui::FocusTargets found = ui::collect_focus_targets(doc_);
    REQUIRE(found.targets.size() == expected_targets);
    for (std::size_t i = 0; i < found.targets.size(); ++i) {
      INFO("from target " << i << ": " << found.elements[i]->GetInnerRML());
      CHECK(ui::count_reachable(found.targets, i) == expected_targets);
    }
  }

  ScreenData data;

 private:
  Rml::Context* context_ = nullptr;
  Rml::ElementDocument* doc_ = nullptr;
};

}  // namespace

TEST_CASE("focus: the D-pad reaches every button of every launcher screen") {
  SdlVideo video;
  Harness ui(video);  // initialises RmlUi, the font and the launcher's interfaces
  ScreenContext screens;
  ScreenData& d = screens.data;

  SUBCASE("Players: from the device list to Start, the combination button and Back") {
    d.seat_title = "Test title";
    d.devices = {{"Keyboard", "not playing"}, {"XInput controller #1", "Player 1"}};
    d.ports = {{"Player 1", "XInput controller #1", true},
               {"Player 2", "Computer", false},
               {"Player 3", "Computer", false},
               {"Player 4", "Computer", false}};
    d.seat_ready = true;
    d.seat_quick = true;
    screens.show("seating");
    screens.check_all_reachable(5);
    CHECK(screens.focused().find("Keyboard") != std::string::npos);
    CHECK(screens.step(ui::NavDir::down));
    CHECK(screens.focused().find("XInput controller #1") != std::string::npos);
    CHECK(screens.step(ui::NavDir::down));  // out of the list (the bug: focus stopped here)
    CHECK(screens.focused() == "Start the party");
    CHECK(screens.step(ui::NavDir::right));
    CHECK(screens.focused() == "Keyboard + controller, 2 computer players");
    CHECK(screens.step(ui::NavDir::right));
    CHECK(screens.focused() == "Back");
    CHECK(screens.step(ui::NavDir::up));
    CHECK(screens.focused().find("XInput controller #1") != std::string::npos);
  }

  SUBCASE("Players before the game is ready: devices and Back") {
    d.devices = {{"Keyboard", "Player 1"}};
    screens.show("seating");
    screens.check_all_reachable(2);
  }

  SUBCASE("Library with titles: every title, Add game dump and Exit") {
    d.has_titles = true;
    d.titles = {{"Title A", "USA, Rev 0", "Ready"}, {"Title B", "USA, Rev 0", "Select to check and play"}};
    screens.show("library");
    screens.check_all_reachable(4);
    CHECK(screens.step(ui::NavDir::down));
    CHECK(screens.step(ui::NavDir::down));
    CHECK(screens.focused() == "Add game dump");
    CHECK(screens.step(ui::NavDir::down));
    CHECK(screens.focused() == "Exit");
  }

  SUBCASE("Library without titles, and Add a game dump") {
    screens.show("library");
    screens.check_all_reachable(2);
    screens.show("add_dump");
    screens.check_all_reachable(3);
  }

  SUBCASE("a long file list scrolls, and Back below it is reachable both ways") {
    d.files.push_back({"..", "up"});
    for (int i = 0; i < 30; ++i) d.files.push_back({"dump_" + std::to_string(i) + ".iso", "file"});
    screens.show("files");
    screens.check_all_reachable(32);
    int steps = 0;
    while (screens.focused() != "Back" && steps < 40) {
      REQUIRE(screens.step(ui::NavDir::down));
      ++steps;
    }
    CHECK(steps == 31);  // every row in turn, then Back
    CHECK(screens.step(ui::NavDir::up));
    CHECK(screens.focused().find("dump_29.iso") != std::string::npos);  // the last row, on screen
  }

  SUBCASE("Import: busy, rejected and done") {
    d.imp_phase = "busy";
    screens.show("import");
    screens.check_all_reachable(1);
    d.imp_phase = "rejected";
    screens.show("import");
    screens.check_all_reachable(2);
    d.imp_phase = "done";
    screens.show("import");
    screens.check_all_reachable(2);
    CHECK(screens.focused() == "Set up players");
    CHECK(screens.step(ui::NavDir::down));
    CHECK(screens.focused() == "Back to library");
  }

  SUBCASE("Pause and Party over") {
    screens.show("pause");
    screens.check_all_reachable(3);
    screens.show("session_end");
    screens.check_all_reachable(1);
  }
}

TEST_CASE("focus: the real launcher reaches Back below the file list by D-pad and arrows") {
  SdlVideo video;
  Harness ui(video);
  std::uint64_t tick = 0;
  for (int i = 0; i < 3; ++i) ui.overlay->update(tick++ * kTickNs);
  REQUIRE(shown_title() == "Library");
  CHECK(focused_text() == "Add game dump");
  tap(ui, tick, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  CHECK(focused_text() == "Exit");
  tap(ui, tick, SDL_GAMEPAD_BUTTON_DPAD_UP);
  CHECK(focused_text() == "Add game dump");
  tap(ui, tick, SDL_GAMEPAD_BUTTON_SOUTH);
  REQUIRE(shown_title() == "Add a game dump");
  tap(ui, tick, SDL_GAMEPAD_BUTTON_SOUTH);  // Pick from a list: the drives (or /)
  REQUIRE(shown_title() == "Choose a dump");
  int steps = 0;
  while (focused_text() != "Back" && steps < 64) {
    tap(ui, tick, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    ++steps;
  }
  CHECK(focused_text() == "Back");
  CHECK(steps >= 1);
  // The keyboard's arrows step the same way.
  ui.overlay->handleEvent(key_event(SDL_EVENT_KEY_DOWN, SDLK_UP, SDL_SCANCODE_UP));
  ui.overlay->handleEvent(key_event(SDL_EVENT_KEY_UP, SDLK_UP, SDL_SCANCODE_UP));
  ui.overlay->update(tick++ * kTickNs);
  CHECK(focused_text() != "Back");
  tap(ui, tick, SDL_GAMEPAD_BUTTON_EAST);
  CHECK(shown_title() == "Add a game dump");
}

// The pause screen by controller (bug 6): Start resumes, as in the games, and Back alone
// does nothing there, so holding Back and pressing Start never hands Start to the game.
TEST_CASE("pause screen: Start resumes and Back alone does not") {
  SdlVideo video;
  const Replay replay = record_party();
  const std::vector<ReplayCheckpoint> plain = play(replay, nullptr, {});
  Harness ui(video);
  std::string title_after_back;
  const std::map<std::uint64_t, std::vector<SDL_Event>> script = {
      press(50, SDL_GAMEPAD_BUTTON_GUIDE),   release(51, SDL_GAMEPAD_BUTTON_GUIDE),
      press(60, SDL_GAMEPAD_BUTTON_BACK),    release(61, SDL_GAMEPAD_BUTTON_BACK),
      press(70, SDL_GAMEPAD_BUTTON_BACK),  // held, then Start
      press(72, SDL_GAMEPAD_BUTTON_START),   release(73, SDL_GAMEPAD_BUTTON_START),
      release(74, SDL_GAMEPAD_BUTTON_BACK),
  };
  CHECK(play(replay, &ui, script, nullptr, [&](std::uint64_t tick) {
          if (tick == 65) title_after_back = shown_title();
        }) == plain);
  CHECK(title_after_back == "Paused");
  CHECK(ui.shell.pauses == 1);
  CHECK(ui.shell.resumes == 1);
  CHECK(ui.shell.stops == 0);
}
