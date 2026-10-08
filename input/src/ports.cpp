#include "mpt/input/ports.h"

#include <algorithm>
#include <iterator>

namespace mpt::input {

const DeviceInfo* PortAssigner::find(DeviceId id) const {
  for (const DeviceInfo& d : devices_) {
    if (d.id == id) return &d;
  }
  return nullptr;
}

bool PortAssigner::port_has_gamepad(std::size_t port) const {
  for (const auto& [id, p] : layout_.device_port) {
    if (p == port && id.kind == DeviceKind::gamepad) return true;
  }
  return false;
}

void PortAssigner::device_added(const DeviceInfo& info) {
  if (find(info.id)) return;  // duplicate add (SDL can report startup devices twice)
  devices_.push_back(info);
  if (!auto_assign_) return;

  if (info.id.kind == DeviceKind::keyboard) {
    assign(info.id, 0);
    return;
  }
  if (!info.persistent_key.empty()) {
    if (const auto it = last_port_.find(info.persistent_key); it != last_port_.end() && !port_has_gamepad(it->second)) {
      assign(info.id, it->second);
      return;
    }
  }
  for (std::size_t port = 0; port < kMaxPlayers; ++port) {
    if (!port_has_gamepad(port)) {
      assign(info.id, port);
      return;
    }
  }
}

void PortAssigner::device_removed(DeviceId id) {
  const auto it = std::find_if(devices_.begin(), devices_.end(), [&](const DeviceInfo& d) { return d.id == id; });
  if (it == devices_.end()) return;
  if (const auto port = port_of(id); port && !it->persistent_key.empty()) last_port_[it->persistent_key] = *port;
  layout_.device_port.erase(id);
  devices_.erase(it);
}

bool PortAssigner::assign(DeviceId id, std::optional<std::size_t> port) {
  const DeviceInfo* info = find(id);
  if (!info) return false;
  if (!port) {
    layout_.device_port.erase(id);
    return true;
  }
  if (*port >= kMaxPlayers) return false;
  layout_.device_port[id] = *port;
  layout_.seated[*port] = true;
  if (!info->persistent_key.empty()) last_port_[info->persistent_key] = *port;
  return true;
}

void PortAssigner::set_seated(std::size_t port, bool seated) {
  if (port >= kMaxPlayers) return;
  layout_.seated[port] = seated;
  if (seated) return;
  for (auto it = layout_.device_port.begin(); it != layout_.device_port.end();) {
    it = it->second == port ? layout_.device_port.erase(it) : std::next(it);
  }
}

std::optional<std::size_t> PortAssigner::port_of(DeviceId id) const {
  if (const auto it = layout_.device_port.find(id); it != layout_.device_port.end()) return it->second;
  return std::nullopt;
}

std::vector<DeviceId> PortAssigner::devices_on(std::size_t port) const {
  std::vector<DeviceId> ids;
  for (const DeviceInfo& d : devices_) {
    if (port_of(d.id) == port) ids.push_back(d.id);
  }
  return ids;
}

}  // namespace mpt::input
