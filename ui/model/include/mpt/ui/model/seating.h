#pragma once

// Local-party seating (ADR 0010 §4-5, P1-23): which device plays on which of the 4
// ports. A view over input/'s PortAssigner, which owns the bookkeeping and hot-plug.
// The shell turns auto-assign off when a session starts, so nothing here runs in a
// party. Plain C++: no RmlUi, no SDL.

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "mpt/core/input.h"
#include "mpt/input/device.h"
#include "mpt/input/ports.h"

namespace mpt::ui {

struct SeatDevice {
  input::DeviceId id;
  std::string name;
  std::optional<std::size_t> port;
  std::string port_label() const;  // "Player 2" or "Not playing"
};

struct SeatPort {
  bool seated = false;
  std::vector<std::string> devices;  // names of the devices on this port
  std::string summary() const;      // "Keyboard" / "Computer player" / "Controller (disconnected)"
};

class Seating {
 public:
  explicit Seating(input::PortAssigner& ports) : ports_(ports) {}

  std::vector<SeatDevice> devices() const;
  std::array<SeatPort, kMaxPlayers> ports() const;

  // "Press any button to join": an unseated device takes the lowest port that is not
  // seated. Returns its port, or nullopt if it was already seated or all 4 are taken.
  std::optional<std::size_t> join(input::DeviceId id);
  // Moves a device to the next (+1) or previous (-1) choice in: not playing, Player 1..4.
  // Two devices may share a port only because the user chose it here.
  void cycle(input::DeviceId id, int direction);
  // Takes a device out; its port stops being seated once it has no device left.
  void leave(input::DeviceId id);

  std::size_t players() const;     // seated ports
  bool ready() const;              // at least one seated port with a device
  std::string cpu_note() const;    // what happens to the empty seats

 private:
  input::PortAssigner& ports_;
};

}  // namespace mpt::ui
