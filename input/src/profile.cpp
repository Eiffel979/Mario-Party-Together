#include "mpt/input/profile.h"

#include <array>
#include <charconv>
#include <cstdlib>
#include <initializer_list>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

#include "mpt/core/content_id.h"
#include "mpt/input/names.h"

namespace mpt::input {
namespace {

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

constexpr std::array<std::string_view, kControlCount> kControlNames = {
    "a",          "b",           "x",          "y",           "l",          "r",
    "z",          "zl",          "start",      "select",      "dpad_up",    "dpad_down",
    "dpad_left",  "dpad_right",  "c_up",       "c_down",      "c_left",     "c_right",
    "home",       "stick_up",    "stick_down", "stick_left",  "stick_right", "c_stick_up",
    "c_stick_down", "c_stick_left", "c_stick_right", "trigger_l", "trigger_r", "stick_half",
};

struct IntOption {
  std::string_view name;
  int Options::*field;
  std::optional<int> ProfileOverride::*override_field;
  int min;
  int max;
};

constexpr IntOption kIntOptions[] = {
    {"stick_deadzone_percent", &Options::stick_deadzone_percent, &ProfileOverride::stick_deadzone_percent, 0, 90},
    {"trigger_deadzone_percent", &Options::trigger_deadzone_percent, &ProfileOverride::trigger_deadzone_percent, 0,
     90},
    {"stick_half_percent", &Options::stick_half_percent, &ProfileOverride::stick_half_percent, 1, 100},
    {"trigger_button_threshold", &Options::trigger_button_threshold, &ProfileOverride::trigger_button_threshold, 0,
     255},
};
constexpr std::string_view kDiagonalsOption = "normalize_digital_diagonals";

Result fail(std::string where, std::string_view what) { return Result::failure(where + ": " + std::string(what)); }

Result check_keys(const json& object, std::string_view where, std::initializer_list<std::string_view> allowed) {
  for (const auto& [key, value] : object.items()) {
    bool known = false;
    for (std::string_view a : allowed) known = known || key == a;
    if (!known) return fail(std::string(where), "unknown key '" + key + "'");
  }
  return Result::success();
}

// Parses "options" into either base options or an override (exactly one non-null).
Result parse_options(const json& j, std::string_view where, Options* base, ProfileOverride* over) {
  if (!j.is_object()) return fail(std::string(where), "must be an object");
  for (const auto& [key, value] : j.items()) {
    const std::string path = std::string(where) + "." + key;
    if (key == kDiagonalsOption) {
      if (!value.is_boolean()) return fail(path, "must be true or false");
      if (base) base->normalize_digital_diagonals = value.get<bool>();
      if (over) over->normalize_digital_diagonals = value.get<bool>();
      continue;
    }
    const IntOption* option = nullptr;
    for (const IntOption& o : kIntOptions) {
      if (o.name == key) option = &o;
    }
    if (!option) return fail(std::string(where), "unknown option '" + key + "'");
    if (!value.is_number_integer()) return fail(path, "must be an integer");
    const auto v = value.get<std::int64_t>();
    if (v < option->min || v > option->max) {
      return fail(path, "must be in " + std::to_string(option->min) + ".." + std::to_string(option->max));
    }
    if (base) base->*(option->field) = static_cast<int>(v);
    if (over) over->*(option->override_field) = static_cast<int>(v);
  }
  return Result::success();
}

Result parse_source_list(const json& j, const std::string& path, std::vector<Source>& out) {
  if (!j.is_array()) return fail(path, "must be an array of source strings");
  std::vector<Source> sources;
  for (const json& item : j) {
    if (!item.is_string()) return fail(path, "sources must be strings");
    const auto text = item.get<std::string>();
    const auto source = source_from_string(text);
    if (!source) return fail(path, "unknown source '" + text + "'");
    for (const Source& s : sources) {
      if (s == *source) return fail(path, "duplicate source '" + text + "'");
    }
    sources.push_back(*source);
  }
  out = std::move(sources);
  return Result::success();
}

// Base bindings: unlisted controls stay unbound. Override bindings: listed only.
Result parse_bindings(const json& j, std::string_view where, Bindings* base, ProfileOverride* over) {
  if (!j.is_object()) return fail(std::string(where), "must be an object");
  for (const auto& [key, value] : j.items()) {
    const auto control = control_from_name(key);
    if (!control) return fail(std::string(where), "unknown control '" + key + "'");
    std::vector<Source> sources;
    if (Result r = parse_source_list(value, std::string(where) + "." + key, sources); !r) return r;
    const auto i = static_cast<std::size_t>(*control);
    if (base) (*base)[i] = std::move(sources);
    else if (over) over->bindings[i] = std::move(sources);
  }
  return Result::success();
}

ordered_json sources_json(const std::vector<Source>& sources) {
  ordered_json list = ordered_json::array();
  for (const Source& s : sources) list.push_back(source_to_string(s));
  return list;
}

// GameCube-style layout. Keyboard: WASD stick (left shift = half), arrows d-pad,
// TFGH C-stick, K/J/L/I = A/B/X/Y, Q/E = L/R (full analog press), U = Z, Enter = Start.
// Gamepad: positional face buttons by label on an Xbox pad, sticks pass through,
// analog triggers with the digital click at ~90%.
constexpr std::string_view kBuiltinProfileJson = R"json({
  "format": "mpt.input_profile",
  "version": 1,
  "name": "Default (GameCube layout)",
  "options": {
    "stick_deadzone_percent": 10,
    "trigger_deadzone_percent": 5,
    "stick_half_percent": 50,
    "trigger_button_threshold": 230,
    "normalize_digital_diagonals": true
  },
  "bindings": {
    "a": ["key:k", "key:space", "button:south"],
    "b": ["key:j", "button:east"],
    "x": ["key:l", "button:west"],
    "y": ["key:i", "button:north"],
    "l": ["key:q"],
    "r": ["key:e"],
    "z": ["key:u", "button:right_shoulder"],
    "start": ["key:enter", "button:start"],
    "select": ["key:backspace", "button:back"],
    "home": ["button:guide"],
    "dpad_up": ["key:up", "button:dpad_up"],
    "dpad_down": ["key:down", "button:dpad_down"],
    "dpad_left": ["key:left", "button:dpad_left"],
    "dpad_right": ["key:right", "button:dpad_right"],
    "stick_up": ["key:w", "axis:left_y-"],
    "stick_down": ["key:s", "axis:left_y+"],
    "stick_left": ["key:a", "axis:left_x-"],
    "stick_right": ["key:d", "axis:left_x+"],
    "c_stick_up": ["key:t", "axis:right_y-"],
    "c_stick_down": ["key:g", "axis:right_y+"],
    "c_stick_left": ["key:f", "axis:right_x-"],
    "c_stick_right": ["key:h", "axis:right_x+"],
    "trigger_l": ["key:q", "axis:left_trigger"],
    "trigger_r": ["key:e", "axis:right_trigger"],
    "stick_half": ["key:lshift"]
  }
})json";

}  // namespace

