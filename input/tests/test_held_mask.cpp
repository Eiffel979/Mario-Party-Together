// Buttons held across a resume or unfocus are masked until released
// (ADR 0010 §3.3). Synthetic device state only.
#include <doctest/doctest.h>

#include <vector>

#include "mpt/input/held_mask.h"
#include "mpt/input/mapper.h"
#include "mpt/input/names.h"

using namespace mpt;
using namespace mpt::input;

namespace {

constexpr DeviceId kPad{DeviceKind::gamepad, 3};

DeviceSnapshot pad() {
  DeviceSnapshot s;
  s.id = kPad;
  return s;
}

DeviceSnapshot keys() {
  DeviceSnapshot s;
  s.id = kKeyboardDevice;
  return s;
}

std::uint16_t key(std::string_view name) {
  const auto k = key_from_name(name);
  REQUIRE(k.has_value());
  return *k;
}

PortLayout port0() {
  PortLayout layout;
  layout.seated[0] = true;
  layout.device_port[kPad] = 0;
  layout.device_port[kKeyboardDevice] = 0;
  return layout;
}

// What SdlInput::sample does: mask, then map.
PadState step(const Mapper& mapper, HeldMask& mask, std::vector<DeviceSnapshot> devices, std::uint64_t frame = 0) {
  mask.apply(devices);
  return mapper.map(frame, devices, port0()).pads[0];
}

}  // namespace

TEST_CASE("South held at resume leaves kButtonA clear until released, then it works") {
  Mapper mapper;
  HeldMask mask;
  DeviceSnapshot p = pad();
  p.gamepad.set(PadButton::south, true);  // the press that confirmed "Resume"
  mask.capture(std::vector<DeviceSnapshot>{p});
  CHECK(mask.masked(kPad, PadButton::south));

  // Still held for several stepped frames: A stays clear, the pad stays connected.
  for (std::uint64_t f = 0; f < 5; ++f) {
    const PadState out = step(mapper, mask, {p}, f);
    CHECK(out.connected == 1);
    CHECK((out.buttons & kButtonA) == 0);
  }
  // Released: the mask lets go.
  p.gamepad.set(PadButton::south, false);
  CHECK((step(mapper, mask, {p}).buttons & kButtonA) == 0);
  CHECK(mask.empty());
  // Pressed again: a real press reaches the game.
  p.gamepad.set(PadButton::south, true);
  CHECK((step(mapper, mask, {p}).buttons & kButtonA) != 0);
  CHECK((step(mapper, mask, {p}).buttons & kButtonA) != 0);
}

TEST_CASE("buttons pressed after the capture work immediately") {
  Mapper mapper;
  HeldMask mask;
  DeviceSnapshot p = pad();
  p.gamepad.set(PadButton::south, true);
  mask.capture(std::vector<DeviceSnapshot>{p});
  p.gamepad.set(PadButton::east, true);  // B, not held at capture
  const PadState out = step(mapper, mask, {p});
  CHECK((out.buttons & kButtonA) == 0);
  CHECK((out.buttons & kButtonB) != 0);
}

TEST_CASE("another source for the same control is not masked") {
  // Masking is per physical source: pad South is masked, keyboard K still presses A.
  Mapper mapper;
  HeldMask mask;
  DeviceSnapshot p = pad();
  DeviceSnapshot k = keys();
  p.gamepad.set(PadButton::south, true);
  mask.capture(std::vector<DeviceSnapshot>{p, k});
  CHECK((step(mapper, mask, {p, k}).buttons & kButtonA) == 0);
  k.keyboard.set(key("k"));
  CHECK((step(mapper, mask, {p, k}).buttons & kButtonA) != 0);
}

TEST_CASE("keys held at resume are masked until released, stick keys included") {
  Mapper mapper;
  HeldMask mask;
  DeviceSnapshot k = keys();
  k.keyboard.set(key("enter"));  // Start
  k.keyboard.set(key("w"));      // stick up
  mask.capture(std::vector<DeviceSnapshot>{k});
  PadState out = step(mapper, mask, {k});
  CHECK((out.buttons & kButtonStart) == 0);
  CHECK(out.stick_y == 0);

  k.keyboard.reset(key("w"));
  out = step(mapper, mask, {k});
  CHECK(out.stick_y == 0);
  k.keyboard.set(key("w"));
  out = step(mapper, mask, {k});
  CHECK(out.stick_y == kAxisMax);
  CHECK((out.buttons & kButtonStart) == 0);  // Enter is still held from before
}

TEST_CASE("sticks and triggers are not masked") {
  Mapper mapper;
  HeldMask mask;
  DeviceSnapshot p = pad();
  p.gamepad.set(PadAxis::left_x, 32767);
  p.gamepad.set(PadAxis::right_trigger, 32767);
  mask.capture(std::vector<DeviceSnapshot>{p});
  CHECK(mask.empty());  // nothing digital was held
  const PadState out = step(mapper, mask, {p});
  CHECK(out.stick_x == kAxisMax);
  CHECK(out.trigger_r == 255);
}

TEST_CASE("captures merge, and an unplugged device loses its mask") {
  HeldMask mask;
  DeviceSnapshot p = pad();
  p.gamepad.set(PadButton::south, true);
  mask.capture(std::vector<DeviceSnapshot>{p});
  p.gamepad.set(PadButton::south, false);
  p.gamepad.set(PadButton::north, true);
  mask.capture(std::vector<DeviceSnapshot>{p});
  CHECK(mask.masked(kPad, PadButton::south));
  CHECK(mask.masked(kPad, PadButton::north));

  std::vector<DeviceSnapshot> others{keys()};
  mask.apply(others);  // the pad is not in this frame's snapshot
  CHECK(mask.empty());
}

TEST_CASE("the mask is a pure function of the snapshots it sees") {
  auto run = [] {
    Mapper mapper;
    HeldMask mask;
    DeviceSnapshot p = pad();
    std::vector<std::uint32_t> trail;
    for (std::uint64_t f = 0; f < 32; ++f) {
      p.gamepad.set(PadButton::south, (f / 3) % 2 == 0);
      p.gamepad.set(PadButton::west, (f / 5) % 2 == 1);
      if (f == 4 || f == 17) mask.capture(std::vector<DeviceSnapshot>{p});
      trail.push_back(step(mapper, mask, {p}, f).buttons);
    }
    return trail;
  };
  CHECK(run() == run());
}
