#include "mpt/ui/model/nav_repeat.h"

#include <cstdlib>

namespace mpt::ui {

std::optional<NavDir> NavRepeat::press(std::optional<NavDir>& slot, std::optional<NavDir> dir, std::uint64_t now_ns) {
  if (slot == dir) return std::nullopt;  // no change
  slot = dir;
  if (!dir) {
    // Released: repeat whatever the other source still holds, without a new step.
    repeating_ = dpad_held_ ? dpad_held_ : stick_held_;
    next_repeat_ns_ = now_ns + kFirstRepeatNs;
    return std::nullopt;
  }
  repeating_ = dir;
  next_repeat_ns_ = now_ns + kFirstRepeatNs;
  return dir;
}

std::optional<NavDir> NavRepeat::dpad(NavDir dir, bool down, std::uint64_t now_ns) {
  if (!down) {
    if (dpad_held_ != dir) return std::nullopt;  // a different direction is the held one
    return press(dpad_held_, std::nullopt, now_ns);
  }
  return press(dpad_held_, dir, now_ns);
}

std::optional<NavDir> NavRepeat::stick(int x, int y, std::uint64_t now_ns) {
  const int ax = std::abs(x);
  const int ay = std::abs(y);
  std::optional<NavDir> dir;
  // Keep the held direction until the stick falls below the release threshold.
  const int threshold = stick_held_ ? kStickRelease : kStickPress;
  if (ax >= threshold || ay >= threshold) {
    if (ay >= ax) {
      dir = y < 0 ? NavDir::up : NavDir::down;
    } else {
      dir = x < 0 ? NavDir::left : NavDir::right;
    }
  }
  return press(stick_held_, dir, now_ns);
}

std::vector<NavDir> NavRepeat::tick(std::uint64_t now_ns) {
  std::vector<NavDir> steps;
  if (!repeating_ || (!dpad_held_ && !stick_held_)) return steps;
  // At most a few steps per tick, so a long stall does not scroll through a whole list.
  for (int i = 0; i < 3 && now_ns >= next_repeat_ns_; ++i) {
    steps.push_back(*repeating_);
    next_repeat_ns_ += kRepeatNs;
  }
  if (now_ns >= next_repeat_ns_) next_repeat_ns_ = now_ns + kRepeatNs;
  return steps;
}

void NavRepeat::clear() {
  dpad_held_.reset();
  stick_held_.reset();
  repeating_.reset();
}

}  // namespace mpt::ui
