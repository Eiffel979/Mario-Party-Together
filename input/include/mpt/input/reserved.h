#pragma once

// Host-reserved bindings (ADR 0010 §3.2, review amendment 3). These physical
// inputs belong to the host, not to any title: they never reach an InputFrame,
// so they can never be recorded in a replay or sent over netplay.
//
//   Esc (keyboard)      host pause action
//   Guide (gamepad)     host pause action
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

// The host action a source is reserved for, or nullopt if titles may bind it.
std::optional<HostAction> host_action_for(const Source& source);
inline bool is_reserved(const Source& source) { return host_action_for(source).has_value(); }

// The host action a key or gamepad button triggers (the event-level form of
// host_action_for, for the shell's event loop via SdlInput::host_action).
std::optional<HostAction> host_action_for_key(std::uint16_t hid_usage);
std::optional<HostAction> host_action_for_button(PadButton button);

// Clears every reserved key and button in the snapshots.
void strip_reserved(std::span<DeviceSnapshot> devices);

}  // namespace mpt::input
