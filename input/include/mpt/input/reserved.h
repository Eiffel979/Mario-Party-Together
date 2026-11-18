#pragma once

// Host-reserved bindings (ADR 0010 §3.2, review amendment 3). These physical
// inputs belong to the host, not to any title: they never reach an InputFrame,
// so they can never be recorded in a replay or sent over netplay.
//
//   Esc (keyboard)      host pause action
//   Guide (gamepad)     host pause action
//
// A gamepad also pauses with a chord, for pads whose Guide never reaches the
// window (XInput pads where the Windows Game Bar takes Guide, bug 6): hold Back,
// then press Start, on the same pad. Neither button is reserved on its own: a
// plain Start or Back press reaches the game, so the chord is not a reserved
// source and nothing below strips it. Order matters:
//   - Start down while Back is already held  -> pause; the shell consumes the
//     Start press, the game is paused before the next sample(), and the resume
//     masks the still-held Back and Start until release (held_mask.h).
//   - Back down while Start is already held  -> no host action. The game has
//     already sampled Start (MP4 opens its own pause menu on it), so pausing
//     here would open both menus. Both landing in one event poll counts as
//     Back-first, because the Start event sees Back held in the polled state.
// While Back is held before Start it does reach the game, so shipped title
// profiles leave `button:back` unbound (the built-in default too); a user
// profile may bind it, at the cost of that control being pressed while the
// chord is formed.
//
// Enforced three ways:
//   1. parse_profile rejects a profile that binds a reserved source;
//   2. the mapper ignores reserved sources even in a Profile built in code;
//   3. SdlInput::sample clears them from the device snapshots before mapping.
// The shell asks SdlInput::host_action(event) whether an event is the pause action.

#include <cstdint>
#include <optional>
#include <span>

#include "mpt/input/device.h"
#include "mpt/input/profile.h"

namespace mpt::input {

enum class HostAction : std::uint8_t { pause };

inline constexpr std::uint16_t kHostPauseKey = 41;  // HID usage "escape" (SDL_SCANCODE_ESCAPE)
inline constexpr PadButton kHostPauseButton = PadButton::guide;
// The gamepad pause chord: kHostPauseChordButton pressed while
// kHostPauseChordModifier is held on the same pad.
inline constexpr PadButton kHostPauseChordModifier = PadButton::back;
inline constexpr PadButton kHostPauseChordButton = PadButton::start;

// The host action a source is reserved for, or nullopt if titles may bind it.
std::optional<HostAction> host_action_for(const Source& source);
inline bool is_reserved(const Source& source) { return host_action_for(source).has_value(); }

// The host action a key or gamepad button triggers (the event-level form of
// host_action_for, for the shell's event loop via SdlInput::host_action).
std::optional<HostAction> host_action_for_key(std::uint16_t hid_usage);
std::optional<HostAction> host_action_for_button(PadButton button);

// The host action a button-down of `pressed` triggers on a pad whose current
// state is `same_pad` (that pad only, never another one): Guide, or the
// Back + Start chord when Back is held in `same_pad`. Pure; SdlInput::host_action
// reads `same_pad` from SDL.
std::optional<HostAction> host_action_for_pad_press(PadButton pressed, const GamepadState& same_pad);

// Clears every reserved key and button in the snapshots.
void strip_reserved(std::span<DeviceSnapshot> devices);

}  // namespace mpt::input
