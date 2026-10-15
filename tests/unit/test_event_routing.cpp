// The shell's event routing and input gate (core/shell/event_routing.h), headless: SDL
// runs with the dummy video driver and no devices, and events are built in code.

#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <vector>

#include "event_routing.h"
#include "mpt/input/sdl_input.h"
#include "overlay.h"

using namespace mpt;
using namespace mpt::shell;

namespace {

struct SdlHeadless {
  SdlHeadless() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD));
  }
  ~SdlHeadless() { SDL_Quit(); }
};

class RecordingOverlay final : public IOverlay {
 public:
  bool focus = false;
  bool accepts_pause = false;
  std::vector<SDL_EventType> seen;

  bool hasFocus() const override { return focus; }
  bool handleEvent(const SDL_Event& e) override {
    seen.push_back(static_cast<SDL_EventType>(e.type));
    return focus || accepts_pause;
  }
  void update(std::uint64_t) override {}
  void render(SDL_Renderer*, int, int) override {}
};

SDL_Event key_down(SDL_Scancode code, bool repeat = false) {
  SDL_Event e{};
  e.type = SDL_EVENT_KEY_DOWN;
  e.key.scancode = code;
  e.key.down = true;
  e.key.repeat = repeat;
  return e;
}

SDL_Event pad_down(SDL_GamepadButton button) {
  SDL_Event e{};
  e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  e.gbutton.button = static_cast<Uint8>(button);
  e.gbutton.down = true;
  return e;
}

SDL_Event of_type(SDL_EventType type) {
  SDL_Event e{};
  e.type = type;
  return e;
}

}  // namespace

TEST_CASE("route_event: quit, pause action, UI focus and hot-plug") {
  SdlHeadless sdl;
  input::SdlInput input;

  CHECK(route_event(of_type(SDL_EVENT_QUIT), nullptr, input) == EventOutcome::quit);

  SUBCASE("without a UI the pause action toggles pause; Esc repeats and other keys do not") {
    CHECK(route_event(key_down(SDL_SCANCODE_ESCAPE), nullptr, input) == EventOutcome::toggle_pause);
    CHECK(route_event(pad_down(SDL_GAMEPAD_BUTTON_GUIDE), nullptr, input) == EventOutcome::toggle_pause);
    CHECK(route_event(key_down(SDL_SCANCODE_ESCAPE, /*repeat=*/true), nullptr, input) == EventOutcome::none);
    CHECK(route_event(key_down(SDL_SCANCODE_K), nullptr, input) == EventOutcome::none);
    CHECK(route_event(pad_down(SDL_GAMEPAD_BUTTON_SOUTH), nullptr, input) == EventOutcome::none);
  }
  SUBCASE("a UI that takes the pause action gets it, and the shell does not toggle") {
    RecordingOverlay ui;
    ui.accepts_pause = true;
    CHECK(route_event(key_down(SDL_SCANCODE_ESCAPE), &ui, input) == EventOutcome::none);
    REQUIRE(ui.seen.size() == 1);
    CHECK(ui.seen[0] == SDL_EVENT_KEY_DOWN);
    // Without focus, ordinary events go to input/, not to the UI.
    CHECK(route_event(key_down(SDL_SCANCODE_K), &ui, input) == EventOutcome::none);
    CHECK(ui.seen.size() == 1);
  }
  SUBCASE("a UI that declines the pause action leaves it to the shell") {
    RecordingOverlay ui;
    CHECK(route_event(pad_down(SDL_GAMEPAD_BUTTON_GUIDE), &ui, input) == EventOutcome::toggle_pause);
    CHECK(ui.seen.size() == 1);
  }
  SUBCASE("while a screen has focus every event goes to it, the pause action included") {
    RecordingOverlay ui;
    ui.focus = true;
    CHECK(route_event(key_down(SDL_SCANCODE_ESCAPE), &ui, input) == EventOutcome::none);
    CHECK(route_event(key_down(SDL_SCANCODE_K), &ui, input) == EventOutcome::none);
    CHECK(route_event(pad_down(SDL_GAMEPAD_BUTTON_SOUTH), &ui, input) == EventOutcome::none);
    CHECK(ui.seen.size() == 3);
  }
  SUBCASE("hot-plug reaches input/ always and a focused UI too") {
    RecordingOverlay ui;
    SDL_Event removed = of_type(SDL_EVENT_GAMEPAD_REMOVED);
    removed.gdevice.which = 12345;  // no such pad: input/ ignores it
    CHECK(route_event(removed, &ui, input) == EventOutcome::none);
    CHECK(ui.seen.empty());
    ui.focus = true;
    CHECK(route_event(removed, &ui, input) == EventOutcome::none);
    CHECK(ui.seen.size() == 1);
  }
  SUBCASE("window focus gained masks held inputs and is not a pause") {
    CHECK(route_event(of_type(SDL_EVENT_WINDOW_FOCUS_GAINED), nullptr, input) == EventOutcome::none);
    CHECK(input.held_mask().empty());  // nothing is held on the dummy driver
  }
}

TEST_CASE("GameInputGate reports each not-live to live edge once") {
  SdlHeadless sdl;
  input::SdlInput input;
  GameInputGate gate;
  CHECK_FALSE(gate.update(false, input));
  CHECK(gate.update(true, input));   // session start or resume
  CHECK_FALSE(gate.update(true, input));
  CHECK_FALSE(gate.update(false, input));  // paused or a screen took focus
  CHECK_FALSE(gate.update(false, input));
  CHECK(gate.update(true, input));
  CHECK(gate.live());
}
