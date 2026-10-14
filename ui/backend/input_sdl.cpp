#include "input_sdl.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_stdinc.h>

namespace mpt::ui {

std::optional<Rml::Input::KeyIdentifier> rml_key(SDL_Keycode key) {
  using namespace Rml::Input;
  switch (key) {
    case SDLK_UP: return KI_UP;
    case SDLK_DOWN: return KI_DOWN;
    case SDLK_LEFT: return KI_LEFT;
    case SDLK_RIGHT: return KI_RIGHT;
    case SDLK_TAB: return KI_TAB;
    case SDLK_RETURN: return KI_RETURN;
    case SDLK_KP_ENTER: return KI_NUMPADENTER;
    case SDLK_SPACE: return KI_SPACE;
    case SDLK_HOME: return KI_HOME;
    case SDLK_END: return KI_END;
    case SDLK_PAGEUP: return KI_PRIOR;
    case SDLK_PAGEDOWN: return KI_NEXT;
    default: return std::nullopt;
  }
}

Rml::Input::KeyIdentifier rml_key(NavDir dir) {
  switch (dir) {
    case NavDir::up: return Rml::Input::KI_UP;
    case NavDir::down: return Rml::Input::KI_DOWN;
    case NavDir::left: return Rml::Input::KI_LEFT;
    case NavDir::right: return Rml::Input::KI_RIGHT;
  }
  return Rml::Input::KI_UNKNOWN;
}

int rml_modifiers(SDL_Keymod mods) {
  int out = 0;
  if (mods & SDL_KMOD_CTRL) out |= Rml::Input::KM_CTRL;
  if (mods & SDL_KMOD_SHIFT) out |= Rml::Input::KM_SHIFT;
  if (mods & SDL_KMOD_ALT) out |= Rml::Input::KM_ALT;
  return out;
}

bool any_input_held() {
  int count = 0;
  const bool* keys = SDL_GetKeyboardState(&count);
  for (int i = 0; keys && i < count; ++i) {
    if (keys[i]) return true;
  }
  SDL_JoystickID* pads = SDL_GetGamepads(&count);
  bool held = false;
  for (int i = 0; pads && i < count && !held; ++i) {
    SDL_Gamepad* pad = SDL_GetGamepadFromID(pads[i]);  // opened by input/; not owned here
    if (!pad) continue;
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && !held; ++b) {
      held = SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(b));
    }
  }
  SDL_free(pads);
  return held;
}

}  // namespace mpt::ui
