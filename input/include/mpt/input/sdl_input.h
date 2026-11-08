#pragma once

// SDL3 device backend. This is the only part of input/ that talks to SDL: it
// tracks keyboard and gamepad hot-plug, reads raw device state, and hands
// plain DeviceSnapshots to the pure mapper. Host-side only; it reads no clock.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>

#include "mpt/core/input.h"
#include "mpt/core/title_id.h"
#include "mpt/input/device.h"
#include "mpt/input/held_mask.h"
#include "mpt/input/mapper.h"
#include "mpt/input/ports.h"
#include "mpt/input/reserved.h"
#include "mpt/input/title_profile.h"

namespace mpt::input {

class SdlInput {
 public:
  // Requires SDL_Init(SDL_INIT_GAMEPAD) (and video for the keyboard) to have
  // succeeded. Registers the keyboard and opens every gamepad already attached.
  explicit SdlInput(bool auto_assign = true);
  ~SdlInput();
  SdlInput(const SdlInput&) = delete;
  SdlInput& operator=(const SdlInput&) = delete;

  // Feed every SDL event from the shell's poll loop. Handles gamepad add/remove.
  void handle_event(const SDL_Event& event);

  // The host action this event triggers, if any: a non-repeat key-down of Esc or
  // a gamepad button-down of Guide is HostAction::pause (reserved.h). The shell
  // checks this before routing events, and consumes the event when it is set.
  static std::optional<HostAction> host_action(const SDL_Event& event);

  // Reads all devices once and maps them. Call exactly once per emulated frame,
  // after polling events; the returned InputFrame is the only output. Reserved
  // keys and buttons (Esc, Guide) are cleared before mapping, so holding one
  // never sets a bit in the frame, and the held-input mask is applied.
  InputFrame sample(std::uint64_t frame);

  // Every key and gamepad button held right now stays clear in the frames
  // sample() returns until it is released; it works normally from its next
  // press. Axes are not masked (held_mask.h). The shell calls this whenever
  // game input resumes after host-side input: resume from pause, a UI screen
  // losing focus, session start, and the window regaining focus.
  void mask_held_until_release();
  const HeldMask& held_mask() const { return held_; }

  // Snapshot of every connected device (used by sample(); exposed for the UI).
  std::vector<DeviceSnapshot> snapshot() const;

  // Where installed title profiles are looked up: "<executable dir>/profiles"
  // (SDL_GetBasePath), which the build fills from input/profiles/. Empty if SDL
  // cannot report the executable directory.
  static std::vector<std::filesystem::path> profile_search_dirs();

  // Loads `title`'s profile and its per-minigame overrides into mapper() from
  // profile_search_dirs() (title_profile.h). Missing or invalid files fall back
  // to the built-in default with an SDL_LogWarn; this never fails or blocks a
  // session. The shell calls it when a session starts, before the first sample().
  // Replays carry mapped InputFrames, so the profile does not affect determinism.
  TitleProfileLoad load_title_profile(TitleId title);

  PortAssigner& ports() { return ports_; }
  const PortAssigner& ports() const { return ports_; }
  Mapper& mapper() { return mapper_; }
  const Mapper& mapper() const { return mapper_; }

 private:
  void open_gamepad(SDL_JoystickID id);
  void close_gamepad(SDL_JoystickID id);

  struct OpenPad {
    SDL_JoystickID id;
    SDL_Gamepad* handle;
  };
  std::vector<OpenPad> pads_;
  PortAssigner ports_;
  Mapper mapper_;
  HeldMask held_;
};

}  // namespace mpt::input
