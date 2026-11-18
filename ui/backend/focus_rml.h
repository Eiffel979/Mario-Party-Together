#pragma once

// RmlUi side of the launcher's directional focus (ui/model focus_nav.h): reads the
// focusable elements of a shown document and moves its focus. Arrow keys, the D-pad
// and the left stick all step through here, never through RmlUi's own `nav` search,
// which cannot leave or enter a scroll container (see focus_nav.h).

#include <vector>

#include <RmlUi/Core/Element.h>

#include "mpt/ui/model/focus_nav.h"

namespace mpt::ui {

struct FocusTargets {
  std::vector<Rml::Element*> elements;  // document order
  std::vector<NavTarget> targets;       // same order
};

// Every visible element under `root` that can take focus (tab-index: auto), with its
// box, its on-screen part and its nearest scrolling ancestor.
FocusTargets collect_focus_targets(Rml::Element* root);

// Moves the focus of `doc` one step in `dir`, scrolling the new element into view.
// Returns false when the focus is not on one of the document's targets, or nothing lies
// that way (the focus stays put).
bool step_focus(Rml::Element* doc, NavDir dir);

}  // namespace mpt::ui
