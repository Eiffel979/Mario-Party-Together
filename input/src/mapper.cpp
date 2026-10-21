#include "mpt/input/mapper.h"

#include <algorithm>
#include <cstdlib>
#include <tuple>

#include "mpt/input/reserved.h"

namespace mpt::input {

// Control i of the button block drives canonical Button bit i.
static_assert((1u << static_cast<unsigned>(Control::a)) == kButtonA);
static_assert((1u << static_cast<unsigned>(Control::l)) == kButtonL);
static_assert((1u << static_cast<unsigned>(Control::start)) == kButtonStart);
static_assert((1u << static_cast<unsigned>(Control::c_right)) == kButtonCRight);
static_assert((1u << static_cast<unsigned>(Control::home)) == kButtonHome);

namespace {

constexpr std::size_t idx(Control c) { return static_cast<std::size_t>(c); }

std::int32_t clamp_axis(std::int64_t v) { return static_cast<std::int32_t>(std::clamp<std::int64_t>(v, -kAxisMax, kAxisMax)); }

// Gamepad axes after deadzones, as 0..kAxisMax half-axis magnitudes.
struct ProcessedAxes {
  std::array<std::int32_t, kPadAxisCount> value{};

  std::int32_t magnitude(PadAxis axis, bool positive) const {
    const std::int32_t v = value[static_cast<std::size_t>(axis)];
    return std::min(kAxisMax, std::max(0, positive ? v : -v));
  }
};

ProcessedAxes process_axes(const GamepadState& pad, const Options& options) {
  ProcessedAxes out;
  auto set = [&](PadAxis a, std::int32_t v) { out.value[static_cast<std::size_t>(a)] = v; };
  const auto [lx, ly] = apply_radial_deadzone(pad.axis(PadAxis::left_x), pad.axis(PadAxis::left_y),
                                              options.stick_deadzone_percent);
  const auto [rx, ry] = apply_radial_deadzone(pad.axis(PadAxis::right_x), pad.axis(PadAxis::right_y),
                                              options.stick_deadzone_percent);
  set(PadAxis::left_x, lx);
  set(PadAxis::left_y, ly);
  set(PadAxis::right_x, rx);
  set(PadAxis::right_y, ry);
  set(PadAxis::left_trigger, apply_trigger_deadzone(pad.axis(PadAxis::left_trigger), options.trigger_deadzone_percent));
  set(PadAxis::right_trigger,
      apply_trigger_deadzone(pad.axis(PadAxis::right_trigger), options.trigger_deadzone_percent));
  return out;
}

bool active(const ControlValue& v) { return v.digital || v.analog >= kAnalogPressThreshold; }

// One stick from its four direction controls. Digital sources give full (or
// half) deflection; analog sources pass through; per axis the larger wins.
std::pair<std::int16_t, std::int16_t> make_stick(const ResolvedProfile& profile, const PortAccumulator& acc,
                                                 Control up, Control down, Control left, Control right) {
  const Options& o = profile.options;
  const std::int64_t full = active(acc[idx(Control::stick_half)])
                                ? std::int64_t{kAxisMax} * o.stick_half_percent / 100
                                : std::int64_t{kAxisMax};
  const int dx = (acc[idx(right)].digital ? 1 : 0) - (acc[idx(left)].digital ? 1 : 0);
  const int dy = (acc[idx(up)].digital ? 1 : 0) - (acc[idx(down)].digital ? 1 : 0);
  // 46341 / 65536 = 0.70711 (1 / sqrt 2), rounded: diagonals land on the unit circle.
  const std::int64_t per_axis =
      (o.normalize_digital_diagonals && dx != 0 && dy != 0) ? (full * 46341 + 32768) / 65536 : full;
  const std::int64_t xd = dx * per_axis;
  const std::int64_t yd = dy * per_axis;
  const std::int64_t xa = std::int64_t{acc[idx(right)].analog} - acc[idx(left)].analog;
  const std::int64_t ya = std::int64_t{acc[idx(up)].analog} - acc[idx(down)].analog;
  const std::int64_t x = std::llabs(xd) >= std::llabs(xa) ? xd : xa;
  const std::int64_t y = std::llabs(yd) >= std::llabs(ya) ? yd : ya;
  return {static_cast<std::int16_t>(clamp_axis(x)), static_cast<std::int16_t>(clamp_axis(y))};
}

// `digital_full` is what a held key or button gives: 255, or the half-modifier
// share of it when the profile lets stick_half scale triggers.
std::uint8_t make_trigger(const ControlValue& v, std::int64_t digital_full) {
  const std::int64_t analog = (std::int64_t{v.analog} * 255 + kAxisMax / 2) / kAxisMax;
  return static_cast<std::uint8_t>(v.digital ? std::max(digital_full, analog) : analog);
}

}  // namespace

std::uint64_t isqrt(std::uint64_t v) {
  if (v < 2) return v;
  // Newton's method from an over-estimate; converges monotonically downward.
  std::uint64_t x = v;
  std::uint64_t y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + v / x) / 2;
  }
  return x;
}

