#include <doctest/doctest.h>

#include <array>
#include <cstdlib>
#include <initializer_list>
#include <ostream>  // doctest prints std::string_view through operator<<
#include <string_view>
#include <vector>

#include "mpt/input/mapper.h"
#include "mpt/input/names.h"

using namespace mpt;
using namespace mpt::input;

namespace {

DeviceSnapshot keyboard(std::initializer_list<std::string_view> held) {
  DeviceSnapshot s;
  s.id = kKeyboardDevice;
  for (std::string_view name : held) {
    const auto key = key_from_name(name);
    REQUIRE(key.has_value());
    s.keyboard.set(*key);
  }
  return s;
}

DeviceSnapshot gamepad(std::uint32_t instance) {
  DeviceSnapshot s;
  s.id = DeviceId{DeviceKind::gamepad, instance};
  return s;
}

// Everything on port 0, seated.
PortLayout port0(std::initializer_list<DeviceId> devices) {
  PortLayout layout;
  layout.seated[0] = true;
  for (DeviceId id : devices) layout.device_port[id] = 0;
  return layout;
}

std::array<ResolvedProfile, kMaxPlayers> defaults() {
  std::array<ResolvedProfile, kMaxPlayers> p;
  p.fill(builtin_default_profile().resolve());
  return p;
}

PadState map_one(const std::vector<DeviceSnapshot>& devices, const PortLayout& layout,
                 const std::array<ResolvedProfile, kMaxPlayers>& profiles = defaults()) {
  return map_frame(0, profiles, devices, layout).pads[0];
}

}  // namespace

TEST_CASE("isqrt is exact floor sqrt") {
  CHECK(isqrt(0) == 0);
  CHECK(isqrt(1) == 1);
  CHECK(isqrt(3) == 1);
  CHECK(isqrt(4) == 2);
  CHECK(isqrt(1073676289) == 32767);  // 32767^2
  CHECK(isqrt(1073676288) == 32766);
  CHECK(isqrt(2147352578) == 46339);  // 2 * 32767^2
  for (std::uint64_t v = 0; v < 5000; ++v) {
    const std::uint64_t r = isqrt(v);
    CHECK(r * r <= v);
    CHECK((r + 1) * (r + 1) > v);
  }
}

TEST_CASE("radial deadzone zeroes small tilts and keeps full tilt full") {
  CHECK(apply_radial_deadzone(0, 0, 10) == std::pair{0, 0});
  CHECK(apply_radial_deadzone(3000, 0, 10) == std::pair{0, 0});      // 9% < 10%
  CHECK(apply_radial_deadzone(2000, 2000, 10) == std::pair{0, 0});   // radius 2828
  CHECK(apply_radial_deadzone(32767, 0, 10) == std::pair{32767, 0});
  CHECK(apply_radial_deadzone(-32768, 0, 10) == std::pair{-32767, 0});  // symmetric range
  CHECK(apply_radial_deadzone(0, -32768, 0) == std::pair{0, -32767});
  CHECK(apply_radial_deadzone(1234, -567, 0) == std::pair{1234, -567});
  const auto [x, y] = apply_radial_deadzone(16384, 0, 10);  // halfway between edge (3276) and full
  CHECK(x > 14000);
  CHECK(x < 16384);
  CHECK(y == 0);
  // Direction is preserved.
  const auto [dx, dy] = apply_radial_deadzone(20000, 20000, 10);
  CHECK(dx == dy);
}

TEST_CASE("trigger deadzone rescales 0..32767") {
  CHECK(apply_trigger_deadzone(0, 5) == 0);
  CHECK(apply_trigger_deadzone(1638, 5) == 0);
  CHECK(apply_trigger_deadzone(32767, 5) == 32767);
  CHECK(apply_trigger_deadzone(32767, 0) == 32767);
  CHECK(apply_trigger_deadzone(-5, 0) == 0);
}

