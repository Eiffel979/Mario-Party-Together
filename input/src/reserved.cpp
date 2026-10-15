#include "mpt/input/reserved.h"

namespace mpt::input {

std::optional<HostAction> host_action_for_key(std::uint16_t hid_usage) {
  if (hid_usage == kHostPauseKey) return HostAction::pause;
  return std::nullopt;
}

std::optional<HostAction> host_action_for_button(PadButton button) {
  if (button == kHostPauseButton) return HostAction::pause;
  return std::nullopt;
}

std::optional<HostAction> host_action_for(const Source& source) {
  switch (source.kind) {
    case Source::Kind::key:
      return host_action_for_key(source.code);
    case Source::Kind::pad_button:
      return host_action_for_button(static_cast<PadButton>(source.code));
    case Source::Kind::pad_axis_positive:
    case Source::Kind::pad_axis_negative:
      return std::nullopt;
  }
  return std::nullopt;
}

void strip_reserved(std::span<DeviceSnapshot> devices) {
  for (DeviceSnapshot& d : devices) {
    d.keyboard.reset(kHostPauseKey);
    d.gamepad.set(kHostPauseButton, false);
  }
}

}  // namespace mpt::input
