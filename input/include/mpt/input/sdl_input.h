#pragma once

// SDL3 device backend. This is the only part of input/ that talks to SDL: it
// tracks keyboard and gamepad hot-plug, reads raw device state, and hands
// plain DeviceSnapshots to the pure mapper. Host-side only; it reads no clock.

#include <cstdint>
#include <optional>
#include <vector>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>

#include "mpt/core/input.h"
#include "mpt/input/device.h"
#include "mpt/input/mapper.h"
#include "mpt/input/ports.h"
#include "mpt/input/reserved.h"

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
  // never sets a bit in the frame.
  InputFrame sample(std::uint64_t frame);

  // Snapshot of every connected device (used by sample(); exposed for the UI).
  std::vector<DeviceSnapshot> snapshot() const;

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
};

}  // namespace mpt::input
