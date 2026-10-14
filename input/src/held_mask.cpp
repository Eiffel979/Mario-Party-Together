#include "mpt/input/held_mask.h"

#include <algorithm>
#include <utility>

namespace mpt::input {

void HeldMask::capture(std::span<const DeviceSnapshot> devices) {
  for (const DeviceSnapshot& d : devices) {
    Entry held;
    held.id = d.id;
    if (d.id.kind == DeviceKind::keyboard) {
      held.keys = d.keyboard;
    } else {
      held.buttons = d.gamepad.buttons;
    }
    if (!held.any()) continue;
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.id == d.id; });
    if (it == entries_.end()) {
      entries_.push_back(held);
    } else {
      it->keys |= held.keys;
      it->buttons |= held.buttons;
    }
  }
}

void HeldMask::apply(std::span<DeviceSnapshot> devices) {
  std::vector<Entry> kept;
  kept.reserve(entries_.size());
  for (Entry e : entries_) {
    const auto it = std::find_if(devices.begin(), devices.end(), [&](const DeviceSnapshot& d) { return d.id == e.id; });
    if (it == devices.end()) continue;  // device gone: everything on it is released
    if (e.id.kind == DeviceKind::keyboard) {
      e.keys &= it->keyboard;  // released keys leave the mask
      it->keyboard &= ~e.keys;
    } else {
      e.buttons &= it->gamepad.buttons;
      it->gamepad.buttons &= ~e.buttons;
    }
    if (e.any()) kept.push_back(e);
  }
  entries_ = std::move(kept);
}

const HeldMask::Entry* HeldMask::find(DeviceId id) const {
  const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.id == id; });
  return it == entries_.end() ? nullptr : &*it;
}

bool HeldMask::masked(DeviceId id, PadButton button) const {
  const Entry* e = find(id);
  return e && ((e->buttons >> static_cast<unsigned>(button)) & 1u);
}

bool HeldMask::masked_key(std::uint16_t key) const {
  const Entry* e = find(kKeyboardDevice);
  return e && key < kKeyCount && e->keys.test(key);
}

}  // namespace mpt::input