std::pair<std::int32_t, std::int32_t> apply_radial_deadzone(std::int32_t x, std::int32_t y, int deadzone_percent) {
  x = clamp_axis(x);  // fold -32768 onto -32767 so both directions have the same range
  y = clamp_axis(y);
  const auto r = static_cast<std::int64_t>(isqrt(static_cast<std::uint64_t>(std::int64_t{x} * x + std::int64_t{y} * y)));
  const std::int64_t dz = std::int64_t{kAxisMax} * std::clamp(deadzone_percent, 0, 99) / 100;
  if (r <= dz) return {0, 0};
  if (dz == 0) return {x, y};
  // New radius (r - dz) * max / (max - dz), capped at max; scale both axes by it.
  const std::int64_t r2 = std::min<std::int64_t>(kAxisMax, (r - dz) * kAxisMax / (kAxisMax - dz));
  return {clamp_axis(std::int64_t{x} * r2 / r), clamp_axis(std::int64_t{y} * r2 / r)};
}

std::int32_t apply_trigger_deadzone(std::int32_t value, int deadzone_percent) {
  const std::int64_t v = std::clamp<std::int64_t>(value, 0, kAxisMax);
  const std::int64_t dz = std::int64_t{kAxisMax} * std::clamp(deadzone_percent, 0, 99) / 100;
  if (v <= dz) return 0;
  return static_cast<std::int32_t>((v - dz) * kAxisMax / (kAxisMax - dz));
}

void accumulate(const ResolvedProfile& profile, const DeviceSnapshot& device, PortAccumulator& acc) {
  const bool keyboard = device.id.kind == DeviceKind::keyboard;
  const ProcessedAxes axes = keyboard ? ProcessedAxes{} : process_axes(device.gamepad, profile.options);

  for (std::size_t c = 0; c < kControlCount; ++c) {
    ControlValue& value = acc[c];
    for (const Source& s : profile.bindings[c]) {
      if (s.is_keyboard() != keyboard) continue;
      if (is_reserved(s)) continue;  // host-owned even if a Profile built in code binds it
      switch (s.kind) {
        case Source::Kind::key:
          if (s.code < kKeyCount && device.keyboard.test(s.code)) value.digital = true;
          break;
        case Source::Kind::pad_button:
          if (device.gamepad.pressed(static_cast<PadButton>(s.code))) value.digital = true;
          break;
        case Source::Kind::pad_axis_positive:
        case Source::Kind::pad_axis_negative:
          if (s.code < kPadAxisCount) {
            value.analog = std::max(
                value.analog, axes.magnitude(static_cast<PadAxis>(s.code), s.kind == Source::Kind::pad_axis_positive));
          }
          break;
      }
    }
  }
}

PadState finalize(const ResolvedProfile& profile, const PortAccumulator& acc, bool seated) {
  PadState pad;
  if (!seated) return pad;
  pad.connected = 1;

  for (std::size_t c = 0; c < kButtonControlCount; ++c) {
    if (active(acc[c])) pad.buttons |= 1u << c;
  }
  std::tie(pad.stick_x, pad.stick_y) =
      make_stick(profile, acc, Control::stick_up, Control::stick_down, Control::stick_left, Control::stick_right);
  std::tie(pad.c_stick_x, pad.c_stick_y) = make_stick(profile, acc, Control::c_stick_up, Control::c_stick_down,
                                                      Control::c_stick_left, Control::c_stick_right);
  const Options& o = profile.options;
  const std::int64_t digital_trigger = (o.stick_half_affects_triggers && active(acc[idx(Control::stick_half)]))
                                           ? (std::int64_t{255} * o.stick_half_percent + 50) / 100
                                           : 255;
  pad.trigger_l = make_trigger(acc[idx(Control::trigger_l)], digital_trigger);
  pad.trigger_r = make_trigger(acc[idx(Control::trigger_r)], digital_trigger);

  const int threshold = o.trigger_button_threshold;
  if (threshold > 0) {
    if (pad.trigger_l >= threshold) pad.buttons |= kButtonL;
    if (pad.trigger_r >= threshold) pad.buttons |= kButtonR;
  }
  return pad;
}

InputFrame map_frame(std::uint64_t frame, const std::array<ResolvedProfile, kMaxPlayers>& profiles,
                     std::span<const DeviceSnapshot> devices, const PortLayout& layout) {
  std::array<PortAccumulator, kMaxPlayers> acc{};
  for (const DeviceSnapshot& device : devices) {
    const auto it = layout.device_port.find(device.id);
    if (it == layout.device_port.end() || it->second >= kMaxPlayers) continue;
    const std::size_t port = it->second;
    if (!layout.seated[port]) continue;
    accumulate(profiles[port], device, acc[port]);
  }

  InputFrame out;
  out.frame = frame;
  for (std::size_t port = 0; port < kMaxPlayers; ++port) {
    out.pads[port] = finalize(profiles[port], acc[port], layout.seated[port]);
  }
  return out;
}

Mapper::Mapper() { set_profile_all(builtin_default_profile()); }

void Mapper::set_profile(std::size_t port, Profile profile) {
  if (port >= kMaxPlayers) return;
  profiles_[port] = std::move(profile);
  refresh(port);
}

void Mapper::set_profile_all(const Profile& profile) {
  for (std::size_t port = 0; port < kMaxPlayers; ++port) set_profile(port, profile);
}

void Mapper::set_minigame(std::string minigame_id) {
  if (minigame_id == minigame_) return;
  minigame_ = std::move(minigame_id);
  for (std::size_t port = 0; port < kMaxPlayers; ++port) refresh(port);
}

void Mapper::refresh(std::size_t port) { resolved_[port] = profiles_[port].resolve(minigame_); }

}  // namespace mpt::input