std::optional<Control> control_from_name(std::string_view name) {
  for (std::size_t i = 0; i < kControlNames.size(); ++i) {
    if (kControlNames[i] == name) return static_cast<Control>(i);
  }
  return std::nullopt;
}

std::string_view control_name(Control control) {
  const auto i = static_cast<std::size_t>(control);
  return i < kControlNames.size() ? kControlNames[i] : std::string_view{};
}

std::optional<Source> source_from_string(std::string_view text) {
  const auto colon = text.find(':');
  if (colon == std::string_view::npos) return std::nullopt;
  const std::string_view kind = text.substr(0, colon);
  std::string_view name = text.substr(colon + 1);

  if (kind == "key") {
    if (const auto key = key_from_name(name)) return Source{Source::Kind::key, *key};
    // Keys without a name: "key:#<HID usage>", decimal, below kKeyCount.
    if (name.size() >= 2 && name.front() == '#') {
      const std::string_view digits = name.substr(1);
      unsigned code = 0;
      const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), code);
      if (ec == std::errc{} && end == digits.data() + digits.size() && code < kKeyCount) {
        return Source{Source::Kind::key, static_cast<std::uint16_t>(code)};
      }
    }
    return std::nullopt;
  }
  if (kind == "button") {
    if (const auto b = pad_button_from_name(name)) return Source{Source::Kind::pad_button, static_cast<std::uint16_t>(*b)};
    return std::nullopt;
  }
  if (kind == "axis") {
    std::optional<Source::Kind> direction;
    if (!name.empty() && (name.back() == '+' || name.back() == '-')) {
      direction = name.back() == '+' ? Source::Kind::pad_axis_positive : Source::Kind::pad_axis_negative;
      name.remove_suffix(1);
    }
    const auto axis = pad_axis_from_name(name);
    if (!axis) return std::nullopt;
    const bool trigger = *axis == PadAxis::left_trigger || *axis == PadAxis::right_trigger;
    if (trigger) {
      if (direction == Source::Kind::pad_axis_negative) return std::nullopt;  // triggers never go negative
      direction = Source::Kind::pad_axis_positive;
    }
    if (!direction) return std::nullopt;  // stick axes need an explicit direction
    return Source{*direction, static_cast<std::uint16_t>(*axis)};
  }
  return std::nullopt;
}

