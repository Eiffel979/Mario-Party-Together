#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mpt {

inline constexpr std::size_t kMaxPlayers = 4;

// Canonical buttons. Each runtime maps these onto its console's pad; input/
// owns the mapping from physical devices onto this layout.
enum Button : std::uint32_t {
  kButtonA = 1u << 0,
  kButtonB = 1u << 1,
  kButtonX = 1u << 2,
  kButtonY = 1u << 3,
  kButtonL = 1u << 4,
  kButtonR = 1u << 5,
  kButtonZ = 1u << 6,
  kButtonZL = 1u << 7,
  kButtonStart = 1u << 8,
  kButtonSelect = 1u << 9,
  kButtonDpadUp = 1u << 10,
  kButtonDpadDown = 1u << 11,
  kButtonDpadLeft = 1u << 12,
  kButtonDpadRight = 1u << 13,
  kButtonCUp = 1u << 14,
  kButtonCDown = 1u << 15,
  kButtonCLeft = 1u << 16,
  kButtonCRight = 1u << 17,
  kButtonHome = 1u << 18,
};

// One player's controller for one frame. Plain data with fixed-width fields so it
// can be serialized byte-for-byte into replays and netplay packets.
struct PadState {
  std::uint32_t buttons = 0;          // Button bitmask
  std::int16_t stick_x = 0;           // left stick, -32768..32767
  std::int16_t stick_y = 0;
  std::int16_t c_stick_x = 0;         // right / C stick
  std::int16_t c_stick_y = 0;
  std::uint8_t trigger_l = 0;         // analog triggers, 0..255
  std::uint8_t trigger_r = 0;
  std::uint8_t connected = 0;         // 1 if a player is on this port
  std::uint8_t reserved = 0;
  std::array<std::int16_t, 3> accel{};  // motion (Wii Remote, Joy-Con), units defined by input/
  std::array<std::int16_t, 3> gyro{};
  std::int16_t pointer_x = 0;          // IR pointer, normalized to -32768..32767
  std::int16_t pointer_y = 0;

  friend bool operator==(const PadState&, const PadState&) = default;
};

// All players' input for one emulated frame. `frame` is the frame number the
// inputs apply to; the shell, replays and lockstep netplay all agree on it.
struct InputFrame {
  std::uint64_t frame = 0;
  std::array<PadState, kMaxPlayers> pads{};

  friend bool operator==(const InputFrame&, const InputFrame&) = default;
};

}  // namespace mpt

static_assert(sizeof(mpt::PadState) == 32, "PadState is serialized byte-for-byte; keep it packed");
