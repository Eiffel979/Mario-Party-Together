#include "event_routing.h"

namespace mpt::shell {
namespace {

bool is_hotplug(const SDL_Event& e) { return e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED; }

}  // namespace

EventOutcome route_event(const SDL_Event& event, IOverlay* overlay, input::SdlInput& input) {
  if (event.type == SDL_EVENT_QUIT) return EventOutcome::quit;

  const bool ui_focus = overlay && overlay->hasFocus();
  if (is_hotplug(event)) {
    input.handle_event(event);
    if (ui_focus) overlay->handleEvent(event);
    return EventOutcome::none;
  }
  if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) input.mask_held_until_release();

  if (ui_focus) {
    overlay->handleEvent(event);
    return EventOutcome::none;
  }
  if (input::SdlInput::host_action(event) == input::HostAction::pause) {
    if (overlay && overlay->handleEvent(event)) return EventOutcome::none;
    return EventOutcome::toggle_pause;
  }
  input.handle_event(event);
  return EventOutcome::none;
}

}  // namespace mpt::shell