TEST_CASE("keyboard stick: full deflection, opposite keys cancel, +Y is up") {
  const PortLayout layout = port0({kKeyboardDevice});
  PadState p = map_one({keyboard({"w"})}, layout);
  CHECK(p.stick_x == 0);
  CHECK(p.stick_y == 32767);
  p = map_one({keyboard({"s"})}, layout);
  CHECK(p.stick_y == -32767);
  p = map_one({keyboard({"d"})}, layout);
  CHECK(p.stick_x == 32767);
  p = map_one({keyboard({"a", "d"})}, layout);
  CHECK(p.stick_x == 0);
  p = map_one({keyboard({"t", "f"})}, layout);  // C-stick up-left
  CHECK(p.c_stick_x < 0);
  CHECK(p.c_stick_y > 0);
  CHECK(p.stick_x == 0);
}

TEST_CASE("keyboard stick: diagonals are normalized onto the unit circle") {
  const PortLayout layout = port0({kKeyboardDevice});
  PadState p = map_one({keyboard({"w", "d"})}, layout);
  CHECK(p.stick_x == 23170);  // 32767 / sqrt(2)
  CHECK(p.stick_y == 23170);

  auto square = defaults();
  square[0].options.normalize_digital_diagonals = false;
  p = map_one({keyboard({"w", "d"})}, layout, square);
  CHECK(p.stick_x == 32767);
  CHECK(p.stick_y == 32767);
}

TEST_CASE("keyboard stick: half deflection modifier") {
  const PortLayout layout = port0({kKeyboardDevice});
  PadState p = map_one({keyboard({"w", "lshift"})}, layout);
  CHECK(p.stick_y == 16383);
  CHECK(p.stick_x == 0);
  CHECK(p.buttons == 0);  // the modifier is not a pad button

  auto custom = defaults();
  custom[0].options.stick_half_percent = 30;
  p = map_one({keyboard({"a", "lshift"})}, layout, custom);
  CHECK(p.stick_x == -9830);

  p = map_one({keyboard({"w", "d", "lshift"})}, layout);  // half and diagonal combine
  CHECK(p.stick_x == 11585);
  CHECK(p.stick_y == 11585);

  p = map_one({keyboard({"t", "lshift"})}, layout);  // applies to the C-stick too
  CHECK(p.c_stick_y == 16383);
}

TEST_CASE("keyboard buttons and full-press triggers") {
  const PortLayout layout = port0({kKeyboardDevice});
  PadState p = map_one({keyboard({"k", "j", "enter", "up"})}, layout);
  CHECK(p.buttons == (kButtonA | kButtonB | kButtonStart | kButtonDpadUp));
  CHECK(p.connected == 1);
  CHECK(p.trigger_l == 0);

  p = map_one({keyboard({"q"})}, layout);  // L: digital click + full analog
  CHECK(p.trigger_l == 255);
  CHECK(p.trigger_r == 0);
  CHECK(p.buttons == kButtonL);

  p = map_one({keyboard({"space"})}, layout);  // second source for A
  CHECK(p.buttons == kButtonA);
}

TEST_CASE("gamepad sticks pass through with SDL's Y flipped to +Y up") {
  const DeviceSnapshot base = gamepad(7);
  const PortLayout layout = port0({base.id});

  DeviceSnapshot pad = base;
  pad.gamepad.set(PadAxis::left_y, -32768);  // SDL: up is negative
  pad.gamepad.set(PadAxis::right_x, 32767);
  PadState p = map_one({pad}, layout);
  CHECK(p.stick_y == 32767);
  CHECK(p.stick_x == 0);
  CHECK(p.c_stick_x == 32767);

  pad = base;
  pad.gamepad.set(PadAxis::left_x, -20000);
  pad.gamepad.set(PadAxis::left_y, 20000);  // down-left
  p = map_one({pad}, layout);
  CHECK(p.stick_x < 0);
  CHECK(p.stick_y < 0);
  CHECK(p.stick_x == p.stick_y);

  pad = base;
  pad.gamepad.set(PadAxis::left_x, 2000);  // inside the 10% deadzone
  p = map_one({pad}, layout);
  CHECK(p.stick_x == 0);
}

