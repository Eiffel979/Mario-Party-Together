#include <doctest/doctest.h>

#include "mpt/ui/model/seating.h"

using namespace mpt;
using namespace mpt::ui;

namespace {
const input::DeviceId kPadA{input::DeviceKind::gamepad, 11};
const input::DeviceId kPadB{input::DeviceKind::gamepad, 12};
const input::DeviceId kPadC{input::DeviceKind::gamepad, 13};
}  // namespace

TEST_CASE("hot-plugged devices appear with their auto-assigned seats") {
  input::PortAssigner ports;
  Seating seating(ports);
  CHECK_FALSE(seating.ready());
  ports.device_added({input::kKeyboardDevice, "Keyboard", "kb"});
  ports.device_added({kPadA, "Controller", "pad-a"});
  const auto devices = seating.devices();
  REQUIRE(devices.size() == 2);
  CHECK(devices[0].port_label() == "Player 1");
  CHECK(devices[1].port_label() == "Player 2");
  CHECK(seating.ready());
  CHECK(seating.players() == 2);
  CHECK(seating.cpu_note().find("2 seats") != std::string::npos);
  const auto seats = seating.ports();
  CHECK(seats[0].summary() == "Keyboard");
  CHECK(seats[2].summary() == "Computer (CPU)");
}

TEST_CASE("cycling moves a device through the seats and out") {
  input::PortAssigner ports;
  Seating seating(ports);
  ports.device_added({kPadA, "Controller", "pad-a"});  // Player 1
  seating.cycle(kPadA, +1);
  CHECK(ports.port_of(kPadA) == 1u);
  CHECK_FALSE(ports.seated(0));  // the seat it left has nobody now
  seating.cycle(kPadA, +1);
  seating.cycle(kPadA, +1);
  CHECK(ports.port_of(kPadA) == 3u);
  seating.cycle(kPadA, +1);
  CHECK_FALSE(ports.port_of(kPadA));
  CHECK(seating.players() == 0);
  seating.cycle(kPadA, -1);
  CHECK(ports.port_of(kPadA) == 3u);
}

TEST_CASE("an unseated device joins the lowest free seat") {
  input::PortAssigner ports;
  Seating seating(ports);
  ports.device_added({kPadA, "Controller", "pad-a"});
  ports.device_added({kPadB, "Controller", "pad-b"});
  seating.leave(kPadA);
  CHECK_FALSE(ports.port_of(kPadA));
  CHECK_FALSE(ports.seated(0));
  CHECK(seating.join(kPadA) == 0u);
  CHECK_FALSE(seating.join(kPadA));  // already seated
}

TEST_CASE("seating is locked while a session runs (Phase 1)") {
  input::PortAssigner ports;
  Seating seating(ports);
  ports.device_added({kPadA, "Controller", "pad-a"});  // Player 1
  ports.device_added({kPadB, "Controller", "pad-b"});  // Player 2
  seating.leave(kPadB);
  seating.set_locked(true);
  CHECK(seating.locked());
  seating.cycle(kPadA, +1);
  CHECK(ports.port_of(kPadA) == 0u);
  seating.leave(kPadA);
  CHECK(ports.port_of(kPadA) == 0u);
  CHECK_FALSE(seating.join(kPadB));
  CHECK_FALSE(ports.port_of(kPadB));
  seating.set_locked(false);
  CHECK(seating.join(kPadB) == 1u);
}

TEST_CASE("a party member's unplugged controller keeps the seat") {
  input::PortAssigner ports;
  Seating seating(ports);
  ports.device_added({kPadA, "Controller", "pad-a"});
  ports.device_removed(kPadA);
  CHECK(seating.devices().empty());
  CHECK(seating.ports()[0].summary() == "Controller disconnected");
  CHECK_FALSE(seating.ready());
}

TEST_CASE("each seat shows the keyboard, the controller's own name, or CPU") {
  input::PortAssigner ports;
  Seating seating(ports);
  ports.device_added({input::kKeyboardDevice, "Keyboard", "kb"});
  ports.device_added({kPadA, "Synthetic Pad Pro", "pad-a"});
  const auto seats = seating.ports();
  CHECK(seats[0].summary() == "Keyboard");
  CHECK(seats[1].summary() == "Synthetic Pad Pro");
  CHECK(seats[2].summary() == "Computer (CPU)");
  CHECK(seats[3].summary() == "Computer (CPU)");
  // Two devices on one seat, by the user's choice.
  seating.cycle(kPadA, -1);
  CHECK(seating.ports()[0].summary() == "Keyboard + Synthetic Pad Pro");
}

