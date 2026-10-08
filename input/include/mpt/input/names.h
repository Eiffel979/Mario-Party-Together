#pragma once

// Stable text names used by profile files and the rebinding UI. Names are lower
// snake_case and never change once shipped (profiles on users' disks use them).

#include <cstdint>
#include <optional>
#include <string_view>

#include "mpt/input/device.h"

namespace mpt::input {

// Keyboard: HID usage ID <-> name ("a", "1", "space", "lshift", "kp_8", "up", ...).
std::optional<std::uint16_t> key_from_name(std::string_view name);
std::string_view key_name(std::uint16_t key);  // empty if the key has no name

std::optional<PadButton> pad_button_from_name(std::string_view name);
std::string_view pad_button_name(PadButton button);

std::optional<PadAxis> pad_axis_from_name(std::string_view name);
std::string_view pad_axis_name(PadAxis axis);

}  // namespace mpt::input
