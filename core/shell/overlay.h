#pragma once

// Shell-internal UI seams (ADR 0010 §2.2 and Review 1). Not core contracts and not
// covered by kCoreContractVersion: changing them needs architect review, not an ADR.
// ui/ implements IOverlay and is handed an IShellCommands; it never constructs a
// Session, never writes an InputFrame and never sees a runtime.

#include <cstdint>
#include <optional>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>

#include "mpt/core/dump.h"

namespace mpt::shell {

// Implemented by ui/, called by the shell on the main thread.
class IOverlay {
 public:
  virtual ~IOverlay() = default;

  // True while a UI screen has input focus. Then every SDL event goes to the UI and the
  // shell does not step the session, so no frame exists that UI input could change
  // (ADR 0010 §3.2).
  virtual bool hasFocus() const = 0;

  // Offered every event while hasFocus(), and otherwise only the host pause action.
  // True = consumed.
  virtual bool handleEvent(const SDL_Event& event) = 0;

  // Once per presented frame, with host time. UI animation may use it; game logic never
  // sees it (ADR 0010 §3.4).
  virtual void update(std::uint64_t host_time_ns) = 0;

  // One pass per presented frame, after the game frame, on the presenter's render
  // target, before present. The UI never reads or modifies FrameOutput.
  virtual void render(SDL_Renderer* renderer, int width_px, int height_px) = 0;
};

// What ui/ may ask of the shell. Requests are queued and applied at the top of the next
// shell loop iteration, never in the middle of a step.
struct StartRequest {
  VerifiedDump dump{};                 // from asset_import::make_verified, never assembled by ui/
  std::optional<std::uint64_t> seed{};  // tests and debugging only; normally the shell picks and records it
};

class IShellCommands {
 public:
  virtual ~IShellCommands() = default;
  virtual void requestStart(const StartRequest& request) = 0;
  virtual void requestPause() = 0;
  virtual void requestResume() = 0;
  virtual void requestStop() = 0;  // ends the session, back to the launcher
  virtual void requestQuit() = 0;  // closes the application
};

}  // namespace mpt::shell
