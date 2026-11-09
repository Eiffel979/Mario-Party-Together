// E2 device-side proof for Mario Party 4 (M4-08, ruling M4-E in
// docs/phases/phase-1.md §10).
//
// For every row of the coverage table in docs/titles/mp4.md §18, the keyboard
// source set and the gamepad source set of the MP4 title profile produce every
// listed input, including the thresholds the row names. The profile is loaded
// the way the shell loads it (load_title_profile from the installed profiles
// directory), and each row runs with that row's minigame selected, so
// per-minigame overrides apply. Each row is checked on each of the 4 ports.
//
// Two test cases, one per source set, with one subcase per row named by the
// row's ID without the "mp4.minigame." prefix:
//   mpt_input_tests -tc="mp4 E2 device proof: keyboard" -sc="bowsers_bigger_blast"
//
// What "produce" means here. input/ ends at the InputFrame; the game sees a
// GameCube PADStatus after the SDK's fixed conversion (docs/titles/mp4.md §14:
// L/R bit -> full travel, full travel -> click, sticks scaled to s8) and after
// PADClamp, which the game's pad reader applies (stick octagon 72/40/15,
// triggers 30..180 minus 30, so 0..150). Thresholds in §18 are values the
// minigames compare the clamped trigger against, so each threshold is checked
// on the clamped game value through gc_view() below, and on the InputFrame
// value (0..255) that clamps to it: game value g > 0 comes from raw g + 30
// (raw_trigger_for()). gc_view() is a model of that documented contract written for
// this test; the game-side proof (M4-07, dump lane) covers the real thing.
//
// Synthetic device state only. No game material.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <ostream>  // doctest prints std::string_view through operator<<
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "mpt/core/title_id.h"
#include "mpt/input/mapper.h"
#include "mpt/input/profile.h"
#include "mpt/input/title_profile.h"

using namespace mpt;
using namespace mpt::input;

namespace {

// ---- The GameCube pad as the game reads it (model of docs/titles/mp4.md §14) ----

// Game-side ranges after PADClamp.
constexpr int kGcStickMax = 72;     // cardinal full tilt of the main stick
constexpr int kGcTriggerClampMin = 30;   // PADClamp's trigger window: raw <= 30 reads 0,
constexpr int kGcTriggerClampMax = 180;  // raw above 180 reads as 180, then 30 is subtracted
constexpr int kGcTriggerMax = kGcTriggerClampMax - kGcTriggerClampMin;  // 150

struct GcView {
  int stick_x = 0, stick_y = 0;  // after PADClamp, +Y up
  int trig_l = 0, trig_r = 0;    // after PADClamp, 0..150
  bool click_l = false, click_r = false;
};

int scale_stick(std::int16_t v) {
  const std::int32_t x = v < -32767 ? -32767 : v;
  const std::int32_t half = x >= 0 ? 16383 : -16383;
  return (x * 127 + half) / 32767;
}

// PADClamp's octagon for one stick.
void clamp_stick(int& px, int& py, int max, int xy, int min) {
  int x = px, y = py;
  const int sx = x < 0 ? -1 : 1, sy = y < 0 ? -1 : 1;
  x *= sx;
  y *= sy;
  x = x <= min ? 0 : x - min;
  y = y <= min ? 0 : y - min;
  if (x == 0 && y == 0) {
    px = py = 0;
    return;
  }
  const int d = y <= x ? xy * x + (max - xy) * y : xy * y + (max - xy) * x;
  if (xy * max < d) {
    x = xy * max * x / d;
    y = xy * max * y / d;
  }
  px = sx * x;
  py = sy * y;
}

int clamp_trigger(int v) {
  return v <= kGcTriggerClampMin ? 0 : std::min(v, kGcTriggerClampMax) - kGcTriggerClampMin;
}

// The InputFrame trigger value (0..255) that reaches game value `game` (1..150)
// through PADClamp: the raw-units form of a §18 threshold.
int raw_trigger_for(int game) { return game + kGcTriggerClampMin; }

GcView gc_view(const PadState& p) {
  GcView g;
  int tl = p.trigger_l, tr = p.trigger_r;
  if (p.buttons & kButtonL) tl = 255;
  if (p.buttons & kButtonR) tr = 255;
  g.click_l = tl == 255;
  g.click_r = tr == 255;
  g.trig_l = clamp_trigger(tl);
  g.trig_r = clamp_trigger(tr);
  g.stick_x = scale_stick(p.stick_x);
  g.stick_y = scale_stick(p.stick_y);
  clamp_stick(g.stick_x, g.stick_y, kGcStickMax, 40, 15);
  return g;
}

// ---- One source set seated on one port, with the shell's MP4 profile ----

enum class Side { l, r };

class Rig {
 public:
  Rig(DeviceKind kind, std::size_t port, const std::string& minigame) : kind_(kind), port_(port) {
    const std::vector<std::filesystem::path> dirs{MPT_INPUT_PROFILES_DIR};
    std::vector<std::string> warnings;
    const TitleProfileLoad load = load_title_profile(mapper_, TitleId::mp4, dirs,
                                                     [&](std::string_view m) { warnings.emplace_back(m); });
    REQUIRE(load.from_file());
    REQUIRE(warnings.empty());
    mapper_.set_minigame(minigame);
    resolved_ = mapper_.profile(port)->resolve(minigame);
    id_ = kind == DeviceKind::keyboard ? kKeyboardDevice : DeviceId{DeviceKind::gamepad, 7};
    layout_.seated[port] = true;
    layout_.device_port[id_] = port;
  }

