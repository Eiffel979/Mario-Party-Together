#include <doctest/doctest.h>

#include "mpt/ui/model/nav_repeat.h"

using namespace mpt::ui;

TEST_CASE("a D-pad press steps once, then repeats after the first delay") {
  NavRepeat nav;
  CHECK(nav.dpad(NavDir::down, true, 0) == NavDir::down);
  CHECK(nav.tick(NavRepeat::kFirstRepeatNs - 1).empty());
  CHECK(nav.tick(NavRepeat::kFirstRepeatNs).size() == 1);
  CHECK(nav.tick(NavRepeat::kFirstRepeatNs + NavRepeat::kRepeatNs).size() == 1);
  CHECK(nav.dpad(NavDir::down, false, NavRepeat::kFirstRepeatNs + 2 * NavRepeat::kRepeatNs) == std::nullopt);
  CHECK(nav.tick(10 * NavRepeat::kFirstRepeatNs).empty());
}

TEST_CASE("the stick has a deadzone and hysteresis") {
  NavRepeat nav;
  CHECK(nav.stick(0, NavRepeat::kStickPress - 1, 0) == std::nullopt);
  CHECK(nav.stick(0, NavRepeat::kStickPress, 0) == NavDir::down);  // SDL +Y is down
  CHECK(nav.stick(0, NavRepeat::kStickRelease, 1) == std::nullopt);  // still held, no new step
  CHECK(nav.stick(0, NavRepeat::kStickRelease - 1, 2) == std::nullopt);  // released
  CHECK(nav.stick(-NavRepeat::kStickPress, 0, 3) == NavDir::left);
  CHECK(nav.stick(0, -NavRepeat::kStickPress - 5000, 4) == NavDir::up);
}

TEST_CASE("a long stall does not scroll a whole list") {
  NavRepeat nav;
  nav.dpad(NavDir::up, true, 0);
  CHECK(nav.tick(100 * NavRepeat::kFirstRepeatNs).size() == 3);
}

TEST_CASE("clear forgets held directions") {
  NavRepeat nav;
  nav.dpad(NavDir::right, true, 0);
  nav.clear();
  CHECK(nav.tick(NavRepeat::kFirstRepeatNs * 2).empty());
}