TEST_CASE("gamepad buttons map to canonical buttons") {
  DeviceSnapshot pad = gamepad(1);
  const PortLayout layout = port0({pad.id});
  pad.gamepad.set(PadButton::south, true);
  pad.gamepad.set(PadButton::right_shoulder, true);
  pad.gamepad.set(PadButton::dpad_left, true);
  pad.gamepad.set(PadButton::start, true);
  CHECK(map_one({pad}, layout).buttons == (kButtonA | kButtonZ | kButtonDpadLeft | kButtonStart));
}

TEST_CASE("analog triggers scale to 0..255 and click L/R at the threshold") {
  DeviceSnapshot pad = gamepad(1);
  const PortLayout layout = port0({pad.id});

  pad.gamepad.set(PadAxis::left_trigger, 32767);
  PadState p = map_one({pad}, layout);
  CHECK(p.trigger_l == 255);
  CHECK(p.buttons == kButtonL);

  pad.gamepad.set(PadAxis::left_trigger, 16384);  // ~47% after the 5% deadzone: analog only
  p = map_one({pad}, layout);
  CHECK(p.trigger_l > 100);
  CHECK(p.trigger_l < 140);
  CHECK(p.buttons == 0);

  pad.gamepad.set(PadAxis::left_trigger, 1000);  // inside the deadzone
  CHECK(map_one({pad}, layout).trigger_l == 0);

  auto no_click = defaults();
  no_click[0].options.trigger_button_threshold = 0;
  pad.gamepad.set(PadAxis::right_trigger, 32767);
  p = map_one({pad}, layout, no_click);
  CHECK(p.trigger_r == 255);
  CHECK(p.buttons == 0);
}

TEST_CASE("an analog source bound to a button presses it past half travel") {
  Profile prof;
  REQUIRE(parse_profile(R"({"format":"mpt.input_profile","version":1,"name":"t",
                            "bindings":{"z":["axis:left_trigger"],"a":["axis:right_y+"]}})",
                        prof));
  std::array<ResolvedProfile, kMaxPlayers> profiles;
  profiles.fill(prof.resolve());
  DeviceSnapshot pad = gamepad(1);
  const PortLayout layout = port0({pad.id});

  pad.gamepad.set(PadAxis::left_trigger, 10000);
  CHECK(map_one({pad}, layout, profiles).buttons == 0);
  pad.gamepad.set(PadAxis::left_trigger, 30000);
  CHECK(map_one({pad}, layout, profiles).buttons == kButtonZ);
  pad.gamepad.set(PadAxis::right_y, 30000);
  CHECK(map_one({pad}, layout, profiles).buttons == (kButtonZ | kButtonA));
}

TEST_CASE("keyboard and gamepad on one port merge") {
  DeviceSnapshot pad = gamepad(3);
  const PortLayout layout = port0({kKeyboardDevice, pad.id});
  pad.gamepad.set(PadButton::east, true);
  pad.gamepad.set(PadAxis::left_x, 32767);
  const PadState p = map_one({keyboard({"k", "w"}), pad}, layout);
  CHECK(p.buttons == (kButtonA | kButtonB));
  CHECK(p.stick_y == 32767);  // from the keyboard
  CHECK(p.stick_x == 32767);  // from the gamepad

  // Per axis the larger magnitude wins: a key overrides a small analog tilt.
  DeviceSnapshot small = gamepad(3);
  small.gamepad.set(PadAxis::left_x, -12000);
  CHECK(map_one({keyboard({"d"}), small}, layout).stick_x == 32767);
}

