#include "mpt/input/sdl_input.h"

#include <algorithm>
#include <string>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

namespace mpt::input {

// Our names and enums mirror SDL's numbering so snapshots are plain copies.
static_assert(SDL_SCANCODE_A == 4 && SDL_SCANCODE_Z == 29 && SDL_SCANCODE_0 == 39);
static_assert(SDL_SCANCODE_SPACE == 44 && SDL_SCANCODE_UP == 82 && SDL_SCANCODE_KP_8 == 96);
static_assert(SDL_SCANCODE_ESCAPE == kHostPauseKey);
static_assert(SDL_GAMEPAD_BUTTON_GUIDE == static_cast<int>(kHostPauseButton));
static_assert(SDL_SCANCODE_LCTRL == 224 && SDL_SCANCODE_LSHIFT == 225 && SDL_SCANCODE_RGUI == 231);
static_assert(static_cast<std::size_t>(SDL_SCANCODE_COUNT) <= kKeyCount);
static_assert(SDL_GAMEPAD_BUTTON_SOUTH == static_cast<int>(PadButton::south));
static_assert(SDL_GAMEPAD_BUTTON_START == static_cast<int>(PadButton::start));
static_assert(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER == static_cast<int>(PadButton::right_shoulder));
static_assert(SDL_GAMEPAD_BUTTON_DPAD_RIGHT == static_cast<int>(PadButton::dpad_right));
static_assert(SDL_GAMEPAD_BUTTON_TOUCHPAD == static_cast<int>(PadButton::touchpad));
static_assert(SDL_GAMEPAD_AXIS_LEFTY == static_cast<int>(PadAxis::left_y));
static_assert(SDL_GAMEPAD_AXIS_LEFT_TRIGGER == static_cast<int>(PadAxis::left_trigger));
static_assert(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER == static_cast<int>(PadAxis::right_trigger));

namespace {

DeviceId gamepad_device(SDL_JoystickID id) { return DeviceId{DeviceKind::gamepad, static_cast<std::uint32_t>(id)}; }

std::string persistent_key(SDL_JoystickID id, SDL_Gamepad* pad) {
  char guid[64] = {};
  SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), guid, sizeof(guid));
  std::string key = guid;
  if (const char* serial = SDL_GetGamepadSerial(pad); serial && *serial) {
    key += "/serial:";
    key += serial;
  } else if (const char* path = SDL_GetGamepadPath(pad); path && *path) {
    key += "/path:";
    key += path;
  }
  return key;
}

}  // namespace

SdlInput::SdlInput(bool auto_assign) : ports_(auto_assign) {
  ports_.device_added(DeviceInfo{kKeyboardDevice, "Keyboard", "keyboard"});
  int count = 0;
  if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
    for (int i = 0; i < count; ++i) open_gamepad(ids[i]);
    SDL_free(ids);
  }
}

SdlInput::~SdlInput() {
  for (const OpenPad& pad : pads_) SDL_CloseGamepad(pad.handle);
}

void SdlInput::open_gamepad(SDL_JoystickID id) {
  if (std::any_of(pads_.begin(), pads_.end(), [&](const OpenPad& p) { return p.id == id; })) return;
  SDL_Gamepad* handle = SDL_OpenGamepad(id);
  if (!handle) return;
  pads_.push_back(OpenPad{id, handle});
  const char* name = SDL_GetGamepadName(handle);
  ports_.device_added(DeviceInfo{gamepad_device(id), name ? name : "Gamepad", persistent_key(id, handle)});
}

void SdlInput::close_gamepad(SDL_JoystickID id) {
  const auto it = std::find_if(pads_.begin(), pads_.end(), [&](const OpenPad& p) { return p.id == id; });
  if (it == pads_.end()) return;
  SDL_CloseGamepad(it->handle);
  pads_.erase(it);
  ports_.device_removed(gamepad_device(id));
}

void SdlInput::handle_event(const SDL_Event& event) {
  switch (event.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
      open_gamepad(event.gdevice.which);
      break;
    case SDL_EVENT_GAMEPAD_REMOVED:
      close_gamepad(event.gdevice.which);
      break;
    default:
      break;
  }
}

std::vector<DeviceSnapshot> SdlInput::snapshot() const {
  std::vector<DeviceSnapshot> out;
  out.reserve(pads_.size() + 1);

  DeviceSnapshot keyboard;
  keyboard.id = kKeyboardDevice;
  int key_count = 0;
  if (const bool* keys = SDL_GetKeyboardState(&key_count)) {
    const int n = std::min(key_count, static_cast<int>(kKeyCount));
    for (int k = 0; k < n; ++k) {
      if (keys[k]) keyboard.keyboard.set(static_cast<std::size_t>(k));
    }
  }
  out.push_back(keyboard);

  for (const OpenPad& pad : pads_) {
    DeviceSnapshot s;
    s.id = gamepad_device(pad.id);
    for (std::size_t b = 0; b < kPadButtonCount; ++b) {
      s.gamepad.set(static_cast<PadButton>(b), SDL_GetGamepadButton(pad.handle, static_cast<SDL_GamepadButton>(b)));
    }
    for (std::size_t a = 0; a < kPadAxisCount; ++a) {
      s.gamepad.set(static_cast<PadAxis>(a), SDL_GetGamepadAxis(pad.handle, static_cast<SDL_GamepadAxis>(a)));
    }
    out.push_back(s);
  }
  return out;
}

std::optional<HostAction> SdlInput::host_action(const SDL_Event& event) {
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    const auto code = static_cast<int>(event.key.scancode);
    if (code < 0 || code >= static_cast<int>(kKeyCount)) return std::nullopt;
    return host_action_for_key(static_cast<std::uint16_t>(code));
  }
  if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    if (static_cast<std::size_t>(event.gbutton.button) >= kPadButtonCount) return std::nullopt;
    return host_action_for_button(static_cast<PadButton>(event.gbutton.button));
  }
  return std::nullopt;
}

InputFrame SdlInput::sample(std::uint64_t frame) {
  std::vector<DeviceSnapshot> devices = snapshot();
  strip_reserved(devices);
  held_.apply(devices);
  return mapper_.map(frame, devices, ports_.layout());
}

void SdlInput::mask_held_until_release() { held_.capture(snapshot()); }

}  // namespace mpt::input