std::string source_to_string(const Source& source) {
  switch (source.kind) {
    case Source::Kind::key: {
      const std::string_view name = key_name(source.code);
      return name.empty() ? "key:#" + std::to_string(source.code) : "key:" + std::string(name);
    }
    case Source::Kind::pad_button:
      return "button:" + std::string(pad_button_name(static_cast<PadButton>(source.code)));
    case Source::Kind::pad_axis_positive:
    case Source::Kind::pad_axis_negative: {
      const auto axis = static_cast<PadAxis>(source.code);
      std::string text = "axis:" + std::string(pad_axis_name(axis));
      const bool trigger = axis == PadAxis::left_trigger || axis == PadAxis::right_trigger;
      if (!trigger) text += source.kind == Source::Kind::pad_axis_positive ? "+" : "-";
      return text;
    }
  }
  return {};
}

ResolvedProfile Profile::resolve(std::string_view minigame_id) const {
  ResolvedProfile resolved = base;
  if (minigame_id.empty()) return resolved;
  const auto it = minigames.find(std::string(minigame_id));
  if (it == minigames.end()) return resolved;

  const ProfileOverride& o = it->second;
  for (const IntOption& option : kIntOptions) {
    if (const auto& v = o.*(option.override_field)) resolved.options.*(option.field) = *v;
  }
  if (o.normalize_digital_diagonals) resolved.options.normalize_digital_diagonals = *o.normalize_digital_diagonals;
  for (std::size_t i = 0; i < kControlCount; ++i) {
    if (o.bindings[i]) resolved.bindings[i] = *o.bindings[i];
  }
  return resolved;
}

