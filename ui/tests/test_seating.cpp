#include <doctest/doctest.h>

#include "mpt/ui/model/seating.h"

using namespace mpt;
using namespace mpt::ui;

namespace {
const input::DeviceId kPadA{input::DeviceKind::gamepad, 11};
const input::DeviceId kPadB{input::DeviceKind::gamepad, 12};
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
  CHECK(seats[2].summary() == "Computer player");
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

TEST_CASE("a party member's unplugged controller keeps the seat") {
  input::PortAssigner ports;
  Seating seating(ports);
  ports.device_added({kPadA, "Controller", "pad-a"});
  ports.device_removed(kPadA);
  CHECK(seating.devices().empty());
  CHECK(seating.ports()[0].summary() == "Controller disconnected");
  CHECK_FALSE(seating.ready());
}
