#include "mpt/ui/model/seating.h"

namespace mpt::ui {

std::string SeatDevice::port_label() const {
  return port ? "Player " + std::to_string(*port + 1) : std::string("Not playing");
}

std::string SeatPort::summary() const {
  if (!seated) return "Computer player";
  if (devices.empty()) return "Controller disconnected";
  std::string text;
  for (const std::string& name : devices) {
    if (!text.empty()) text += " + ";
    text += name;
  }
  return text;
}

std::vector<SeatDevice> Seating::devices() const {
  std::vector<SeatDevice> out;
  for (const input::DeviceInfo& info : ports_.devices()) {
    out.push_back(SeatDevice{info.id, info.name, ports_.port_of(info.id)});
  }
  return out;
}

std::array<SeatPort, kMaxPlayers> Seating::ports() const {
  std::array<SeatPort, kMaxPlayers> out{};
  for (std::size_t p = 0; p < kMaxPlayers; ++p) {
    out[p].seated = ports_.seated(p);
    for (const input::DeviceId id : ports_.devices_on(p)) {
      for (const input::DeviceInfo& info : ports_.devices()) {
        if (info.id == id) out[p].devices.push_back(info.name);
      }
    }
  }
  return out;
}

std::optional<std::size_t> Seating::join(input::DeviceId id) {
  if (locked_ || ports_.port_of(id)) return std::nullopt;
  for (std::size_t p = 0; p < kMaxPlayers; ++p) {
    if (!ports_.seated(p)) {
      if (!ports_.assign(id, p)) return std::nullopt;
      return p;
    }
  }
  return std::nullopt;
}

void Seating::cycle(input::DeviceId id, int direction) {
  // Choices: 0 = not playing, 1..4 = Player 1..4.
  if (locked_) return;
  constexpr int kChoices = static_cast<int>(kMaxPlayers) + 1;
  const std::optional<std::size_t> now = ports_.port_of(id);
  const int current = now ? static_cast<int>(*now) + 1 : 0;
  const int next = ((current + (direction < 0 ? -1 : 1)) % kChoices + kChoices) % kChoices;
  if (next == 0) {
    leave(id);
    return;
  }
  ports_.assign(id, static_cast<std::size_t>(next - 1));
  // The port it left stays seated only if another device still plays on it.
  if (now && ports_.devices_on(*now).empty()) ports_.set_seated(*now, false);
}

void Seating::leave(input::DeviceId id) {
  if (locked_) return;
  const std::optional<std::size_t> port = ports_.port_of(id);
  ports_.assign(id, std::nullopt);
  if (port && ports_.devices_on(*port).empty()) ports_.set_seated(*port, false);
}

std::size_t Seating::players() const {
  std::size_t n = 0;
  for (std::size_t p = 0; p < kMaxPlayers; ++p) n += ports_.seated(p) ? 1 : 0;
  return n;
}

bool Seating::ready() const {
  for (std::size_t p = 0; p < kMaxPlayers; ++p) {
    if (ports_.seated(p) && !ports_.devices_on(p).empty()) return true;
  }
  return false;
}

std::string Seating::cpu_note() const {
  const std::size_t empty = kMaxPlayers - players();
  if (empty == 0) return "All four seats have a player.";
  return std::to_string(empty) + (empty == 1 ? " seat is" : " seats are") +
         " left for computer players. You choose them in the game's own setup.";
}

}  // namespace mpt::ui
