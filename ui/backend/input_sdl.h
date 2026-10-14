#pragma once

// SDL input to RmlUi input for the launcher (ADR 0010 §5). Keyboard and mouse go to
// RmlUi as they are; gamepads are turned into navigation keys by the launcher with
// NavRepeat. Host side only: nothing here reaches an InputFrame.

#include <optional>

#include <RmlUi/Core/Input.h>
#include <SDL3/SDL_keyboard.h>

#include "mpt/ui/model/nav_repeat.h"

namespace mpt::ui {

// Keys RmlUi navigates and activates with (arrows, Tab, Enter, Space, Home/End,
// Page Up/Down). Others are not passed on.
std::optional<Rml::Input::KeyIdentifier> rml_key(SDL_Keycode key);
int rml_modifiers(SDL_Keymod mods);
Rml::Input::KeyIdentifier rml_key(NavDir dir);

// True while any key or any gamepad button is down: resume waits for it (ADR 0010
// §3.3), so the "Resume" press never reaches the game.
bool any_input_held();

}  // namespace mpt::ui
