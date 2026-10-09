// PAD mapping from InputFrame, latched at the VI post-retrace point, and VI
// retrace callbacks (E10, ADR 0007 §4).
#include "harness.h"
#include "mpt_gc/os.h"
#include "mpt_gc/pad.h"
#include "mpt_gc/vi.h"

using namespace mpt;
using namespace mpt::gc;
using namespace mpt::gc::test;

namespace {

PADStatus readOne(const PadState& pad) {
  PADStatus out[4] = {};
  Game g{[&] {
    PADInit();
    PADRead(out);
  }};
  auto sdk = boot(g);
  InputFrame in;
  in.pads[0] = pad;
  REQUIRE(step(*sdk, in) == StepStatus::halted);
  return out[0];
}

}  // namespace

TEST_CASE("canonical buttons map onto the GameCube pad") {
  struct Case {
    std::uint32_t in;
    u16 out;
  } cases[] = {
      {kButtonA, PAD_BUTTON_A},          {kButtonB, PAD_BUTTON_B},           {kButtonX, PAD_BUTTON_X},
      {kButtonY, PAD_BUTTON_Y},          {kButtonStart, PAD_BUTTON_START},   {kButtonZ, PAD_TRIGGER_Z},
      {kButtonDpadUp, PAD_BUTTON_UP},    {kButtonDpadDown, PAD_BUTTON_DOWN}, {kButtonDpadLeft, PAD_BUTTON_LEFT},
      {kButtonDpadRight, PAD_BUTTON_RIGHT}, {kButtonSelect, 0},              {kButtonHome, 0},
      {kButtonZL, 0},
  };
  for (const Case& c : cases) {
    const PADStatus s = readOne(connectedPad(c.in));
    CHECK(s.button == c.out);
    CHECK(s.err == PAD_ERR_NONE);
  }
}

TEST_CASE("sticks scale symmetrically to -127..127, folding -32768 to -32767") {
  auto stick = [](std::int16_t v) {
    PadState p = connectedPad();
    p.stick_x = v;
    p.stick_y = static_cast<std::int16_t>(-v == 32768 ? 32767 : -v);
    p.c_stick_x = v;
    return readOne(p);
  };
  CHECK(stick(0).stickX == 0);
  CHECK(stick(32767).stickX == 127);
  CHECK(stick(32767).stickY == -127);
  CHECK(stick(-32767).stickX == -127);
  CHECK(stick(-32768).stickX == -127);
  CHECK(stick(16384).stickX == 64);
  CHECK(stick(-16384).stickX == -64);
  CHECK(stick(258).stickX == 1);   // 258 * 127 / 32767 = 1.0 rounds to 1
  CHECK(stick(129).stickX == 0);   // 0.5 - epsilon rounds to 0
  CHECK(stick(12345).substickX == 48);
}

TEST_CASE("triggers: analog passes through, digital press means full travel and the click") {
  PadState p = connectedPad();
  p.trigger_l = 128;
  p.trigger_r = 255;
  PADStatus s = readOne(p);
  CHECK(s.triggerL == 128);
  CHECK(s.triggerR == 255);
  CHECK(s.button == PAD_TRIGGER_R);
  p = connectedPad(kButtonL);
  s = readOne(p);
  CHECK(s.triggerL == 255);
  CHECK(s.button == PAD_TRIGGER_L);
}

TEST_CASE("C buttons deflect a resting C-stick fully") {
  PADStatus s = readOne(connectedPad(kButtonCUp | kButtonCLeft));
  CHECK(s.substickX == -127);
  CHECK(s.substickY == 127);
  s = readOne(connectedPad(kButtonCUp | kButtonCDown));  // opposite presses cancel
  CHECK(s.substickY == 0);
}

TEST_CASE("an unconnected port reports no controller") {
  PADStatus out[4] = {};
  u32 mask = 0;
  Game g{[&] { mask = PADRead(out); }};
  auto sdk = boot(g);
  InputFrame in;
  in.pads[1] = connectedPad(kButtonA);
  in.pads[1].stick_x = 5000;
  in.pads[3] = connectedPad();
  in.pads[2].buttons = kButtonA;  // not connected: ignored
  REQUIRE(step(*sdk, in) == StepStatus::halted);
  CHECK(mask == (PAD_CHAN1_BIT | PAD_CHAN3_BIT));
  CHECK(out[0].err == PAD_ERR_NO_CONTROLLER);
  CHECK(out[2].err == PAD_ERR_NO_CONTROLLER);
  CHECK(out[2].button == 0);
  CHECK(out[1].button == PAD_BUTTON_A);
  CHECK(out[3].err == PAD_ERR_NONE);
}

