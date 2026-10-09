// VI (retrace, callbacks, frame buffers) and PAD (InputFrame -> GameCube pads).
#include <cmath>
#include <cstring>

#include "sdk_internal.h"

using namespace mpt;
using namespace mpt::gc;
using namespace mpt::gc::detail;

namespace mpt::gc::detail {

// The retrace interrupt: count, pre-retrace callback, the hardware swap of the
// flushed frame buffer, PAD latch (the post-retrace sampling point, ADR 0007 §4),
// post-retrace callback, then wake VIWaitForRetrace waiters.
void viRetrace() {
  auto& s = st();
  ++s.retrace_count;
  if (s.pre_retrace) s.pre_retrace(s.retrace_count);
  s.current_fb = s.flushed_fb;
  padLatch();
  if (s.post_retrace) s.post_retrace(s.retrace_count);
  wakeAll(WaitKind::retrace, nullptr);
}

namespace {

s8 scaleStick(std::int16_t v) {
  std::int32_t x = v < -32767 ? -32767 : v;  // -32768 is treated as -32767 (input.h convention)
  const std::int32_t half = x >= 0 ? 16383 : -16383;
  return static_cast<s8>((x * 127 + half) / 32767);
}

}  // namespace

// Canonical pad -> GameCube pad. Fixed table, documented in docs/titles/mp4.md §14.
PADStatus padStatusFrom(const PadState& p) {
  PADStatus out{};
  if (!p.connected) {
    out.err = PAD_ERR_NO_CONTROLLER;
    return out;
  }
  const std::uint32_t b = p.buttons;
  u16 btn = 0;
  if (b & kButtonA) btn |= PAD_BUTTON_A;
  if (b & kButtonB) btn |= PAD_BUTTON_B;
  if (b & kButtonX) btn |= PAD_BUTTON_X;
  if (b & kButtonY) btn |= PAD_BUTTON_Y;
  if (b & kButtonStart) btn |= PAD_BUTTON_START;
  if (b & kButtonZ) btn |= PAD_TRIGGER_Z;
  if (b & kButtonDpadUp) btn |= PAD_BUTTON_UP;
  if (b & kButtonDpadDown) btn |= PAD_BUTTON_DOWN;
  if (b & kButtonDpadLeft) btn |= PAD_BUTTON_LEFT;
  if (b & kButtonDpadRight) btn |= PAD_BUTTON_RIGHT;

  // Analog triggers. The digital click happens at full travel, so a digital
  // press reports full travel and full travel reports the click.
  u8 tl = p.trigger_l, tr = p.trigger_r;
  if (b & kButtonL) tl = 255;
  if (b & kButtonR) tr = 255;
  if (tl == 255) btn |= PAD_TRIGGER_L;
  if (tr == 255) btn |= PAD_TRIGGER_R;
  out.triggerL = tl;
  out.triggerR = tr;

  out.stickX = scaleStick(p.stick_x);
  out.stickY = scaleStick(p.stick_y);
  out.substickX = scaleStick(p.c_stick_x);
  out.substickY = scaleStick(p.c_stick_y);
  // N64-style C buttons give full C-stick deflection on an axis that is at rest.
  if (out.substickX == 0) {
    if ((b & kButtonCRight) && !(b & kButtonCLeft)) out.substickX = 127;
    if ((b & kButtonCLeft) && !(b & kButtonCRight)) out.substickX = -127;
  }
  if (out.substickY == 0) {
    if ((b & kButtonCUp) && !(b & kButtonCDown)) out.substickY = 127;
    if ((b & kButtonCDown) && !(b & kButtonCUp)) out.substickY = -127;
  }
  out.button = btn;
  out.err = PAD_ERR_NONE;
  return out;
}

void padStage(const InputFrame& input) {
  for (std::size_t i = 0; i < kMaxPlayers; ++i) st().staged[i] = input.pads[i];
}

void padLatch() {
  auto& s = st();
  for (std::size_t i = 0; i < PAD_MAX_CONTROLLERS; ++i) s.latched[i] = padStatusFrom(s.staged[i]);
}

}  // namespace mpt::gc::detail

namespace {

void clampStick(s8* px, s8* py, int max, int xy, int min) {
  int x = *px, y = *py;
  const int sx = x < 0 ? -1 : 1, sy = y < 0 ? -1 : 1;
  x *= sx;
  y *= sy;
  x = x <= min ? 0 : x - min;
  y = y <= min ? 0 : y - min;
  if (x == 0 && y == 0) {
    *px = *py = 0;
    return;
  }
  // Octagon: the diagonal edges pass through (xy, xy), the axes end at max.
  const int d = y <= x ? xy * x + (max - xy) * y : xy * y + (max - xy) * x;
  if (xy * max < d) {
    x = xy * max * x / d;
    y = xy * max * y / d;
  }
  *px = static_cast<s8>(sx * x);
  *py = static_cast<s8>(sy * y);
}

void clampTrigger(u8* t, int min, int max) {
  int v = *t;
  if (v <= min) {
    v = 0;
  } else {
    if (v > max) v = max;
    v -= min;
  }
  *t = static_cast<u8>(v);
}

void clampCircle(s8* px, s8* py, int radius, int min) {
  int x = *px, y = *py;
  const int sx = x < 0 ? -1 : 1, sy = y < 0 ? -1 : 1;
  x *= sx;
  y *= sy;
  x = x <= min ? 0 : x - min;
  y = y <= min ? 0 : y - min;
  const int sq = x * x + y * y;
  if (sq > radius * radius) {
    // Exact integer scaling: largest k with (k*x)^2 + (k*y)^2 <= r^2 in 1/1024 steps.
    int lo = 0, hi = 1024;
    while (lo < hi) {
      const int mid = (lo + hi + 1) / 2;
      const long long nx = static_cast<long long>(x) * mid, ny = static_cast<long long>(y) * mid;
      if (nx * nx + ny * ny <= static_cast<long long>(radius) * radius * 1024 * 1024) lo = mid;
      else hi = mid - 1;
    }
    x = x * lo / 1024;
    y = y * lo / 1024;
  }
  *px = static_cast<s8>(sx * x);
  *py = static_cast<s8>(sy * y);
}

}  // namespace

