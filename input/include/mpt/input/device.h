#pragma once

// Physical device state as plain data. The SDL backend (sdl_input.h) fills these
// once per frame; everything else in input/ is a pure function of them, so the
// mapping core never touches SDL, clocks or threads.

#include <array>
#include <bitset>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mpt::input {

enum class DeviceKind : std::uint8_t { keyboard, gamepad };

// Identifies one physical device for as long as it stays connected. `instance`
// is backend-assigned (SDL joystick instance ID for gamepads, 0 for the keyboard).
struct DeviceId {
  DeviceKind kind = DeviceKind::keyboard;
  std::uint32_t instance = 0;

  friend bool operator==(const DeviceId&, const DeviceId&) = default;
  friend auto operator<=>(const DeviceId&, const DeviceId&) = default;
};

inline constexpr DeviceId kKeyboardDevice{DeviceKind::keyboard, 0};

// Keys are USB HID keyboard usage IDs (page 0x07), which is also what SDL calls a
// scancode. Scancodes are layout-independent: "key:w" is the key in the W position.
inline constexpr std::size_t kKeyCount = 512;
using KeyboardState = std::bitset<kKeyCount>;

// Positional gamepad buttons, in SDL_GamepadButton order (the SDL backend
// static_asserts this). "south" is A on Xbox, Cross on PlayStation, B on Nintendo.
enum class PadButton : std::uint8_t {
  south,
  east,
  west,
  north,
  back,
  guide,
  start,
  left_stick,
  right_stick,
  left_shoulder,
  right_shoulder,
  dpad_up,
  dpad_down,
  dpad_left,
  dpad_right,
  misc1,
  right_paddle1,
  left_paddle1,
  right_paddle2,
  left_paddle2,
  touchpad,
  count,
};
inline constexpr std::size_t kPadButtonCount = static_cast<std::size_t>(PadButton::count);

// Gamepad axes in SDL_GamepadAxis order and with SDL's ranges: sticks are
// -32768..32767 with **positive Y pointing down**; triggers are 0..32767.
enum class PadAxis : std::uint8_t { left_x, left_y, right_x, right_y, left_trigger, right_trigger, count };
inline constexpr std::size_t kPadAxisCount = static_cast<std::size_t>(PadAxis::count);

struct GamepadState {
  std::uint32_t buttons = 0;  // bit i = PadButton i
  std::array<std::int16_t, kPadAxisCount> axes{};

  bool pressed(PadButton b) const { return (buttons >> static_cast<unsigned>(b)) & 1u; }
  std::int16_t axis(PadAxis a) const { return axes[static_cast<std::size_t>(a)]; }
  void set(PadButton b, bool down) {
    const std::uint32_t bit = 1u << static_cast<unsigned>(b);
    buttons = down ? (buttons | bit) : (buttons & ~bit);
  }
  void set(PadAxis a, std::int16_t value) { axes[static_cast<std::size_t>(a)] = value; }
};

// One device's state for one frame. Only the member matching `id.kind` is read.
struct DeviceSnapshot {
  DeviceId id;
  KeyboardState keyboard;
  GamepadState gamepad;
};

// Descriptive data the UI and the port assigner need. `persistent_key` should be
// stable across unplug/replug of the same controller (the SDL backend uses the
// device GUID plus its path or serial) so it can return to its old port.
struct DeviceInfo {
  DeviceId id;
  std::string name;
  std::string persistent_key;
};

}  // namespace mpt::input
