#pragma once

// Gamepad D-pad and left stick to menu navigation, with a stick deadzone and host-timed
// key repeat (ADR 0010 §5). Plain data in, plain directions out: the SDL backend feeds
// it and turns the directions into RmlUi navigation keys.

#include <cstdint>
#include <optional>
#include <vector>

namespace mpt::ui {

enum class NavDir { up, down, left, right };

class NavRepeat {
 public:
  static constexpr std::uint64_t kFirstRepeatNs = 400'000'000;  // hold this long before repeating
  static constexpr std::uint64_t kRepeatNs = 110'000'000;       // then one step per interval
  static constexpr int kStickPress = 16000;                     // about half travel
  static constexpr int kStickRelease = 12000;                   // hysteresis

  // A D-pad direction went down or up. Returns the step to take now, if any.
  std::optional<NavDir> dpad(NavDir dir, bool down, std::uint64_t now_ns);
  // The left stick moved (SDL orientation: +Y down). Returns the step to take now, if any.
  std::optional<NavDir> stick(int x, int y, std::uint64_t now_ns);
  // Repeat steps that are due.
  std::vector<NavDir> tick(std::uint64_t now_ns);
  // Forget held directions (a screen changed, or the pad was removed).
  void clear();

 private:
  std::optional<NavDir> press(std::optional<NavDir>& slot, std::optional<NavDir> dir, std::uint64_t now_ns);

  std::optional<NavDir> dpad_held_;
  std::optional<NavDir> stick_held_;
  std::optional<NavDir> repeating_;
  std::uint64_t next_repeat_ns_ = 0;
};

}  // namespace mpt::ui