extern "C" {

// ---- VI -----------------------------------------------------------------------------

void VIInit(void) { boundary(); }

void VIConfigure(const GXRenderModeObj* rm) {
  boundary();
  if (rm) st().tv_format = rm->viTVmode >> 2;
}

void VIConfigurePan(u16, u16, u16, u16) { boundary(); }

void VIFlush(void) {
  boundary();
  auto& s = st();
  s.flushed_fb = s.next_fb;
  s.black_flushed = s.black;
}

void VIWaitForRetrace(void) {
  boundary();
  const std::uint32_t start = st().retrace_count;
  while (st().retrace_count == start) block(WaitKind::retrace, nullptr);
}

u32 VIGetRetraceCount(void) {
  boundary();
  return st().retrace_count;
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback callback) {
  boundary();
  auto& s = st();
  VIRetraceCallback old = s.pre_retrace;
  s.pre_retrace = callback;
  return old;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback callback) {
  boundary();
  auto& s = st();
  VIRetraceCallback old = s.post_retrace;
  s.post_retrace = callback;
  return old;
}

void VISetNextFrameBuffer(void* fb) {
  boundary();
  st().next_fb = fb;
}

void* VIGetNextFrameBuffer(void) {
  boundary();
  return st().next_fb;
}

void* VIGetCurrentFrameBuffer(void) {
  boundary();
  return st().current_fb;
}

void VISetBlack(BOOL black) {
  boundary();
  st().black = black ? 1 : 0;
}

u32 VIGetNextField(void) {
  boundary();
  return (st().retrace_count & 1u) ? VI_FIELD_ABOVE : VI_FIELD_BELOW;
}

u32 VIGetCurrentLine(void) {
  boundary();
  return 0;
}

u32 VIGetTvFormat(void) {
  boundary();
  return st().tv_format;
}

u32 VIGetDTVStatus(void) {
  boundary();
  return 0;  // no component cable: progressive scan is never offered
}

// ---- PAD --------------------------------------------------------------------------------

BOOL PADInit(void) {
  boundary();
  st().pad_initialized = 1;
  return TRUE;
}

u32 PADRead(PADStatus* status) {
  boundary();
  auto& s = st();
  u32 mask = 0;
  for (int i = 0; i < PAD_MAX_CONTROLLERS; ++i) {
    status[i] = s.latched[i];
    if (s.latched[i].err == PAD_ERR_NONE) mask |= PAD_CHAN0_BIT >> i;
  }
  return mask;
}

void PADClamp(PADStatus* status) {
  for (int i = 0; i < PAD_MAX_CONTROLLERS; ++i) {
    PADStatus& p = status[i];
    if (p.err != PAD_ERR_NONE) continue;
    clampStick(&p.stickX, &p.stickY, 72, 40, 15);
    clampStick(&p.substickX, &p.substickY, 59, 31, 15);
    clampTrigger(&p.triggerL, 30, 180);
    clampTrigger(&p.triggerR, 30, 180);
  }
}

void PADClampCircle(PADStatus* status) {
  for (int i = 0; i < PAD_MAX_CONTROLLERS; ++i) {
    PADStatus& p = status[i];
    if (p.err != PAD_ERR_NONE) continue;
    clampCircle(&p.stickX, &p.stickY, 56, 15);
    clampCircle(&p.substickX, &p.substickY, 44, 15);
    clampTrigger(&p.triggerL, 30, 180);
    clampTrigger(&p.triggerR, 30, 180);
  }
}

BOOL PADReset(u32) {
  boundary();
  return TRUE;
}

BOOL PADRecalibrate(u32) {
  boundary();
  return TRUE;
}

void PADControlMotor(s32 chan, u32 command) {
  boundary();
  if (chan < 0 || chan >= PAD_MAX_CONTROLLERS) return;
  st().motor[chan] = command == PAD_MOTOR_RUMBLE ? PAD_MOTOR_RUMBLE : PAD_MOTOR_STOP;
}

void PADSetSpec(u32 spec) {
  boundary();
  st().pad_spec = spec;
}

void PADSetAnalogMode(u32 mode) {
  boundary();
  st().pad_analog_mode = mode;
}

BOOL PADSync(void) {
  boundary();
  return TRUE;
}

}  // extern "C"
