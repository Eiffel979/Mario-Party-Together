#pragma once

// The mapping core: device snapshots + profiles + seating -> InputFrame.
// Everything here is a pure function of its arguments (no SDL, clock, RNG or
// threads), so it can be unit-tested with synthetic state and gives the same
// InputFrame on every host. InputFrame is the only thing that leaves input/.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "mpt/core/input.h"
#include "mpt/input/device.h"
#include "mpt/input/ports.h"
#include "mpt/input/profile.h"

namespace mpt::input {

inline constexpr std::int32_t kAxisMax = 32767;
// An analog source bound to a button control presses it at half deflection.
inline constexpr std::int32_t kAnalogPressThreshold = 16384;

// Per-control activation gathered from every device seated on one port.
// `analog` is the largest analog magnitude (0..kAxisMax); `digital` is true if
// any key or button bound to the control is held.
struct ControlValue {
  std::int32_t analog = 0;
  bool digital = false;
};
using PortAccumulator = std::array<ControlValue, kControlCount>;

// Integer square root, floor(sqrt(v)).
std::uint64_t isqrt(std::uint64_t v);

// Radial deadzone with rescale: inside the deadzone -> (0, 0); outside, the
// magnitude is remapped so the edge of the deadzone becomes 0 and full tilt stays full.
std::pair<std::int32_t, std::int32_t> apply_radial_deadzone(std::int32_t x, std::int32_t y, int deadzone_percent);
// Linear deadzone with rescale for a 0..kAxisMax trigger.
std::int32_t apply_trigger_deadzone(std::int32_t value, int deadzone_percent);

// Adds one device's contribution to a port accumulator (max for analog, OR for digital).
void accumulate(const ResolvedProfile& profile, const DeviceSnapshot& device, PortAccumulator& acc);

// Turns a port's accumulated controls into a PadState. Stick +Y is up, +X right.
PadState finalize(const ResolvedProfile& profile, const PortAccumulator& acc, bool seated);

// The whole frame: each device feeds its port, unassigned devices are ignored,
// unseated ports are neutral with connected = 0.
InputFrame map_frame(std::uint64_t frame, const std::array<ResolvedProfile, kMaxPlayers>& profiles,
                     std::span<const DeviceSnapshot> devices, const PortLayout& layout);

// Holds each port's profile and the current minigame context, and caches the
// resolved profiles. Still pure: map() depends only on its arguments and on
// what was set through the setters.
class Mapper {
 public:
  Mapper();

  void set_profile(std::size_t port, Profile profile);
  void set_profile_all(const Profile& profile);
  const Profile& profile(std::size_t port) const { return profiles_[port]; }

  // Selects per-minigame overrides ("mp4.minigame.<name>"); empty = none. The
  // shell passes the Director's current minigame. This only changes how local
  // devices become pads; the resulting InputFrame is what gets recorded and sent.
  void set_minigame(std::string minigame_id);
  const std::string& minigame() const { return minigame_; }

  InputFrame map(std::uint64_t frame, std::span<const DeviceSnapshot> devices, const PortLayout& layout) const {
    return map_frame(frame, resolved_, devices, layout);
  }

 private:
  void refresh(std::size_t port);

  std::array<Profile, kMaxPlayers> profiles_;
  std::array<ResolvedProfile, kMaxPlayers> resolved_;
  std::string minigame_;
};

}  // namespace mpt::input
