// Headless smoke test for the SDL backend: it initialises, registers the
// keyboard on port 0 and produces InputFrames. No input is injected; physical
// devices that happen to be attached are not asserted on.
#include <cstdio>

#include <SDL3/SDL.h>

#include "mpt/input/sdl_input.h"

int main() {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  int failures = 0;
  {
    mpt::input::SdlInput input;
    if (input.ports().port_of(mpt::input::kKeyboardDevice) != 0u) ++failures;

    // A removal for a device we never opened must be ignored.
    SDL_Event removed{};
    removed.type = SDL_EVENT_GAMEPAD_REMOVED;
    removed.gdevice.which = 0x7fffffff;
    input.handle_event(removed);

    // The host pause action, as the shell will ask for it (ADR 0010 §3.2).
    using mpt::input::HostAction;
    using mpt::input::SdlInput;
    SDL_Event esc{};
    esc.type = SDL_EVENT_KEY_DOWN;
    esc.key.scancode = SDL_SCANCODE_ESCAPE;
    if (SdlInput::host_action(esc) != HostAction::pause) ++failures;
    esc.key.repeat = true;  // auto-repeat must not toggle pause again
    if (SdlInput::host_action(esc).has_value()) ++failures;
    esc.key.repeat = false;
    esc.type = SDL_EVENT_KEY_UP;
    if (SdlInput::host_action(esc).has_value()) ++failures;
    SDL_Event enter{};
    enter.type = SDL_EVENT_KEY_DOWN;
    enter.key.scancode = SDL_SCANCODE_RETURN;
    if (SdlInput::host_action(enter).has_value()) ++failures;
    SDL_Event guide{};
    guide.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    guide.gbutton.button = SDL_GAMEPAD_BUTTON_GUIDE;
    if (SdlInput::host_action(guide) != HostAction::pause) ++failures;
    guide.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    if (SdlInput::host_action(guide).has_value()) ++failures;

    input.mask_held_until_release();  // whatever is attached; must not disturb sampling
    for (std::uint64_t frame = 0; frame < 3; ++frame) {
      SDL_PumpEvents();
      const mpt::InputFrame f = input.sample(frame);
      if (f.frame != frame || f.pads[0].connected != 1) ++failures;
    }
  }
  SDL_Quit();
  std::printf("input sdl smoke: %s\n", failures == 0 ? "OK" : "FAILED");
  return failures == 0 ? 0 : 1;
}