  DeviceKind kind() const { return kind_; }

  // The sources of `c` this device kind can drive (the "source set").
  std::vector<Source> sources(Control c) const {
    std::vector<Source> out;
    for (const Source& s : resolved_.bindings[static_cast<std::size_t>(c)]) {
      if (s.is_keyboard() == (kind_ == DeviceKind::keyboard)) out.push_back(s);
    }
    return out;
  }
  std::vector<Source> digital_sources(Control c) const {
    std::vector<Source> out;
    for (const Source& s : sources(c)) {
      if (s.is_digital()) out.push_back(s);
    }
    return out;
  }
  std::vector<Source> analog_sources(Control c) const {
    std::vector<Source> out;
    for (const Source& s : sources(c)) {
      if (!s.is_digital()) out.push_back(s);
    }
    return out;
  }

  DeviceSnapshot blank() const {
    DeviceSnapshot s;
    s.id = id_;
    return s;
  }

  // Holds `source`. Axis sources take `magnitude` (0..32767) in their direction.
  static void hold(DeviceSnapshot& snap, const Source& source, std::int32_t magnitude = kAxisMax) {
    switch (source.kind) {
      case Source::Kind::key:
        snap.keyboard.set(source.code);
        break;
      case Source::Kind::pad_button:
        snap.gamepad.set(static_cast<PadButton>(source.code), true);
        break;
      case Source::Kind::pad_axis_positive:
        snap.gamepad.set(static_cast<PadAxis>(source.code), static_cast<std::int16_t>(magnitude));
        break;
      case Source::Kind::pad_axis_negative:
        snap.gamepad.set(static_cast<PadAxis>(source.code), static_cast<std::int16_t>(-magnitude));
        break;
    }
  }

  PadState map(const DeviceSnapshot& snap) {
    const std::vector<DeviceSnapshot> devices{snap};
    const InputFrame f = mapper_.map(frame_++, devices, layout_);
    for (std::size_t p = 0; p < kMaxPlayers; ++p) {
      if (p != port_) CHECK(f.pads[p] == PadState{});  // nothing leaks to other ports
    }
    return f.pads[port_];
  }

