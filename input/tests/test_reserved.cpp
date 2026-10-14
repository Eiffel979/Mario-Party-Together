// Host-reserved bindings (ADR 0010 §3.2, review amendment 3): Esc and Guide
// never set a bit in an InputFrame. Synthetic device state only.
#include <doctest/doctest.h>

#include <array>
#include <ostream>  // doctest prints std::string_view through operator<<
#include <string>
#include <vector>

#include "mpt/input/mapper.h"
#include "mpt/input/names.h"
#include "mpt/input/reserved.h"

using namespace mpt;
using namespace mpt::input;

namespace {

DeviceSnapshot pad(std::uint32_t instance) {
  DeviceSnapshot s;
  s.id = DeviceId{DeviceKind::gamepad, instance};
  return s;
}

DeviceSnapshot keys() {
  DeviceSnapshot s;
  s.id = kKeyboardDevice;
  return s;
}

PortLayout port0(std::initializer_list<DeviceId> devices) {
  PortLayout layout;
  layout.seated[0] = true;
  for (DeviceId id : devices) layout.device_port[id] = 0;
  return layout;
}

std::string profile_binding(const std::string& bindings, const std::string& minigames = "") {
  return R"({"format": "mpt.input_profile", "version": 1, "name": "t", "title": "mp4", "bindings": {)" + bindings +
         "}" + minigames + "}";
}

// A profile built in code that binds every reserved source to Home and to A,
// which parse_profile would refuse.
Profile hostile_profile() {
  Profile p = builtin_default_profile();
  const Source esc{Source::Kind::key, kHostPauseKey};
  const Source guide{Source::Kind::pad_button, static_cast<std::uint16_t>(kHostPauseButton)};
  for (Control c : {Control::home, Control::a, Control::start}) {
    p.base.bindings[static_cast<std::size_t>(c)].push_back(esc);
    p.base.bindings[static_cast<std::size_t>(c)].push_back(guide);
  }
  return p;
}

}  // namespace

TEST_CASE("Esc and Guide are the reserved host pause action, nothing else is") {
  CHECK(key_from_name("escape") == kHostPauseKey);
  CHECK(pad_button_name(kHostPauseButton) == "guide");
  CHECK(host_action_for(*source_from_string("key:escape")) == HostAction::pause);
  CHECK(host_action_for(*source_from_string("key:#41")) == HostAction::pause);
  CHECK(host_action_for(*source_from_string("button:guide")) == HostAction::pause);
  CHECK(host_action_for_key(kHostPauseKey) == HostAction::pause);
  CHECK(host_action_for_button(PadButton::guide) == HostAction::pause);
  for (std::uint16_t k = 0; k < kKeyCount; ++k) {
    if (k != kHostPauseKey) CHECK_FALSE(host_action_for_key(k).has_value());
  }
  for (std::size_t b = 0; b < kPadButtonCount; ++b) {
    if (static_cast<PadButton>(b) != PadButton::guide) {
      CHECK_FALSE(host_action_for_button(static_cast<PadButton>(b)).has_value());
    }
  }
  CHECK_FALSE(is_reserved(*source_from_string("axis:left_trigger")));
  CHECK_FALSE(is_reserved(*source_from_string("button:start")));
}

TEST_CASE("the built-in default profile binds no reserved source") {
  const Profile& p = builtin_default_profile();
  for (std::size_t c = 0; c < kControlCount; ++c) {
    for (const Source& s : p.base.bindings[c]) {
      CAPTURE(source_to_string(s));
      CHECK_FALSE(is_reserved(s));
    }
  }
  CHECK(p.base.bindings[static_cast<std::size_t>(Control::home)].empty());
}

TEST_CASE("profiles that bind Esc or Guide are rejected") {
  const std::string cases[] = {
      profile_binding(R"("home": ["button:guide"])"),
      profile_binding(R"("a": ["key:k", "key:escape"])"),
      profile_binding(R"("start": ["key:#41"])"),
      profile_binding(R"("a": ["key:k"])",
                      R"(, "minigames": {"mp4.minigame.test": {"bindings": {"home": ["button:guide"]}}})"),
      profile_binding(R"("a": ["key:k"])",
                      R"(, "minigames": {"mp4.minigame.test": {"bindings": {"b": ["key:escape"]}}})"),
  };
  for (const std::string& json : cases) {
    CAPTURE(json);
    Profile p;
    p.name = "untouched";
    const Result r = parse_profile(json, p);
    REQUIRE_FALSE(r.ok());
    CAPTURE(r.error());
    CHECK(r.error().find("reserved for the host pause action") != std::string::npos);
    CHECK(p.name == "untouched");
  }
  // Still fine: neighbouring sources.
  Profile ok;
  CHECK(parse_profile(profile_binding(R"("home": ["button:back"], "start": ["key:enter"])"), ok));
}

TEST_CASE("a reserved source never sets a bit, even in a profile built in code") {
  std::array<ResolvedProfile, kMaxPlayers> profiles;
  profiles.fill(hostile_profile().resolve());
  DeviceSnapshot p = pad(1);
  DeviceSnapshot k = keys();
  const PortLayout layout = port0({p.id, k.id});
  p.gamepad.set(PadButton::guide, true);
  k.keyboard.set(kHostPauseKey);
  const PadState out = map_frame(0, profiles, std::vector<DeviceSnapshot>{p, k}, layout).pads[0];
  CHECK(out.connected == 1);
  CHECK(out.buttons == 0);
}

TEST_CASE("Guide held across a resume leaves kButtonHome clear") {
  // The shell consumed the Guide press that resumed; the button is still down
  // on the frames stepped after resume. Even with Home bound to every other
  // gamepad button, Guide contributes nothing.
  Mapper mapper;
  DeviceSnapshot p = pad(7);
  const PortLayout layout = port0({p.id});
  for (std::uint64_t frame = 0; frame < 4; ++frame) {
    p.gamepad.set(PadButton::guide, frame < 3);  // held for three frames, then released
    std::vector<DeviceSnapshot> devices{p};
    strip_reserved(devices);
    CHECK_FALSE(devices[0].gamepad.pressed(PadButton::guide));
    CHECK((mapper.map(frame, devices, layout).pads[0].buttons & kButtonHome) == 0);
    // Without stripping too: the mapper ignores the reserved source on its own.
    CHECK((mapper.map(frame, std::vector<DeviceSnapshot>{p}, layout).pads[0].buttons & kButtonHome) == 0);
  }
}

TEST_CASE("strip_reserved clears only Esc and Guide") {
  DeviceSnapshot k = keys();
  k.keyboard.set(kHostPauseKey);
  k.keyboard.set(*key_from_name("enter"));
  DeviceSnapshot p = pad(2);
  p.gamepad.set(PadButton::guide, true);
  p.gamepad.set(PadButton::south, true);
  std::vector<DeviceSnapshot> devices{k, p};
  strip_reserved(devices);
  CHECK_FALSE(devices[0].keyboard.test(kHostPauseKey));
  CHECK(devices[0].keyboard.test(*key_from_name("enter")));
  CHECK(devices[0].keyboard.count() == 1);
  CHECK(devices[1].gamepad.buttons == (1u << static_cast<unsigned>(PadButton::south)));
}