TEST_CASE("map_frame routes devices to ports and handles seating") {
  DeviceSnapshot p1 = gamepad(10);
  DeviceSnapshot p2 = gamepad(11);
  DeviceSnapshot stray = gamepad(12);
  p1.gamepad.set(PadButton::south, true);
  p2.gamepad.set(PadButton::north, true);
  stray.gamepad.set(PadButton::start, true);

  PortLayout layout;
  layout.seated = {true, false, true, true};
  layout.device_port[kKeyboardDevice] = 3;
  layout.device_port[p1.id] = 0;
  layout.device_port[p2.id] = 1;  // port 1 is not seated
  // stray is unassigned

  const std::vector<DeviceSnapshot> devices = {keyboard({"j"}), p1, p2, stray};
  const InputFrame f = map_frame(42, defaults(), devices, layout);
  CHECK(f.frame == 42);
  CHECK(f.pads[0].buttons == kButtonA);
  CHECK(f.pads[0].connected == 1);
  CHECK(f.pads[1] == PadState{});  // unseated: neutral and disconnected
  CHECK(f.pads[2].buttons == 0);   // seated without a device: idle but connected
  CHECK(f.pads[2].connected == 1);
  CHECK(f.pads[3].buttons == kButtonB);
}

TEST_CASE("mapping is a pure function of its inputs") {
  DeviceSnapshot pad = gamepad(5);
  pad.gamepad.set(PadAxis::left_x, 12345);
  pad.gamepad.set(PadAxis::left_y, -23456);
  pad.gamepad.set(PadAxis::right_trigger, 20000);
  pad.gamepad.set(PadButton::west, true);
  PortLayout layout = port0({kKeyboardDevice});
  layout.seated[1] = true;
  layout.device_port[pad.id] = 1;
  const std::vector<DeviceSnapshot> devices = {keyboard({"w", "d", "lshift", "e"}), pad};

  const InputFrame a = map_frame(9, defaults(), devices, layout);
  for (int i = 0; i < 100; ++i) CHECK(map_frame(9, defaults(), devices, layout) == a);

  Mapper mapper;
  CHECK(mapper.map(9, devices, layout) == a);
}

TEST_CASE("Mapper applies per-minigame overrides only while that minigame is set") {
  Profile prof = builtin_default_profile();
  ProfileOverride over;
  over.bindings[static_cast<std::size_t>(Control::a)] =
      std::vector<Source>{*source_from_string("button:south"), *source_from_string("button:east")};
  prof.minigames["mp4.minigame.test_mash"] = over;

  Mapper mapper;
  mapper.set_profile_all(prof);
  DeviceSnapshot pad = gamepad(2);
  pad.gamepad.set(PadButton::east, true);
  const PortLayout layout = port0({pad.id});
  const std::vector<DeviceSnapshot> devices = {pad};

  CHECK(mapper.map(0, devices, layout).pads[0].buttons == kButtonB);
  mapper.set_minigame("mp4.minigame.test_mash");
  // East now also presses A. B keeps its base binding (east): only listed controls are replaced.
  CHECK(mapper.map(1, devices, layout).pads[0].buttons == (kButtonA | kButtonB));
  mapper.set_minigame("");
  CHECK(mapper.map(2, devices, layout).pads[0].buttons == kButtonB);
}

TEST_CASE("per-port profiles are independent") {
  Profile swapped;
  REQUIRE(parse_profile(R"({"format":"mpt.input_profile","version":1,"name":"swap",
                            "bindings":{"a":["button:east"],"b":["button:south"]}})",
                        swapped));
  Mapper mapper;
  mapper.set_profile(1, swapped);
  DeviceSnapshot a = gamepad(1);
  DeviceSnapshot b = gamepad(2);
  a.gamepad.set(PadButton::south, true);
  b.gamepad.set(PadButton::south, true);
  PortLayout layout;
  layout.seated = {true, true, false, false};
  layout.device_port[a.id] = 0;
  layout.device_port[b.id] = 1;
  const std::vector<DeviceSnapshot> devices = {a, b};
  const InputFrame f = mapper.map(0, devices, layout);
  CHECK(f.pads[0].buttons == kButtonA);
  CHECK(f.pads[1].buttons == kButtonB);
}
