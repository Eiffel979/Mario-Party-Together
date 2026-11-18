#pragma once

// Directional focus navigation for the launcher (ADR 0010 §5: every screen is fully
// usable with a controller). Plain data in, an index out: the screens collect the
// focusable elements of the shown document and focus the one this picks.
//
// Why not RmlUi's own `nav: auto`: RmlUi 6.2 searches only inside the nearest scroll
// container of the focused element, and skips the children of every other scroll
// container. A button inside a scrolling list (the seat devices, the file list) could
// then never reach a button outside it, and the reverse. Here a step first stays inside
// the current scroll container (so a list scrolls row by row), and only when nothing
// lies that way does it search the whole screen, using each element's part that is
// actually on screen.

#include <cstddef>
#include <limits>
#include <optional>
#include <span>

#include "mpt/ui/model/nav_repeat.h"

namespace mpt::ui {

struct NavBox {
  float left = 0;
  float top = 0;
  float right = 0;
  float bottom = 0;
  bool empty() const { return right <= left || bottom <= top; }
};

inline constexpr float kNavUnbounded = std::numeric_limits<float>::max();

struct NavTarget {
  NavBox box;  // border box, in screen coordinates (after scrolling)
  // What its scrolling ancestors show (their viewports intersected); unbounded without one.
  NavBox clip{-kNavUnbounded, -kNavUnbounded, kNavUnbounded, kNavUnbounded};
  int container = -1;  // the nearest scrolling ancestor (any stable number per container), -1 for none

  // The part of `box` on screen; empty when it is scrolled out of view.
  NavBox visible() const;
  // `box` as it is once scrolled into view (the least scrolling that shows it, as the
  // launcher does after every step).
  NavBox scrolled_into_view() const;
};

// The target to focus after a step in `dir` from `targets[current]`, or nullopt when
// nothing lies that way. Ties go to the earlier target (document order). The current
// target counts as scrolled into view.
std::optional<std::size_t> next_focus(std::span<const NavTarget> targets, std::size_t current, NavDir dir);

// How many targets (`start` included) some sequence of steps from `start` reaches. For
// tests: on a screen a controller can fully use, that is all of them.
std::size_t count_reachable(std::span<const NavTarget> targets, std::size_t start);

}  // namespace mpt::ui
