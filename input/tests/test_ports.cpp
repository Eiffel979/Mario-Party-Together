#include <doctest/doctest.h>

#include <string>
#include <utility>

#include "mpt/input/ports.h"

using namespace mpt::input;

namespace {

DeviceInfo pad(std::uint32_t instance, std::string key) {
  return DeviceInfo{DeviceId{DeviceKind::gamepad, instance}, "Synthetic pad", std::move(key)};
}
DeviceInfo keyboard() { return DeviceInfo{kKeyboardDevice, "Keyboard", "keyboard"}; }

}  // namespace

TEST_CASE("auto-assignment: keyboard on port 0, gamepads fill ports without a gamepad") {
  PortAssigner ports;
  ports.device_added(keyboard());
  for (std::uint32_t i = 0; i < 5; ++i) ports.device_added(pad(100 + i, "guid-" + std::to_string(i)));

  CHECK(ports.port_of(kKeyboardDevice) == 0u);
  CHECK(ports.port_of(pad(100, "").id) == 0u);  // keyboard and first pad share port 0
  CHECK(ports.port_of(pad(101, "").id) == 1u);
  CHECK(ports.port_of(pad(102, "").id) == 2u);
  CHECK(ports.port_of(pad(103, "").id) == 3u);
  CHECK_FALSE(ports.port_of(pad(104, "").id).has_value());  // fifth pad waits for the UI
  for (std::size_t p = 0; p < mpt::kMaxPlayers; ++p) CHECK(ports.seated(p));
  CHECK(ports.devices().size() == 6);
  CHECK(ports.devices_on(0).size() == 2);
}

TEST_CASE("duplicate add is ignored") {
  PortAssigner ports;
  ports.device_added(pad(1, "a"));
  ports.device_added(pad(1, "a"));
  CHECK(ports.devices().size() == 1);
  ports.device_added(pad(2, "b"));
  CHECK(ports.port_of(pad(2, "").id) == 1u);
}

TEST_CASE("unplugging keeps the seat; replugging returns to the old port") {
  PortAssigner ports;
  ports.device_added(pad(1, "guid-a"));
  ports.device_added(pad(2, "guid-b"));
  ports.device_added(pad(3, "guid-c"));
  REQUIRE(ports.port_of(pad(2, "").id) == 1u);

  ports.device_removed(pad(2, "").id);
  CHECK(ports.seated(1));  // the player stays in the party, idle
  CHECK(ports.devices_on(1).empty());
  CHECK(ports.devices().size() == 2);

  // Same controller, new SDL instance ID.
  ports.device_added(pad(9, "guid-b"));
  CHECK(ports.port_of(pad(9, "").id) == 1u);
}

TEST_CASE("a replugged pad whose old port was taken goes to the lowest free port") {
  PortAssigner ports;
  ports.device_added(pad(1, "guid-a"));  // port 0
  ports.device_added(pad(2, "guid-b"));  // port 1
  ports.device_removed(pad(1, "").id);
  ports.device_added(pad(3, "guid-c"));  // takes the free port 0
  CHECK(ports.port_of(pad(3, "").id) == 0u);
  ports.device_added(pad(4, "guid-a"));  // its old port 0 now has a pad
  CHECK(ports.port_of(pad(4, "").id) == 2u);
}

TEST_CASE("manual seating") {
  PortAssigner ports(/*auto_assign=*/false);
  ports.device_added(keyboard());
  ports.device_added(pad(1, "guid-a"));
  CHECK_FALSE(ports.port_of(kKeyboardDevice).has_value());
  CHECK_FALSE(ports.seated(0));

  CHECK(ports.assign(pad(1, "").id, 2));
  CHECK(ports.seated(2));
  CHECK(ports.port_of(pad(1, "").id) == 2u);
  CHECK(ports.assign(kKeyboardDevice, 2));  // two devices may drive one port
  CHECK(ports.devices_on(2).size() == 2);

  CHECK_FALSE(ports.assign(pad(1, "").id, 4));  // out of range
  CHECK_FALSE(ports.assign(pad(77, "").id, 0));  // unknown device

  CHECK(ports.assign(kKeyboardDevice, std::nullopt));
  CHECK_FALSE(ports.port_of(kKeyboardDevice).has_value());
  CHECK(ports.seated(2));

  ports.set_seated(2, false);  // unseating frees the port's devices
  CHECK_FALSE(ports.seated(2));
  CHECK_FALSE(ports.port_of(pad(1, "").id).has_value());
  CHECK(ports.layout().device_port.empty());
  CHECK_FALSE(ports.seated(99));
}

TEST_CASE("manual reassignment is remembered for replug") {
  PortAssigner ports;
  ports.device_added(pad(1, "guid-a"));
  REQUIRE(ports.assign(pad(1, "").id, 3));
  ports.device_removed(pad(1, "").id);
  ports.device_added(pad(2, "guid-a"));
  CHECK(ports.port_of(pad(2, "").id) == 3u);
}
