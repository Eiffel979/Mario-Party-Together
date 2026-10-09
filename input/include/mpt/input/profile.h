#pragma once

// Input profiles: which physical keys, buttons and axes drive each logical
// control of a player's pad, plus tuning options and per-minigame overrides.
// The on-disk format is JSON and is documented in input/README.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mpt/core/result.h"
#include "mpt/core/title_id.h"
#include "mpt/input/device.h"

namespace mpt::input {

// Logical controls of one player's pad. The first block is one control per
// canonical mpt::Button, in bit order (control i <-> Button 1u << i).
enum class Control : std::uint8_t {
  a,
  b,
  x,
  y,
  l,
  r,
  z,
  zl,
  start,
  select,
  dpad_up,
  dpad_down,
  dpad_left,
  dpad_right,
  c_up,
  c_down,
  c_left,
  c_right,
  home,
  // Analog stick directions (main stick and right/C stick). PadState +Y is up.
  stick_up,
  stick_down,
  stick_left,
  stick_right,
  c_stick_up,
  c_stick_down,
  c_stick_left,
  c_stick_right,
  // Analog triggers, 0..255 in PadState.
  trigger_l,
  trigger_r,
  // Modifier: while held, digital stick sources give `stick_half_percent` deflection.
  stick_half,
  count,
};
inline constexpr std::size_t kControlCount = static_cast<std::size_t>(Control::count);
inline constexpr std::size_t kButtonControlCount = static_cast<std::size_t>(Control::home) + 1;

std::optional<Control> control_from_name(std::string_view name);
std::string_view control_name(Control control);

// One physical input. Text form: "key:<name>", "button:<name>", "axis:<name>+",
// "axis:<name>-" (trigger axes may omit the sign; they only go positive).
struct Source {
  enum class Kind : std::uint8_t { key, pad_button, pad_axis_positive, pad_axis_negative };
  Kind kind = Kind::key;
  std::uint16_t code = 0;  // HID usage, PadButton or PadAxis

  bool is_keyboard() const { return kind == Kind::key; }
  bool is_digital() const { return kind == Kind::key || kind == Kind::pad_button; }

  friend bool operator==(const Source&, const Source&) = default;
};

std::optional<Source> source_from_string(std::string_view text);
std::string source_to_string(const Source& source);

using Bindings = std::array<std::vector<Source>, kControlCount>;

// Tuning. Integers only, so a profile maps identically on every host.
struct Options {
  int stick_deadzone_percent = 10;    // radial, on gamepad sticks (0..90)
  int trigger_deadzone_percent = 5;   // on gamepad trigger axes (0..90)
  int stick_half_percent = 50;        // deflection while stick_half is held (1..100)
  int trigger_button_threshold = 0;   // 1..255: analog trigger >= this also presses L/R. 0 = off
  bool normalize_digital_diagonals = true;  // keys on two axes give a 45 degree vector of radius 1, not a square corner

  friend bool operator==(const Options&, const Options&) = default;
};

// What the mapper actually uses: a profile with its overrides already applied.
struct ResolvedProfile {
  Options options;
  Bindings bindings;

  friend bool operator==(const ResolvedProfile&, const ResolvedProfile&) = default;
};

// A per-minigame override. Listed controls replace the base source list
// (an empty list unbinds); unlisted controls and options inherit.
struct ProfileOverride {
  std::optional<int> stick_deadzone_percent;
  std::optional<int> trigger_deadzone_percent;
  std::optional<int> stick_half_percent;
  std::optional<int> trigger_button_threshold;
  std::optional<bool> normalize_digital_diagonals;
  std::array<std::optional<std::vector<Source>>, kControlCount> bindings;
  std::string note;  // why the override exists (free text, for the coverage table)

  friend bool operator==(const ProfileOverride&, const ProfileOverride&) = default;
};

struct Profile {
  std::string name;
  std::optional<TitleId> title;  // nullopt = usable with any title
  ResolvedProfile base;
  std::map<std::string, ProfileOverride> minigames;  // key: ContentId text, kind "minigame"

  // Applies the override for `minigame_id` if there is one.
  ResolvedProfile resolve(std::string_view minigame_id = {}) const;

  friend bool operator==(const Profile&, const Profile&) = default;
};

inline constexpr std::string_view kProfileFormat = "mpt.input_profile";
inline constexpr int kProfileFormatVersion = 1;

// Parses and validates a profile. Unknown keys, names and out-of-range values are
// errors (so typos are not silently ignored). On failure `out` is unchanged.
Result parse_profile(std::string_view json_text, Profile& out);

// Canonical JSON for a profile; parse_profile(to_json(p)) == p.
std::string to_json(const Profile& profile);

// Built-in GameCube-style profile for keyboard and SDL gamepads. Used when no
// title profile is installed; per-title defaults live in input/profiles/<title>/.
const Profile& builtin_default_profile();

}  // namespace mpt::input
