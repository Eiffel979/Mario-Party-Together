#pragma once

// How the shell routes SDL events and when game input resumes (P1-06 i, ADR 0010 §2.2,
// §3.2, §3.3). Shared by core/shell/app.cpp and by tests that drive a UI the way the
// shell does, so a test can never pass against routing the shell no longer uses.

#include <SDL3/SDL_events.h>

#include "mpt/input/sdl_input.h"
#include "overlay.h"

namespace mpt::shell {

// What the caller must do after route_event().
enum class EventOutcome {
  none,          // handled (or ignored); nothing for the caller to do
  quit,          // SDL_EVENT_QUIT: end the run
  toggle_pause,  // the host pause action with no UI to take it: pause or resume the session
};

// Routes one event:
//   1. SDL_EVENT_QUIT                     -> quit.
//   2. Gamepad hot-plug                   -> always to input/ (seating stays correct while
//                                            a screen is open), then also to a focused UI.
//   3. Window focus gained                -> input/ masks everything held (§3.3), so a key
//                                            held while switching windows is not a press.
//   4. A UI screen has focus              -> to the UI only.
//   5. The host pause action (input::SdlInput::host_action: Esc, Guide)
//                                         -> offered to the UI (it opens its pause screen);
//                                            without a UI, or if the UI declines, toggle_pause.
//                                            input/ never sees it, and sample() strips the
//                                            reserved sources anyway, so it never sets a
//                                            PadState bit.
//   6. Anything else                      -> to input/.
// `overlay` may be null (no UI).
EventOutcome route_event(const SDL_Event& event, IOverlay* overlay, input::SdlInput& input);

// Watches whether game input is live (a session exists, it is not paused, and no UI
// screen has focus). On every change from not-live to live (session start, resume,
// a screen losing focus) it masks held inputs (SdlInput::mask_held_until_release), so
// the press that resumed the game never reaches it, and reports the edge so the caller
// can restart frame pacing. Call update() once per loop iteration, after routing events
// and before FrameLoop::tick, which is before the next sample().
class GameInputGate {
 public:
  // True on a not-live -> live edge (the mask has been taken).
  bool update(bool live, input::SdlInput& input) {
    const bool edge = live && !live_;
    live_ = live;
    if (edge) input.mask_held_until_release();
    return edge;
  }
  bool live() const { return live_; }

 private:
  bool live_ = false;
};

}  // namespace mpt::shell
