// The Back + Start gamepad pause chord through the real SDL backend
// (reserved.h, SdlInput::host_action). Pads are SDL virtual gamepads, so button
// state is synthetic and comes from SDL itself; button events are built by hand
// with the virtual pad's instance ID, as SDL would deliver them. No physical
// device is read or asserted on.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <optional>

#include <SDL3/SDL.h>

#include "mpt/input/sdl_input.h"

using namespace mpt;
using namespace mpt::input;

namespace {

// A virtual gamepad with the buttons South..Start (SDL_GamepadButton 0..6), so
// its joystick button index equals the gamepad button index.
class VirtualPad {
 public:
  VirtualPad() {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_START + 1;
    desc.button_mask = (1u << (SDL_GAMEPAD_BUTTON_START + 1)) - 1u;
    desc.name = "mpt test pad";
    id_ = SDL_AttachVirtualJoystick(&desc);
    REQUIRE_MESSAGE(id_ != 0, "SDL_AttachVirtualJoystick: " << SDL_GetError());
    joystick_ = SDL_OpenJoystick(id_);
    REQUIRE_MESSAGE(joystick_ != nullptr, "SDL_OpenJoystick: " << SDL_GetError());
  }
  ~VirtualPad() {
    SDL_CloseJoystick(joystick_);
    SDL_DetachVirtualJoystick(id_);
  }
  VirtualPad(const VirtualPad&) = delete;
  VirtualPad& operator=(const VirtualPad&) = delete;

  SDL_JoystickID id() const { return id_; }
  DeviceId device() const { return DeviceId{DeviceKind::gamepad, static_cast<std::uint32_t>(id_)}; }

  // Sets the button's state as SDL reports it from now on.
  void set(SDL_GamepadButton button, bool down) {
    REQUIRE(SDL_SetJoystickVirtualButton(joystick_, button, down));
    SDL_UpdateJoysticks();
  }

  // The event SDL would deliver for that change.
  SDL_Event event(SDL_GamepadButton button, bool down) const {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.which = id_;
    e.gbutton.button = static_cast<Uint8>(button);
    e.gbutton.down = down;
    return e;
  }

  // Changes the state, then returns the host action for the resulting event.
  std::optional<HostAction> press(SDL_GamepadButton button) {
    set(button, true);
    return SdlInput::host_action(event(button, true));
  }

 private:
  SDL_JoystickID id_ = 0;
  SDL_Joystick* joystick_ = nullptr;
};

// The two pads and the backend that opened them (keyboard on port 0, pads after).
struct Rig {
  VirtualPad a;
  VirtualPad b;
  std::optional<SdlInput> input;
  Rig() {
    SDL_UpdateJoysticks();
    input.emplace();
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);  // the added events; SdlInput opened both already
    REQUIRE(SDL_GetGamepadFromID(a.id()) != nullptr);
    REQUIRE(SDL_GetGamepadFromID(b.id()) != nullptr);
    REQUIRE(input->ports().port_of(a.device()).has_value());
    REQUIRE(input->ports().port_of(b.device()).has_value());
  }
  std::uint32_t buttons(const VirtualPad& pad, std::uint64_t frame) {
    return input->sample(frame).pads[*input->ports().port_of(pad.device())].buttons;
  }
};

}  // namespace

TEST_CASE("hold Back, then press Start: the host pause action") {
  Rig rig;
  CHECK_FALSE(rig.a.press(SDL_GAMEPAD_BUTTON_BACK).has_value());
  CHECK(rig.buttons(rig.a, 0) == 0);  // Back alone presses nothing with the built-in default
  CHECK(rig.a.press(SDL_GAMEPAD_BUTTON_START) == HostAction::pause);
}

TEST_CASE("Back and Start landing in one event poll count as Back first") {
  // Both changed before the events are handled: the Start event sees Back held.
  Rig rig;
  rig.a.set(SDL_GAMEPAD_BUTTON_START, true);
  rig.a.set(SDL_GAMEPAD_BUTTON_BACK, true);
  CHECK(SdlInput::host_action(rig.a.event(SDL_GAMEPAD_BUTTON_START, true)) == HostAction::pause);
  CHECK_FALSE(SdlInput::host_action(rig.a.event(SDL_GAMEPAD_BUTTON_BACK, true)).has_value());
}