 private:
  DeviceKind kind_;
  std::size_t port_;
  Mapper mapper_;
  ResolvedProfile resolved_;
  DeviceId id_;
  PortLayout layout_;
  std::uint64_t frame_ = 0;
};

const char* control_label(Control c) { return control_name(c).data(); }

// ---- Checks, one per kind of listed input ----

// A digital button: every source of the control presses exactly that bit;
// releasing clears it.
void check_button(Rig& rig, Control c, std::uint32_t bit) {
  CAPTURE(control_label(c));
  const auto sources = rig.sources(c);
  REQUIRE_FALSE(sources.empty());
  CHECK((rig.map(rig.blank()).buttons & bit) == 0);
  for (const Source& s : sources) {
    CAPTURE(source_to_string(s));
    DeviceSnapshot snap = rig.blank();
    Rig::hold(snap, s);
    const PadState p = rig.map(snap);
    CHECK(p.buttons == bit);
    CHECK((rig.map(rig.blank()).buttons & bit) == 0);
  }
}

Control click_control(Side side) { return side == Side::l ? Control::l : Control::r; }
Control trigger_control(Side side) { return side == Side::l ? Control::trigger_l : Control::trigger_r; }
std::uint32_t click_bit(Side side) { return side == Side::l ? kButtonL : kButtonR; }
int pad_trigger(const PadState& p, Side side) { return side == Side::l ? p.trigger_l : p.trigger_r; }
int gc_trigger(const GcView& g, Side side) { return side == Side::l ? g.trig_l : g.trig_r; }
bool gc_click(const GcView& g, Side side) { return side == Side::l ? g.click_l : g.click_r; }

// Every way this source set has to press the L/R click: the click control's
// own sources, and the trigger's sources held fully (a gamepad's analog
// trigger clicks through trigger_button_threshold).
std::vector<Source> click_sources(const Rig& rig, Side side) {
  auto out = rig.sources(click_control(side));
  for (const Source& s : rig.sources(trigger_control(side))) {
    if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
  }
  return out;
}

// The digital L/R click (PAD_TRIGGER_L/R in the game).
void check_click(Rig& rig, Side side) {
  CAPTURE(side == Side::l ? "L click" : "R click");
  const auto sources = click_sources(rig, side);
  REQUIRE_FALSE(sources.empty());
  const GcView idle = gc_view(rig.map(rig.blank()));
  CHECK_FALSE(gc_click(idle, side));
  CHECK(gc_trigger(idle, side) == 0);
  for (const Source& s : sources) {
    CAPTURE(source_to_string(s));
    DeviceSnapshot snap = rig.blank();
    Rig::hold(snap, s);
    const PadState p = rig.map(snap);
    CHECK((p.buttons & click_bit(side)) != 0);
    const GcView g = gc_view(p);
    CHECK(gc_click(g, side));
    CHECK(gc_trigger(g, side) == kGcTriggerMax);
  }
}

struct TriggerSweep {
  std::set<int> pad;          // InputFrame trigger values (0..255) reached without the click bit
  std::set<int> game;         // clamped game values reached without the click
  std::set<int> game_any;     // clamped game values reached at all
  bool monotone = true;
};

// Pulls each analog source of the trigger from rest to full travel.
TriggerSweep sweep_trigger(Rig& rig, Side side) {
  TriggerSweep out;
  const auto sources = rig.analog_sources(trigger_control(side));
  for (const Source& s : sources) {
    int last_game = -1;
    for (std::int32_t raw = 0;; raw = std::min<std::int32_t>(raw + 8, kAxisMax)) {
      DeviceSnapshot snap = rig.blank();
      Rig::hold(snap, s, raw);
      const PadState p = rig.map(snap);
      const GcView g = gc_view(p);
      const int game = gc_trigger(g, side);
      if (game < last_game) out.monotone = false;
      last_game = game;
      out.game_any.insert(game);
      if (!gc_click(g, side)) {
        CHECK((p.buttons & click_bit(side)) == 0);
        out.pad.insert(pad_trigger(p, side));
        out.game.insert(game);
      }
      if (raw == kAxisMax) break;
    }
  }
  return out;
}

// "L/R past t": released stays at or below t, a press goes past it. An analog
// source also reaches exactly t and t + 1 (fine control on both sides of the
// threshold) in game units, and the InputFrame values that clamp to them.
void check_trigger_past(Rig& rig, Side side, int t) {
  CAPTURE(side == Side::l ? "L" : "R");
  CAPTURE(t);
  REQUIRE(t >= 1);
  REQUIRE(t + 1 <= kGcTriggerMax);
  const int raw_t = raw_trigger_for(t);
  REQUIRE(clamp_trigger(raw_t) == t);
  REQUIRE(clamp_trigger(raw_t + 1) == t + 1);
  const PadState idle_pad = rig.map(rig.blank());
  CHECK(gc_trigger(gc_view(idle_pad), side) <= t);
  CHECK(pad_trigger(idle_pad, side) <= raw_t);
  const auto sources = click_sources(rig, side);
  REQUIRE_FALSE(sources.empty());
  for (const Source& s : sources) {
    CAPTURE(source_to_string(s));
    DeviceSnapshot snap = rig.blank();
    Rig::hold(snap, s);
    const PadState p = rig.map(snap);
    CHECK(pad_trigger(p, side) > raw_t);
    CHECK(gc_trigger(gc_view(p), side) > t);
  }
  if (rig.kind() == DeviceKind::gamepad) {
    REQUIRE_FALSE(rig.analog_sources(trigger_control(side)).empty());
    const TriggerSweep sw = sweep_trigger(rig, side);
    CHECK(sw.monotone);
    CHECK(sw.game.count(t) == 1);
    CHECK(sw.game.count(t + 1) == 1);
    CHECK(sw.pad.count(raw_t) == 1);
    CHECK(sw.pad.count(raw_t + 1) == 1);
  }
}

// An analog read that saturates at `s` (game units): a full press reaches it,
// and an analog source covers every value from rest up to it.
void check_trigger_saturates(Rig& rig, Side side, int s) {
  CAPTURE(side == Side::l ? "L" : "R");
  CAPTURE(s);
  REQUIRE(s <= kGcTriggerMax);
  CHECK(gc_trigger(gc_view(rig.map(rig.blank())), side) == 0);
  const auto sources = click_sources(rig, side);
  REQUIRE_FALSE(sources.empty());
  for (const Source& src : sources) {
    CAPTURE(source_to_string(src));
    DeviceSnapshot snap = rig.blank();
    Rig::hold(snap, src);
    const PadState p = rig.map(snap);
    CHECK(gc_trigger(gc_view(p), side) >= s);
    CHECK(pad_trigger(p, side) >= raw_trigger_for(s));
  }
  if (rig.kind() == DeviceKind::gamepad) {
    const TriggerSweep sw = sweep_trigger(rig, side);
    CHECK(sw.monotone);
    for (int v = 0; v <= s; ++v) {
      CAPTURE(v);
      CHECK(sw.game_any.count(v) == 1);
    }
  }
}

// Bowser's Bigger Blast: R held part-way without the click, then the click.
// M4-E names R = 128 against R = 255 for this row.
void check_r_partial_then_click(Rig& rig) {
  // The module holds the plunger while the clamped pressure is above 10 and
  // commits on the digital R click.
  constexpr int kHoldAbove = 10;
  const GcView idle = gc_view(rig.map(rig.blank()));
  CHECK(idle.trig_r == 0);
  CHECK_FALSE(idle.click_r);

  if (rig.kind() == DeviceKind::keyboard) {
    const auto half = rig.digital_sources(Control::stick_half);
    const auto trig = rig.digital_sources(Control::trigger_r);
    REQUIRE_FALSE(half.empty());
    REQUIRE_FALSE(trig.empty());
    for (const Source& h : half) {
      for (const Source& t : trig) {
        CAPTURE(source_to_string(h));
        CAPTURE(source_to_string(t));
        DeviceSnapshot snap = rig.blank();
        Rig::hold(snap, h);
        Rig::hold(snap, t);
        const PadState p = rig.map(snap);
        CHECK(p.trigger_r == 128);
        CHECK((p.buttons & kButtonR) == 0);
        const GcView g = gc_view(p);
        CHECK_FALSE(g.click_r);
        CHECK(g.trig_r > kHoldAbove);
        CHECK(g.trig_r < kGcTriggerMax);
        // Held for a while: the same partial value every frame.
        for (int i = 0; i < 30; ++i) CHECK(rig.map(snap) == p);
      }
    }
  } else {
    const TriggerSweep sw = sweep_trigger(rig, Side::r);
    CHECK(sw.monotone);
    CHECK(sw.pad.count(128) == 1);  // R = 128 with no click
    CHECK(sw.game.count(kHoldAbove) == 1);
    CHECK(sw.game.count(kHoldAbove + 1) == 1);
    // The whole pressure range is there before the click.
    for (int v = 0; v <= kGcTriggerMax; ++v) {
      CAPTURE(v);
      CHECK(sw.game.count(v) == 1);
    }
  }
  // R = 255 and the click, from every source that can click.
  const auto clicks = click_sources(rig, Side::r);
  REQUIRE_FALSE(clicks.empty());
  for (const Source& s : clicks) {
    CAPTURE(source_to_string(s));
    DeviceSnapshot snap = rig.blank();
    Rig::hold(snap, s);
    const PadState p = rig.map(snap);
    CHECK(p.trigger_r == 255);
    CHECK((p.buttons & kButtonR) != 0);
    CHECK(gc_view(p).click_r);
  }
}

struct Direction {
  const char* name;
  int dx, dy;  // +Y up
};
constexpr std::array<Direction, 8> kDirections{{{"right", 1, 0},
                                                {"up-right", 1, 1},
                                                {"up", 0, 1},
                                                {"up-left", -1, 1},
                                                {"left", -1, 0},
                                                {"down-left", -1, -1},
                                                {"down", 0, -1},
                                                {"down-right", 1, -1}}};

int sign(int v) { return (v > 0) - (v < 0); }

// Holds the main stick toward `d`: each key of the direction's controls, or
// each analog axis at `magnitude` (a diagonal gets 1/sqrt 2 on each axis).
DeviceSnapshot stick_toward(const Rig& rig, const Direction& d, bool half, std::int32_t magnitude = kAxisMax) {
  DeviceSnapshot snap = rig.blank();
  const bool diagonal = d.dx != 0 && d.dy != 0;
  const std::int32_t per_axis = diagonal ? static_cast<std::int32_t>((std::int64_t{magnitude} * 46341) >> 16) : magnitude;
  auto press = [&](Control c) {
    const auto list = rig.sources(c);
    REQUIRE_FALSE(list.empty());
    Rig::hold(snap, list.front(), per_axis);
  };
  if (d.dx > 0) press(Control::stick_right);
  if (d.dx < 0) press(Control::stick_left);
  if (d.dy > 0) press(Control::stick_up);
  if (d.dy < 0) press(Control::stick_down);
  if (half) {
    const auto mod = rig.digital_sources(Control::stick_half);
    REQUIRE_FALSE(mod.empty());
    Rig::hold(snap, mod.front());
  }
  return snap;
}

// The main stick: all 8 directions reach the game with the right signs, full
// tilt reaches the game's full range, and a smaller tilt is available too
// (half tilt on the keyboard, any tilt on a gamepad).
void check_stick(Rig& rig) {
  const GcView idle = gc_view(rig.map(rig.blank()));
  CHECK(idle.stick_x == 0);
  CHECK(idle.stick_y == 0);
  // Every direction control has a source in this set.
  for (Control c : {Control::stick_up, Control::stick_down, Control::stick_left, Control::stick_right}) {
    CAPTURE(control_label(c));
    CHECK_FALSE(rig.sources(c).empty());
  }
  for (const Direction& d : kDirections) {
    CAPTURE(d.name);
    const GcView full = gc_view(rig.map(stick_toward(rig, d, false)));
    CHECK(sign(full.stick_x) == d.dx);
    CHECK(sign(full.stick_y) == d.dy);
    if (d.dx == 0 || d.dy == 0) CHECK(std::abs(full.stick_x) + std::abs(full.stick_y) == kGcStickMax);

    const GcView part = rig.kind() == DeviceKind::keyboard ? gc_view(rig.map(stick_toward(rig, d, true)))
                                                            : gc_view(rig.map(stick_toward(rig, d, false, kAxisMax / 2)));
    CHECK(sign(part.stick_x) == d.dx);
    CHECK(sign(part.stick_y) == d.dy);
    CHECK(std::abs(part.stick_x) + std::abs(part.stick_y) < std::abs(full.stick_x) + std::abs(full.stick_y));
  }
  if (rig.kind() == DeviceKind::gamepad) {
    // Fine tilt: every game value from rest to full on the X axis.
    std::set<int> seen;
    for (std::int32_t raw = 0;; raw = std::min<std::int32_t>(raw + 16, kAxisMax)) {
      seen.insert(gc_view(rig.map(stick_toward(rig, kDirections[0], false, raw))).stick_x);
      if (raw == kAxisMax) break;
    }
    for (int v = 0; v <= kGcStickMax; ++v) {
      CAPTURE(v);
      CHECK(seen.count(v) == 1);
    }
  }
}

// The game's stick-as-D-pad reads the clamped stick: up/down past 20, left/right
// past 30, and resets its repeat when |x| + |y| drops below 20.
void check_stick_as_dpad(Rig& rig) {
  constexpr int kUpDownPast = 20;
  constexpr int kLeftRightPast = 30;
  constexpr int kRestBelow = 20;
  const GcView idle = gc_view(rig.map(rig.blank()));
  CHECK(std::abs(idle.stick_x) + std::abs(idle.stick_y) < kRestBelow);
  for (const Direction& d : kDirections) {
    if (d.dx != 0 && d.dy != 0) continue;
    CAPTURE(d.name);
    std::vector<bool> tilts{false};
    if (rig.kind() == DeviceKind::keyboard) tilts.push_back(true);  // half tilt still counts
    for (bool half : tilts) {
      CAPTURE(half);
      const DeviceSnapshot snap = stick_toward(rig, d, half);
      const PadState p = rig.map(snap);
      const GcView g = gc_view(p);
      if (d.dx != 0) {
        CHECK(g.stick_x * d.dx > kLeftRightPast);
        CHECK(std::abs(g.stick_y) <= kUpDownPast);
      } else {
        CHECK(g.stick_y * d.dy > kUpDownPast);
        CHECK(std::abs(g.stick_x) <= kLeftRightPast);
      }
      // Held: the same pad every frame, which the game turns into repeats.
      for (int i = 0; i < 30; ++i) CHECK(rig.map(snap) == p);
      const GcView back = gc_view(rig.map(rig.blank()));
      CHECK(std::abs(back.stick_x) + std::abs(back.stick_y) < kRestBelow);
    }
  }
}

// Rapid presses: tapping a source (press one frame, release the next) gives a
// rising edge of A on every press frame, from every A source, and alternating
// two keys with a release between them does the same.
void check_press_rate(Rig& rig) {
  const auto sources = rig.sources(Control::a);
  REQUIRE_FALSE(sources.empty());
  constexpr int kTaps = 20;
  auto count_edges = [&](const std::vector<DeviceSnapshot>& seq) {
    int edges = 0;
    bool was = false;
    for (const DeviceSnapshot& s : seq) {
      const bool now = (rig.map(s).buttons & kButtonA) != 0;
      if (now && !was) ++edges;
      was = now;
    }
    return edges;
  };
  for (const Source& s : sources) {
    CAPTURE(source_to_string(s));
    std::vector<DeviceSnapshot> seq;
    DeviceSnapshot down = rig.blank();
    Rig::hold(down, s);
    for (int i = 0; i < kTaps; ++i) {
      seq.push_back(down);
      seq.push_back(rig.blank());
    }
    CHECK(count_edges(seq) == kTaps);
  }
  if (sources.size() >= 2) {
    std::vector<DeviceSnapshot> seq;
    for (int i = 0; i < kTaps; ++i) {
      DeviceSnapshot down = rig.blank();
      Rig::hold(down, sources[static_cast<std::size_t>(i) % sources.size()]);
      seq.push_back(down);
      seq.push_back(rig.blank());
    }
    CHECK(count_edges(seq) == kTaps);
  }
}

// ---- The §18 rows ----

enum class Need {
  stick,
  stick_as_dpad,
  a,
  b,
  x,
  y,
  l_click,
  r_click,
  digital_buttons,  // every digital GameCube button: A B X Y Z Start, D-pad, L/R clicks
  l_past_20,
  r_past_20,
  l_past_80,
  r_past_10,
  r_saturates_140,
  l_saturates_150,
  r_saturates_150,
  r_partial_then_click,
  a_press_rate,
};

const char* need_label(Need n) {
  switch (n) {
    case Need::stick: return "stick";
    case Need::stick_as_dpad: return "stick as D-pad";
    case Need::a: return "A";
    case Need::b: return "B";
    case Need::x: return "X";
    case Need::y: return "Y";
    case Need::l_click: return "L click";
    case Need::r_click: return "R click";
    case Need::digital_buttons: return "digital buttons";
    case Need::l_past_20: return "L past 20";
    case Need::r_past_20: return "R past 20";
    case Need::l_past_80: return "L past 80";
    case Need::r_past_10: return "R past 10";
    case Need::r_saturates_140: return "R saturates at 140";
    case Need::l_saturates_150: return "L saturates at 150";
    case Need::r_saturates_150: return "R saturates at 150";
    case Need::r_partial_then_click: return "R part-way, then R click";
    case Need::a_press_rate: return "A press rate";
  }
  return "?";
}

void check_need(Rig& rig, Need n) {
  CAPTURE(need_label(n));
  switch (n) {
    case Need::stick: check_stick(rig); break;
    case Need::stick_as_dpad: check_stick_as_dpad(rig); break;
    case Need::a: check_button(rig, Control::a, kButtonA); break;
    case Need::b: check_button(rig, Control::b, kButtonB); break;
    case Need::x: check_button(rig, Control::x, kButtonX); break;
    case Need::y: check_button(rig, Control::y, kButtonY); break;
    case Need::l_click: check_click(rig, Side::l); break;
    case Need::r_click: check_click(rig, Side::r); break;
    case Need::digital_buttons:
      check_button(rig, Control::a, kButtonA);
      check_button(rig, Control::b, kButtonB);
      check_button(rig, Control::x, kButtonX);
      check_button(rig, Control::y, kButtonY);
      check_button(rig, Control::z, kButtonZ);
      check_button(rig, Control::start, kButtonStart);
      check_button(rig, Control::dpad_up, kButtonDpadUp);
      check_button(rig, Control::dpad_down, kButtonDpadDown);
      check_button(rig, Control::dpad_left, kButtonDpadLeft);
      check_button(rig, Control::dpad_right, kButtonDpadRight);
      check_click(rig, Side::l);
      check_click(rig, Side::r);
      break;
    case Need::l_past_20: check_trigger_past(rig, Side::l, 20); break;
    case Need::r_past_20: check_trigger_past(rig, Side::r, 20); break;
    case Need::l_past_80: check_trigger_past(rig, Side::l, 80); break;
    case Need::r_past_10: check_trigger_past(rig, Side::r, 10); break;
    case Need::r_saturates_140: check_trigger_saturates(rig, Side::r, 140); break;
    case Need::l_saturates_150: check_trigger_saturates(rig, Side::l, 150); break;
    case Need::r_saturates_150: check_trigger_saturates(rig, Side::r, 150); break;
    case Need::r_partial_then_click: check_r_partial_then_click(rig); break;
    case Need::a_press_rate: check_press_rate(rig); break;
  }
}

#define DAGGER "\xE2\x80\xA0"

struct Row {
  const char* id;
  // The row's "Inputs read" cell, verbatim. If §18 changes, this table must
  // be revisited, and the cell check below fails until it is.
  const char* inputs_read;
  std::vector<Need> needs;
};

using N = Need;

const std::vector<Row>& rows() {
  static const std::vector<Row> table{
      {"manta_rings", "stick, A", {N::stick, N::a}},
      {"slime_time", "A", {N::a}},
      {"booksquirm", "stick", {N::stick}},
      {"trace_race", "stick", {N::stick}},
      {"mario_medley", "A, B, L click, R click (prompted)" DAGGER, {N::a, N::b, N::l_click, N::r_click}},
      {"avalanche", "stick, A", {N::stick, N::a}},
      {"domination", "A", {N::a}},
      {"paratrooper_plunge", "stick", {N::stick}},
      {"toads_quick_draw", "stick, A", {N::stick, N::a}},
      {"three_throw", "A, B", {N::a, N::b}},
      {"photo_finish", "stick, A, L/R past 20/255" DAGGER, {N::stick, N::a, N::l_past_20, N::r_past_20}},
      {"mr_blizzards_brigade", "stick", {N::stick}},
      {"bob_omb_breakers", "stick, A, B, X, Y", {N::stick, N::a, N::b, N::x, N::y}},
      {"long_claw_of_the_law", "stick, A, L/R past 20/255" DAGGER, {N::stick, N::a, N::l_past_20, N::r_past_20}},
      {"stamp_out", "stick, A" DAGGER, {N::stick, N::a}},
      {"candlelight_flight", "stick, A", {N::stick, N::a}},
      {"makin_waves", "stick, A, L past 80/255" DAGGER, {N::stick, N::a, N::l_past_80}},
      {"hide_and_go_boom", "A, B, X, Y", {N::a, N::b, N::x, N::y}},
      {"tree_stomp", "stick, A, B" DAGGER, {N::stick, N::a, N::b}},
      {"fish_n_drips", "any of A, B, X, Y, L click, R click", {N::a, N::b, N::x, N::y, N::l_click, N::r_click}},
      {"hop_or_pop", "stick, A", {N::stick, N::a}},
      {"money_belts", "stick", {N::stick}},
      {"goooooooal", "stick, A", {N::stick, N::a}},
      {"blame_it_on_the_crane", "stick, A", {N::stick, N::a}},
      {"the_great_deflate", "A (press rate)" DAGGER, {N::a, N::a_press_rate}},
      {"revers_a_bomb", "stick, A", {N::stick, N::a}},
      {"right_oar_left", "A, B", {N::a, N::b}},
      {"cliffhangers", "stick, A", {N::stick, N::a}},
      {"team_treasure_trek", "stick, X, Y", {N::stick, N::x, N::y}},
      {"pair_a_sailing", "stick, A, B", {N::stick, N::a, N::b}},
      {"order_up", "stick, A", {N::stick, N::a}},
      {"dungeon_duos",
       "stick, A, B, X, Y, L/R analog (strength / 150)" DAGGER,
       {N::stick, N::a, N::b, N::x, N::y, N::l_saturates_150, N::r_saturates_150}},
      {"cheep_cheep_sweep", "stick, A, B", {N::stick, N::a, N::b}},
      {"darts_of_doom", "A", {N::a}},
      {"fruits_of_doom", "stick, A", {N::stick, N::a}},
      {"balloon_of_doom", "A, L click", {N::a, N::l_click}},
      {"chain_chomp_fever", "stick", {N::stick}},
      {"paths_of_peril", "stick", {N::stick}},
      {"bowsers_bigger_blast", "R pressure (held part-way) and R click" DAGGER, {N::r_partial_then_click}},
      {"butterfly_blitz", "stick, A, B", {N::stick, N::a, N::b}},
      {"mario_speedwagons", "A, R throttle (saturates at 140/255)" DAGGER, {N::a, N::r_saturates_140}},
      {"reversal_of_fortune", "stick, A, R past 10/255" DAGGER, {N::stick, N::a, N::r_past_10}},
      {"bowser_bop", "stick, A" DAGGER, {N::stick, N::a}},
      {"mystic_match_em", "A, stick as D-pad (repeat)", {N::a, N::stick_as_dpad}},
      {"archaeologuess", "stick, A", {N::stick, N::a}},
      {"goombas_chip_flip", "stick, A, L click", {N::stick, N::a, N::l_click}},
      {"kareening_koopa", "stick", {N::stick}},
      {"rumble_fishing", "stick, A", {N::stick, N::a}},
      {"take_a_breather",
       "stick, digital buttons (mask excludes the analog-L/R bits)",
       {N::stick, N::digital_buttons}},
  };
  return table;
}

std::string full_id(const Row& r) { return std::string("mp4.minigame.") + r.id; }

void run_rows(DeviceKind kind) {
  for (const Row& row : rows()) {
    SUBCASE(row.id) {
      const std::string id = full_id(row);
      REQUIRE_FALSE(row.needs.empty());
      for (std::size_t port = 0; port < kMaxPlayers; ++port) {
        CAPTURE(port);
        Rig rig(kind, port, id);
        for (Need n : row.needs) check_need(rig, n);
      }
    }
  }
}

// ---- Reading §18 ----

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE_MESSAGE(in.good(), "cannot open " << path);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::vector<std::string> cells(const std::string& row) {
  std::vector<std::string> out;
  std::size_t start = 1;
  for (std::size_t bar = row.find('|', start); bar != std::string::npos; bar = row.find('|', start)) {
    std::string cell = row.substr(start, bar - start);
    const auto b = cell.find_first_not_of(' ');
    const auto e = cell.find_last_not_of(' ');
    out.push_back(b == std::string::npos ? std::string() : cell.substr(b, e - b + 1));
    start = bar + 1;
  }
  return out;
}

// §18 rows keyed by ID: the "Inputs read" cell (column 4).
std::map<std::string, std::string> coverage_inputs() {
  std::map<std::string, std::string> out;
  std::istringstream in(read_file(MPT_DOCS_DIR "/titles/mp4.md"));
  bool in_section = false;
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("#", 0) == 0) {
      if (in_section) break;
      in_section = line.rfind("## 18. Input coverage", 0) == 0;
      continue;
    }
    if (!in_section || line.rfind("| `mp4.", 0) != 0) continue;
    const auto c = cells(line);
    REQUIRE(c.size() >= 4);
    const std::string id = c[0].substr(1, c[0].size() - 2);
    out[id] = c[3];
  }
  return out;
}

}  // namespace

TEST_CASE("mp4 E2 device proof covers every row of docs/titles/mp4.md section 18 as written") {
  const auto doc = coverage_inputs();
  REQUIRE(doc.size() == 49);
  std::set<std::string> proved;
  for (const Row& row : rows()) {
    const std::string id = full_id(row);
    CAPTURE(id);
    CHECK(proved.insert(id).second);
    const auto it = doc.find(id);
    REQUIRE(it != doc.end());
    CHECK(it->second == row.inputs_read);
  }
  std::set<std::string> listed;
  for (const auto& [id, inputs] : doc) listed.insert(id);
  CHECK(proved == listed);
}

TEST_CASE("mp4 E2 device proof: keyboard") { run_rows(DeviceKind::keyboard); }

TEST_CASE("mp4 E2 device proof: gamepad") { run_rows(DeviceKind::gamepad); }
