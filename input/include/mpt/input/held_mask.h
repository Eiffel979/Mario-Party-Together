#pragma once

// Masks inputs held across a focus change until they are released
// (ADR 0010 §3.3). When the pause overlay closes, the South press that
// confirmed "Resume" is still down on the first stepped frames; without a mask
// it would reach the game as A. The shell calls SdlInput::mask_held_until_release()
// at resume, when a UI screen loses focus, at session start and when the window
// regains focus. Pure: the mask is a function of the snapshots it is given.
//
// What is masked: **digital physical sources only**, i.e. every keyboard key
// and every gamepad button held at capture, whatever control it is bound to.
// Masking by physical source rather than by control keeps the result the same
// whichever profile or minigame override is active when the frame is mapped,
// and lets a different source for the same control (keyboard K while pad South
// is masked) press it straight away. Keys bound to stick directions (WASD) are
// digital too, so they are masked as well.
//
// Not masked: stick and trigger axes. They are continuous state, menus do not
// leave them meaningfully "pressed", and masking a held stick would make the
// player's character ignore a deliberate hold. As a consequence an analog
// trigger past `trigger_button_threshold`, or an axis bound to a button
// control, can press a button on the first frame.
//
// Masking happens left of the InputFrame boundary: the frame the pads report
// is what the replay records and what netplay sends, so determinism holds.

#include <cstdint>
#include <span>
#include <vector>

#include "mpt/input/device.h"

namespace mpt::input {

class HeldMask {
 public:
  // Adds every key and gamepad button held in `devices` to the mask. Inputs
  // already masked stay masked (captures merge).
  void capture(std::span<const DeviceSnapshot> devices);

  // For each device: masked inputs that are now released leave the mask for
  // good; masked inputs still held are cleared in the snapshot. A device that
  // is absent from `devices` (unplugged) loses its mask. Call once per mapped
  // frame, before mapping.
  void apply(std::span<DeviceSnapshot> devices);

  void clear() { entries_.clear(); }
  bool empty() const { return entries_.empty(); }
  // True if `button` on gamepad `id`, or key `key` on the keyboard, is masked.
  bool masked(DeviceId id, PadButton button) const;
  bool masked_key(std::uint16_t key) const;

 private:
  struct Entry {
    DeviceId id;
    KeyboardState keys;
    std::uint32_t buttons = 0;
    bool any() const { return keys.any() || buttons != 0; }
  };
  const Entry* find(DeviceId id) const;

  std::vector<Entry> entries_;
};

}  // namespace mpt::input