Result parse_profile(std::string_view json_text, Profile& out) {
  // nlohmann keeps the last of duplicate keys silently. Track the keys of each
  // open object (one set per nesting depth) and reject any repeat.
  std::vector<std::set<std::string>> open_keys;
  std::string duplicate;
  const json::parser_callback_t track_keys = [&](int depth, json::parse_event_t event, json& parsed) {
    const auto d = static_cast<std::size_t>(depth);
    if (event == json::parse_event_t::object_start) {
      if (open_keys.size() < d + 2) open_keys.resize(d + 2);
      open_keys[d + 1].clear();
    } else if (event == json::parse_event_t::key && duplicate.empty() && d < open_keys.size()) {
      const auto key = parsed.get<std::string>();
      if (!open_keys[d].insert(key).second) duplicate = key;
    }
    return true;
  };
  const json root = json::parse(json_text, track_keys, /*allow_exceptions=*/false);
  if (root.is_discarded()) return Result::failure("profile: not valid JSON");
  if (!duplicate.empty()) return Result::failure("profile: duplicate key '" + duplicate + "'");
  if (!root.is_object()) return Result::failure("profile: top level must be an object");
  if (Result r = check_keys(root, "profile", {"format", "version", "name", "title", "options", "bindings", "minigames"});
      !r) {
    return r;
  }

  if (!root.contains("format") || !root["format"].is_string() || root["format"].get<std::string>() != kProfileFormat) {
    return Result::failure("profile: 'format' must be \"" + std::string(kProfileFormat) + "\"");
  }
  if (!root.contains("version") || !root["version"].is_number_integer()) {
    return Result::failure("profile: 'version' must be an integer");
  }
  if (root["version"].get<std::int64_t>() != kProfileFormatVersion) {
    return Result::failure("profile: unsupported version " + root["version"].dump());
  }

  Profile profile;
  if (!root.contains("name") || !root["name"].is_string() || root["name"].get<std::string>().empty()) {
    return Result::failure("profile: 'name' must be a non-empty string");
  }
  profile.name = root["name"].get<std::string>();

  if (root.contains("title")) {
    if (!root["title"].is_string()) return Result::failure("profile: 'title' must be a string");
    profile.title = parse_title_id(root["title"].get<std::string>());
    if (!profile.title) return Result::failure("profile: unknown title '" + root["title"].get<std::string>() + "'");
  }

  if (root.contains("options")) {
    if (Result r = parse_options(root["options"], "options", &profile.base.options, nullptr); !r) return r;
  }
  if (!root.contains("bindings")) return Result::failure("profile: 'bindings' is required");
  if (Result r = parse_bindings(root["bindings"], "bindings", &profile.base.bindings, nullptr); !r) return r;

  if (root.contains("minigames")) {
    const json& minigames = root["minigames"];
    if (!minigames.is_object()) return Result::failure("minigames: must be an object");
    for (const auto& [key, value] : minigames.items()) {
      const std::string where = "minigames." + key;
      const auto id = ContentId::parse(key);
      if (!id || id->kind() != "minigame") return fail(where, "key must be a content ID of kind 'minigame'");
      if (profile.title && id->title() != *profile.title) {
        return fail(where, "minigame belongs to another title than the profile");
      }
      if (!value.is_object()) return fail(where, "must be an object");
      if (Result r = check_keys(value, where, {"note", "options", "bindings"}); !r) return r;

      ProfileOverride over;
      if (value.contains("note")) {
        if (!value["note"].is_string()) return fail(where + ".note", "must be a string");
        over.note = value["note"].get<std::string>();
      }
      if (value.contains("options")) {
        if (Result r = parse_options(value["options"], where + ".options", nullptr, &over); !r) return r;
      }
      if (value.contains("bindings")) {
        if (Result r = parse_bindings(value["bindings"], where + ".bindings", nullptr, &over); !r) return r;
      }
      profile.minigames.emplace(key, std::move(over));
    }
  }

  out = std::move(profile);
  return Result::success();
}

std::string to_json(const Profile& profile) {
  ordered_json root;
  root["format"] = std::string(kProfileFormat);
  root["version"] = kProfileFormatVersion;
  root["name"] = profile.name;
  if (profile.title) root["title"] = std::string(to_string(*profile.title));

  ordered_json options;
  for (const IntOption& o : kIntOptions) options[std::string(o.name)] = profile.base.options.*(o.field);
  options[std::string(kDiagonalsOption)] = profile.base.options.normalize_digital_diagonals;
  root["options"] = options;

  ordered_json bindings = ordered_json::object();
  for (std::size_t i = 0; i < kControlCount; ++i) {
    if (!profile.base.bindings[i].empty()) {
      bindings[std::string(kControlNames[i])] = sources_json(profile.base.bindings[i]);
    }
  }
  root["bindings"] = bindings;

  if (!profile.minigames.empty()) {
    ordered_json minigames = ordered_json::object();
    for (const auto& [id, o] : profile.minigames) {
      ordered_json entry = ordered_json::object();
      if (!o.note.empty()) entry["note"] = o.note;
      ordered_json over_options = ordered_json::object();
      for (const IntOption& option : kIntOptions) {
        if (const auto& v = o.*(option.override_field)) over_options[std::string(option.name)] = *v;
      }
      if (o.normalize_digital_diagonals) over_options[std::string(kDiagonalsOption)] = *o.normalize_digital_diagonals;
      if (!over_options.empty()) entry["options"] = over_options;
      ordered_json over_bindings = ordered_json::object();
      for (std::size_t i = 0; i < kControlCount; ++i) {
        if (o.bindings[i]) over_bindings[std::string(kControlNames[i])] = sources_json(*o.bindings[i]);
      }
      if (!over_bindings.empty()) entry["bindings"] = over_bindings;
      minigames[id] = entry;
    }
    root["minigames"] = minigames;
  }
  return root.dump(2) + "\n";
}

const Profile& builtin_default_profile() {
  static const Profile profile = [] {
    Profile p;
    if (Result r = parse_profile(kBuiltinProfileJson, p); !r) std::abort();  // covered by a unit test
    return p;
  }();
  return profile;
}

}  // namespace mpt::input
