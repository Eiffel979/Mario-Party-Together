#pragma once

// Seating: which physical devices drive which of the 4 ports, with hot-plug.
// Pure bookkeeping driven by add/remove calls; the SDL backend forwards its
// device events here and the UI calls assign()/set_seated() for local seating.

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mpt/core/input.h"
#include "mpt/input/device.h"

namespace mpt::input {

// What the mapper needs: per port, whether a player sits there, and each
// device's port. A seated port reports connected = 1 even with no device, so a
// controller that drops mid-party leaves its player idle, not unplugged.
struct PortLayout {
  std::array<bool, kMaxPlayers> seated{};
  std::map<DeviceId, std::size_t> device_port;
};

class PortAssigner {
 public:
  // Hot-plug with auto-assign on (lobby / seating screen): each new device
  // returns to the port it last used (matched by persistent_key) if that port
  // has no device, else takes the lowest port with no device. Devices never
  // merge onto an occupied port automatically; sharing a port (e.g. keyboard
  // and pad for one player) takes an explicit assign(). A device that finds no
  // free port stays unassigned until the UI seats it.
  //
  // With auto-assign off (in a session) hot-plug never seats a new port, so a
  // pad plugged in mid-party cannot add a player. The only automatic step left
  // is a replugged device returning to its own last port when that port is
  // still seated and has no device. The shell turns auto-assign off when a
  // Session starts and back on when it ends.
  explicit PortAssigner(bool auto_assign = true) : auto_assign_(auto_assign) {}

  void set_auto_assign(bool on) { auto_assign_ = on; }
  bool auto_assign() const { return auto_assign_; }

  void device_added(const DeviceInfo& info);
  void device_removed(DeviceId id);  // the port stays seated; the device's port is remembered

  // Manual seating. assign(id, nullopt) unassigns. Returns false for an
  // unknown device or a port >= kMaxPlayers. Assigning seats the port.
  bool assign(DeviceId id, std::optional<std::size_t> port);
  // Unseating a port also unassigns its devices.
  void set_seated(std::size_t port, bool seated);

  std::optional<std::size_t> port_of(DeviceId id) const;
  bool seated(std::size_t port) const { return port < kMaxPlayers && layout_.seated[port]; }
  std::vector<DeviceId> devices_on(std::size_t port) const;
  const std::vector<DeviceInfo>& devices() const { return devices_; }  // connected devices, in arrival order
  const PortLayout& layout() const { return layout_; }

 private:
  bool port_has_device(std::size_t port) const;
  const DeviceInfo* find(DeviceId id) const;

  bool auto_assign_;
  std::vector<DeviceInfo> devices_;
  PortLayout layout_;
  std::map<std::string, std::size_t> last_port_;  // persistent_key -> port
};

}  // namespace mpt::input
