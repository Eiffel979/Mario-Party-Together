// Directional focus over synthetic layouts shaped like the launcher's screens.

#include <doctest/doctest.h>

#include <vector>

#include "mpt/ui/model/focus_nav.h"

using namespace mpt::ui;

namespace {

NavTarget at(float x, float y, float w, float h, int container = -1) {
  NavTarget t;
  t.box = NavBox{x, y, x + w, y + h};
  t.container = container;
  return t;
}

// The Players screen: two device rows in a scrolling list, then a row of three
// side-by-side actions (Start the party, Keyboard + controller, Back).
std::vector<NavTarget> players_screen() {
  std::vector<NavTarget> t;
  t.push_back(at(72, 300, 560, 50, 0));  // 0 Keyboard: not playing
  t.push_back(at(72, 366, 560, 50, 0));  // 1 XInput controller #1
  t.push_back(at(72, 470, 200, 50));     // 2 Start the party
  t.push_back(at(284, 470, 450, 50));    // 3 Keyboard + controller, 2 computer players
  t.push_back(at(746, 470, 100, 50));    // 4 Back
  return t;
}

}  // namespace

TEST_CASE("Players screen: the D-pad leaves the device list and reaches every action") {
  const std::vector<NavTarget> t = players_screen();
  CHECK(next_focus(t, 0, NavDir::down) == 1u);
  CHECK(next_focus(t, 1, NavDir::down) == 2u);  // out of the list: Start the party
  CHECK(next_focus(t, 2, NavDir::right) == 3u);
  CHECK(next_focus(t, 3, NavDir::right) == 4u);
  CHECK(next_focus(t, 4, NavDir::right) == std::nullopt);
  CHECK(next_focus(t, 4, NavDir::left) == 3u);
  CHECK(next_focus(t, 2, NavDir::up) == 1u);  // back into the list, at its last row
  CHECK(next_focus(t, 4, NavDir::up) == 1u);  // from far right too
  CHECK(next_focus(t, 1, NavDir::up) == 0u);
  CHECK(next_focus(t, 0, NavDir::up) == std::nullopt);
  for (std::size_t i = 0; i < t.size(); ++i) CHECK(count_reachable(t, i) == t.size());
}

// The file list: a viewport 300 px high with 10 rows of 60 px scrolled up by `scroll`,
// then Back below it.
std::vector<NavTarget> files_screen(float scroll) {
  const NavBox viewport{72, 100, 692, 400};
  std::vector<NavTarget> t;
  for (int i = 0; i < 10; ++i) {
    NavTarget row = at(72, 100.0f + 60.0f * static_cast<float>(i) - scroll, 620, 50, 7);
    row.clip = viewport;
    t.push_back(row);
  }
  t.push_back(at(72, 420, 560, 50));  // 10 Back
  return t;
}

TEST_CASE("a scrolling list steps row by row, even to rows scrolled out of view") {
  const std::vector<NavTarget> top = files_screen(0);
  REQUIRE(top[5].visible().empty());  // row 5 starts at 400: entirely below the viewport
  CHECK(next_focus(top, 4, NavDir::down) == 5u);  // stays in the list, which scrolls
  // From Back, up goes to the lowest row that is on screen, not to a hidden row.
  CHECK(next_focus(top, 10, NavDir::up) == 4u);
  CHECK(count_reachable(top, 0) == top.size());
  CHECK(count_reachable(top, 10) == top.size());

  const std::vector<NavTarget> bottom = files_screen(300);  // scrolled to the end
  REQUIRE(bottom[0].visible().empty());
  CHECK(next_focus(bottom, 9, NavDir::down) == 10u);  // from the last row: Back
  CHECK(next_focus(bottom, 10, NavDir::up) == 9u);
  CHECK(next_focus(bottom, 5, NavDir::up) == 4u);  // up past the top of the view
}

TEST_CASE("a step prefers the target straight ahead to a nearer one off to the side") {
  std::vector<NavTarget> t;
  t.push_back(at(0, 0, 100, 40));      // 0 source
  t.push_back(at(300, 50, 100, 40));   // 1 near, but off to the side
  t.push_back(at(0, 200, 100, 40));    // 2 far, straight below
  CHECK(next_focus(t, 0, NavDir::down) == 2u);
  t.erase(t.begin() + 2);
  CHECK(next_focus(t, 0, NavDir::down) == 1u);  // the side one when it is the only one
}

TEST_CASE("ties go to the earlier target, and bad input finds nothing") {
  std::vector<NavTarget> t;
  t.push_back(at(100, 0, 100, 40));  // 0 source
  t.push_back(at(0, 100, 100, 40));  // 1 below left
  t.push_back(at(200, 100, 100, 40));  // 2 below right, same distance
  CHECK(next_focus(t, 0, NavDir::down) == 1u);
  CHECK(next_focus(t, 5, NavDir::down) == std::nullopt);
  CHECK(next_focus(std::vector<NavTarget>{}, 0, NavDir::up) == std::nullopt);
  CHECK(count_reachable(std::vector<NavTarget>{}, 0) == 0u);
}

TEST_CASE("separate lists are entered by their visible rows") {
  // Two side-by-side lists, each with a row scrolled out of view at the top.
  const NavBox left_view{0, 100, 300, 400};
  const NavBox right_view{400, 100, 700, 400};
  std::vector<NavTarget> t;
  for (int list = 0; list < 2; ++list) {
    for (int i = 0; i < 3; ++i) {
      NavTarget row = at(list == 0 ? 0.0f : 400.0f, 40.0f + 100.0f * static_cast<float>(i), 300, 50, list);
      row.clip = list == 0 ? left_view : right_view;
      t.push_back(row);
    }
  }
  REQUIRE(t[0].visible().empty());  // y 40..90, above the viewport
  CHECK(next_focus(t, 1, NavDir::right) == 4u);
  CHECK(next_focus(t, 4, NavDir::left) == 1u);
  CHECK(next_focus(t, 1, NavDir::up) == 0u);  // inside its own list, the hidden row counts
  CHECK(count_reachable(t, 1) == t.size());
}