TEST_CASE("keyboard + controller seats 2 humans and leaves 2 seats to the CPU (M4-U1)") {
  input::PortAssigner ports;
  Seating seating(ports);
  CHECK_FALSE(seating.can_seat_keyboard_and_controller());
  ports.device_added({kPadA, "Pad A", "pad-a"});  // Player 1 by hot-plug
  CHECK_FALSE(seating.can_seat_keyboard_and_controller());  // no keyboard yet
  CHECK_FALSE(seating.seat_keyboard_and_controller());
  CHECK(ports.port_of(kPadA) == 0u);  // unchanged
  ports.device_added({input::kKeyboardDevice, "Keyboard", "kb"});  // Player 2
  ports.device_added({kPadB, "Pad B", "pad-b"});                     // Player 3
  ports.device_added({kPadC, "Pad C", "pad-c"});                     // Player 4
  CHECK(seating.players() == 4);
  REQUIRE(seating.can_seat_keyboard_and_controller());

  REQUIRE(seating.seat_keyboard_and_controller());
  CHECK(ports.port_of(input::kKeyboardDevice) == 0u);
  CHECK(ports.port_of(kPadA) == 1u);  // the seated controller on the lowest seat
  CHECK_FALSE(ports.port_of(kPadB));
  CHECK_FALSE(ports.port_of(kPadC));
  CHECK(seating.players() == 2);
  CHECK(seating.ready());
  const auto seats = seating.ports();
  CHECK(seats[0].summary() == "Keyboard");
  CHECK(seats[1].summary() == "Pad A");
  CHECK(seats[2].summary() == "Computer (CPU)");
  CHECK(seats[3].summary() == "Computer (CPU)");
  CHECK(seating.cpu_note().find("2 seats") != std::string::npos);
}

TEST_CASE("keyboard + controller takes the controller that chose it") {
  input::PortAssigner ports;
  Seating seating(ports);
  ports.device_added({input::kKeyboardDevice, "Keyboard", "kb"});
  ports.device_added({kPadA, "Pad A", "pad-a"});
  ports.device_added({kPadB, "Pad B", "pad-b"});
  REQUIRE(seating.seat_keyboard_and_controller(kPadB));
  CHECK(ports.port_of(kPadB) == 1u);
  CHECK_FALSE(ports.port_of(kPadA));
  // The keyboard is not a controller, and an unknown pad falls back to the rule.
  REQUIRE(seating.seat_keyboard_and_controller(input::kKeyboardDevice));
  CHECK(ports.port_of(kPadB) == 1u);
  REQUIRE(seating.seat_keyboard_and_controller(input::DeviceId{input::DeviceKind::gamepad, 99}));
  CHECK(ports.port_of(kPadB) == 1u);
}

TEST_CASE("keyboard + controller with no seated controller takes the first one connected") {
  input::PortAssigner ports;
  Seating seating(ports);
  ports.device_added({input::kKeyboardDevice, "Keyboard", "kb"});
  ports.device_added({kPadA, "Pad A", "pad-a"});
  ports.device_added({kPadB, "Pad B", "pad-b"});
  seating.leave(kPadA);
  seating.leave(kPadB);
  REQUIRE(seating.seat_keyboard_and_controller());
  CHECK(ports.port_of(kPadA) == 1u);
}

TEST_CASE("keyboard + controller does nothing while seating is locked") {
  input::PortAssigner ports;
  Seating seating(ports);
  ports.device_added({kPadA, "Pad A", "pad-a"});
  ports.device_added({input::kKeyboardDevice, "Keyboard", "kb"});
  seating.set_locked(true);
  CHECK_FALSE(seating.can_seat_keyboard_and_controller());
  CHECK_FALSE(seating.seat_keyboard_and_controller());
  CHECK(ports.port_of(kPadA) == 0u);
  CHECK(ports.port_of(input::kKeyboardDevice) == 1u);
}