TEST_CASE("Start alone passes to the game") {
  Rig rig;
  CHECK_FALSE(rig.a.press(SDL_GAMEPAD_BUTTON_START).has_value());
  CHECK((rig.buttons(rig.a, 0) & kButtonStart) != 0);
  rig.a.set(SDL_GAMEPAD_BUTTON_START, false);
  CHECK_FALSE(SdlInput::host_action(rig.a.event(SDL_GAMEPAD_BUTTON_START, false)).has_value());
  CHECK((rig.buttons(rig.a, 1) & kButtonStart) == 0);
}

TEST_CASE("Back alone passes") {
  Rig rig;
  CHECK_FALSE(rig.a.press(SDL_GAMEPAD_BUTTON_BACK).has_value());
  // Not stripped: the raw state still carries it, and a profile could bind it.
  bool seen = false;
  for (const DeviceSnapshot& d : rig.input->snapshot()) {
    if (d.id == rig.a.device()) seen = d.gamepad.pressed(PadButton::back);
  }
  CHECK(seen);
  rig.a.set(SDL_GAMEPAD_BUTTON_BACK, false);
  CHECK_FALSE(SdlInput::host_action(rig.a.event(SDL_GAMEPAD_BUTTON_BACK, false)).has_value());
}

TEST_CASE("Start, then Back: no host action, and Start has reached the game") {
  Rig rig;
  CHECK_FALSE(rig.a.press(SDL_GAMEPAD_BUTTON_START).has_value());
  CHECK((rig.buttons(rig.a, 0) & kButtonStart) != 0);
  CHECK_FALSE(rig.a.press(SDL_GAMEPAD_BUTTON_BACK).has_value());
  CHECK((rig.buttons(rig.a, 1) & kButtonStart) != 0);
}

TEST_CASE("two pads: Back on pad A and Start on pad B does not pause") {
  Rig rig;
  CHECK_FALSE(rig.a.press(SDL_GAMEPAD_BUTTON_BACK).has_value());
  CHECK_FALSE(rig.b.press(SDL_GAMEPAD_BUTTON_START).has_value());
  CHECK((rig.buttons(rig.b, 0) & kButtonStart) != 0);
  // Pad B forming its own chord still works while pad A holds Back.
  rig.b.set(SDL_GAMEPAD_BUTTON_START, false);
  CHECK_FALSE(rig.b.press(SDL_GAMEPAD_BUTTON_BACK).has_value());
  CHECK(rig.b.press(SDL_GAMEPAD_BUTTON_START) == HostAction::pause);
}

TEST_CASE("Guide still pauses, alone or with Back or Start held") {
  Rig rig;
  CHECK(rig.a.press(SDL_GAMEPAD_BUTTON_GUIDE) == HostAction::pause);
  rig.a.set(SDL_GAMEPAD_BUTTON_GUIDE, false);
  rig.a.set(SDL_GAMEPAD_BUTTON_BACK, true);
  CHECK(rig.a.press(SDL_GAMEPAD_BUTTON_GUIDE) == HostAction::pause);
  CHECK(rig.buttons(rig.a, 0) == 0);  // Guide is stripped, Back is unbound
}

TEST_CASE("a pad SDL has not opened never forms the chord") {
  Rig rig;
  SDL_Event e = rig.a.event(SDL_GAMEPAD_BUTTON_START, true);
  e.gbutton.which = 0x7ffffff0;
  CHECK_FALSE(SdlInput::host_action(e).has_value());
}

TEST_CASE("resume masks the chord's held Start and Back until they are released") {
  // What GameInputGate does on resume (core/shell/event_routing.h): it calls
  // mask_held_until_release() before the next sample().
  Rig rig;
  rig.a.set(SDL_GAMEPAD_BUTTON_BACK, true);
  REQUIRE(rig.a.press(SDL_GAMEPAD_BUTTON_START) == HostAction::pause);
  // ... paused; the overlay resumes (on Start, PR #84, or the chord again) ...
  rig.input->mask_held_until_release();
  CHECK(rig.buttons(rig.a, 0) == 0);
  CHECK(rig.buttons(rig.a, 1) == 0);  // still held: still masked
  rig.a.set(SDL_GAMEPAD_BUTTON_BACK, false);
  CHECK(rig.buttons(rig.a, 2) == 0);
  rig.a.set(SDL_GAMEPAD_BUTTON_START, false);
  CHECK(rig.buttons(rig.a, 3) == 0);
  // The next Start is a normal press again.
  CHECK_FALSE(rig.a.press(SDL_GAMEPAD_BUTTON_START).has_value());
  CHECK((rig.buttons(rig.a, 4) & kButtonStart) != 0);
}

int main(int argc, char** argv) {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  doctest::Context context(argc, argv);
  const int result = context.run();
  SDL_Quit();
  return result;
}