TEST_CASE("PADClamp applies the octagonal stick and trigger clamps") {
  PADStatus s[4] = {};
  s[0].stickX = 127;
  s[0].stickY = 0;
  s[0].substickX = 127;
  s[0].substickY = 127;
  s[0].triggerL = 255;
  s[0].triggerR = 20;
  s[1].stickX = 127;
  s[1].stickY = 127;
  s[1].substickX = -10;
  s[2].err = PAD_ERR_NO_CONTROLLER;
  s[2].stickX = 100;
  s[3].stickX = -127;
  s[3].stickY = 60;
  PADClamp(s);
  CHECK(s[0].stickX == 72);
  CHECK(s[0].stickY == 0);
  CHECK(s[0].substickX == 31);
  CHECK(s[0].substickY == 31);
  CHECK(s[0].triggerL == 150);
  CHECK(s[0].triggerR == 0);
  CHECK(s[1].stickX == 40);
  CHECK(s[1].stickY == 40);
  CHECK(s[1].substickX == 0);  // inside the dead zone
  CHECK(s[2].stickX == 100);   // not connected: untouched
  CHECK(s[3].stickX == -54);  // (112, 45) after the dead zone, scaled onto the octagon edge
  CHECK(s[3].stickY == 21);
}

TEST_CASE("input is latched at the post-retrace point of each frame") {
  struct Seen {
    u32 retrace;
    u16 pre, post, loop;
  };
  static std::vector<Seen> seen;
  static Seen cur;
  seen.clear();
  Game g{[&] {
    PADInit();
    VISetPreRetraceCallback([](u32 count) {
      PADStatus s[4];
      PADRead(s);
      cur = {count, s[0].button, 0, 0};
    });
    VISetPostRetraceCallback([](u32) {
      PADStatus s[4];
      PADRead(s);
      cur.post = s[0].button;
    });
    for (;;) {
      VIWaitForRetrace();
      PADStatus s[4];
      PADRead(s);
      cur.loop = s[0].button;
      seen.push_back(cur);
    }
  }};
  auto sdk = boot(g);
  const std::uint32_t buttons[] = {kButtonA, kButtonB, kButtonX, kButtonY};
  for (std::uint32_t b : buttons) {
    InputFrame in;
    in.pads[0] = connectedPad(b);
    REQUIRE(step(*sdk, in) == StepStatus::running);
  }
  REQUIRE(seen.size() == 3);
  // Frame n's retrace: the pre-retrace callback still sees frame n-1, the
  // post-retrace callback and the main loop see frame n.
  CHECK(seen[0].retrace == 1);
  CHECK(seen[0].pre == PAD_BUTTON_A);
  CHECK(seen[0].post == PAD_BUTTON_B);
  CHECK(seen[0].loop == PAD_BUTTON_B);
  CHECK(seen[2].retrace == 3);
  CHECK(seen[2].pre == PAD_BUTTON_X);
  CHECK(seen[2].post == PAD_BUTTON_Y);
  CHECK(seen[2].loop == PAD_BUTTON_Y);
}

TEST_CASE("VI: one retrace per frame step, frame buffers swap at the retrace after VIFlush") {
  static int fb_a, fb_b;
  std::vector<u32> counts;
  std::vector<void*> current;
  Game g{[&] {
    VIInit();
    VISetNextFrameBuffer(&fb_a);
    VIFlush();
    for (int i = 0; i < 3; ++i) {
      VIWaitForRetrace();
      counts.push_back(VIGetRetraceCount());
      current.push_back(VIGetCurrentFrameBuffer());
      VISetNextFrameBuffer(i % 2 ? &fb_a : &fb_b);
      VIFlush();
    }
    CHECK(VIGetTvFormat() == VI_NTSC);
    CHECK(VIGetDTVStatus() == 0);
  }};
  auto sdk = boot(g);
  stepN(*sdk, 3);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(counts == std::vector<u32>{1, 2, 3});
  CHECK(current == std::vector<void*>{&fb_a, &fb_b, &fb_a});
}

TEST_CASE("rumble state is a host-visible output") {
  Game g{[] {
    PADControlMotor(2, PAD_MOTOR_RUMBLE);
    PADControlMotor(3, PAD_MOTOR_STOP_HARD);
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(sdk->rumble() == std::array<std::uint32_t, 4>{0, 0, PAD_MOTOR_RUMBLE, PAD_MOTOR_STOP});
}

TEST_CASE("hardware settings are fixed answers") {
  Game g{[] {
    CHECK(OSGetConsoleType() == OS_CONSOLE_RETAIL3);
    CHECK(OSGetPhysicalMemSize() == 24u * 1024 * 1024);
    CHECK(OSGetSoundMode() == OS_SOUND_MODE_STEREO);
    CHECK(OSGetProgressiveMode() == 0);
    CHECK(OSGetLanguage() == OS_LANG_ENGLISH);
    CHECK(OSGetResetCode() == 0);
    OSSetSoundMode(OS_SOUND_MODE_MONO);  // a game setting: kept as state
    CHECK(OSGetSoundMode() == OS_SOUND_MODE_MONO);
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::halted);
}
