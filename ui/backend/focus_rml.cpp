#include "focus_rml.h"

#include <algorithm>
#include <map>

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ScrollTypes.h>

namespace mpt::ui {
namespace {

bool clips(Rml::Element* element) {
  const Rml::Style::ComputedValues& c = element->GetComputedValues();
  return c.overflow_x() != Rml::Style::Overflow::Visible || c.overflow_y() != Rml::Style::Overflow::Visible;
}

NavBox box_of(Rml::Element* element, Rml::BoxArea area) {
  const Rml::Vector2f at = element->GetAbsoluteOffset(area);
  const Rml::Vector2f size = element->GetBox().GetSize(area);
  return NavBox{at.x, at.y, at.x + size.x, at.y + size.y};
}

NavBox intersect(const NavBox& a, const NavBox& b) {
  return NavBox{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}

bool focusable(Rml::Element* element) {
  const Rml::Style::ComputedValues& c = element->GetComputedValues();
  return c.tab_index() == Rml::Style::TabIndex::Auto && c.focus() != Rml::Style::Focus::None &&
         element->IsVisible(/*include_ancestors=*/true);
}

void collect(Rml::Element* element, Rml::Element* root, FocusTargets& out, std::map<Rml::Element*, int>& containers) {
  for (int i = 0; i < element->GetNumChildren(); ++i) {
    Rml::Element* child = element->GetChild(i);
    if (!child->IsVisible()) continue;  // hidden subtrees hold nothing to focus
    if (focusable(child)) {
      NavTarget t;
      t.box = box_of(child, Rml::BoxArea::Border);
      for (Rml::Element* up = child->GetParentNode(); up && up != root; up = up->GetParentNode()) {
        if (!clips(up)) continue;
        t.clip = intersect(t.clip, box_of(up, Rml::BoxArea::Padding));
        if (t.container < 0) {
          const auto [it, added] = containers.emplace(up, static_cast<int>(containers.size()));
          t.container = it->second;
        }
      }
      out.elements.push_back(child);
      out.targets.push_back(t);
    }
    collect(child, root, out, containers);
  }
}

}  // namespace

FocusTargets collect_focus_targets(Rml::Element* root) {
  FocusTargets out;
  if (!root) return out;
  std::map<Rml::Element*, int> containers;
  collect(root, root, out, containers);
  return out;
}

bool step_focus(Rml::Element* doc, NavDir dir) {
  if (!doc || !doc->GetContext()) return false;
  const FocusTargets found = collect_focus_targets(doc);
  Rml::Element* focus = doc->GetContext()->GetFocusElement();
  const auto it = std::find(found.elements.begin(), found.elements.end(), focus);
  if (it == found.elements.end()) return false;
  const auto current = static_cast<std::size_t>(it - found.elements.begin());
  const std::optional<std::size_t> next = next_focus(found.targets, current, dir);
  if (!next) return false;
  Rml::Element* element = found.elements[*next];
  if (!element->Focus(/*focus_visible=*/true)) return false;
  element->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
  return true;
}

}  // namespace mpt::ui
