#include "mpt/ui/model/focus_nav.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace mpt::ui {
namespace {

// Layout rounding can leave neighbours overlapping by a fraction of a pixel.
constexpr float kSlack = 0.5f;
// The penalty for lying outside the band the source projects in the step direction, per
// pixel of cross-axis distance (as RmlUi's own heuristic): a target straight ahead always
// beats one off to the side.
constexpr float kCrossAxisFactor = 10'000.0f;

// Lower is better; nullopt when `target` does not lie in `dir` from `source`.
std::optional<float> score(const NavBox& source, const NavBox& target, NavDir dir) {
  float main = 0;
  float cross = 0;
  switch (dir) {
    case NavDir::down:
      main = target.top - source.bottom;
      break;
    case NavDir::up:
      main = source.top - target.bottom;
      break;
    case NavDir::right:
      main = target.left - source.right;
      break;
    case NavDir::left:
      main = source.left - target.right;
      break;
  }
  if (main < -kSlack) return std::nullopt;
  if (dir == NavDir::up || dir == NavDir::down) {
    cross = std::max(0.0f, target.left - source.right) + std::max(0.0f, source.left - target.right);
  } else {
    cross = std::max(0.0f, target.top - source.bottom) + std::max(0.0f, source.top - target.bottom);
  }
  return std::max(0.0f, main) + kCrossAxisFactor * cross;
}

// inside_list: only the current scroll container, by full box, so a step reaches the next
// row even when it is scrolled out of view. Otherwise: every target outside the current
// container, by the part that is on screen.
std::optional<std::size_t> best(std::span<const NavTarget> targets, std::size_t current, NavDir dir, bool inside_list) {
  const NavTarget& from = targets[current];
  // Rows of one list move together when it scrolls, so inside it the box as it is now
  // compares; leaving it, the box as it will be once scrolled into view.
  const NavBox source = inside_list ? from.box : from.scrolled_into_view();
  std::optional<std::size_t> pick;
  float pick_score = std::numeric_limits<float>::infinity();
  for (std::size_t i = 0; i < targets.size(); ++i) {
    if (i == current) continue;
    const NavTarget& t = targets[i];
    const bool same = from.container >= 0 && t.container == from.container;
    if (inside_list != same) continue;
    const NavBox box = inside_list ? t.box : t.visible();
    if (box.empty()) continue;
    if (const std::optional<float> s = score(source, box, dir); s && *s < pick_score) {
      pick = i;
      pick_score = *s;
    }
  }
  return pick;
}

}  // namespace

NavBox NavTarget::visible() const {
  return NavBox{std::max(box.left, clip.left), std::max(box.top, clip.top), std::min(box.right, clip.right),
                std::min(box.bottom, clip.bottom)};
}

NavBox NavTarget::scrolled_into_view() const {
  const auto shift = [](float lo, float hi, float view_lo, float view_hi) {
    if (hi - lo >= view_hi - view_lo) return view_lo - lo;  // taller than the view: align the start
    if (lo < view_lo) return view_lo - lo;
    if (hi > view_hi) return view_hi - hi;
    return 0.0f;
  };
  const float dx = shift(box.left, box.right, clip.left, clip.right);
  const float dy = shift(box.top, box.bottom, clip.top, clip.bottom);
  return NavBox{box.left + dx, box.top + dy, box.right + dx, box.bottom + dy};
}

std::optional<std::size_t> next_focus(std::span<const NavTarget> targets, std::size_t current, NavDir dir) {
  if (current >= targets.size()) return std::nullopt;
  if (targets[current].container >= 0) {
    if (const std::optional<std::size_t> inside = best(targets, current, dir, /*inside_list=*/true)) return inside;
  }
  return best(targets, current, dir, /*inside_list=*/false);
}

std::size_t count_reachable(std::span<const NavTarget> targets, std::size_t start) {
  if (start >= targets.size()) return 0;
  std::vector<bool> seen(targets.size(), false);
  std::vector<std::size_t> todo{start};
  seen[start] = true;
  std::size_t count = 1;
  while (!todo.empty()) {
    const std::size_t at = todo.back();
    todo.pop_back();
    for (NavDir dir : {NavDir::up, NavDir::down, NavDir::left, NavDir::right}) {
      if (const std::optional<std::size_t> next = next_focus(targets, at, dir); next && !seen[*next]) {
        seen[*next] = true;
        ++count;
        todo.push_back(*next);
      }
    }
  }
  return count;
}

}  // namespace mpt::ui
