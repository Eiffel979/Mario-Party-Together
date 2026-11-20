// E2 game-side proof for MP4 (M4-07, ruling M4-E in docs/phases/phase-1.md section 10).
//
// The rows of docs/titles/mp4.md section 18 as the dump lane plays them: each minigame's number, its
// category, and the controls its "Inputs read" cell lists. `mpt_dump_lane --boot mp4 --e2` boots
// straight into each minigame (Mp4Runtime::setTestStart), plays it with idle pads and then with each
// control injected on each port, and calls a control proven on a port when the state hash leaves the
// idle run's while both runs are still inside the minigame. Everything here is a pure function of its
// arguments, so tests/unit/test_dump_lane_e2.cpp checks it without a dump.
//
// Our own table of content IDs and control names: no game material.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "mpt/core/input.h"

namespace mpt::dump_lane::e2 {

enum class Control : std::uint8_t {
  stick,     // main stick, turning through right, up, left, down
  a,
  b,
  x,
  y,
  l_click,   // the digital L click (full travel)
  r_click,
  l_analog,  // L pressure past every threshold section 18 names, without the click
  r_analog,
  r_half,    // R held at half pressure (Bowser's Bigger Blast's partial pull)
  r_full,    // R held at full travel (its R=255 side)
  z,         // the null control: no listed row reads Z, so it must not diverge
};

constexpr std::string_view name(Control c) {
  switch (c) {
    case Control::stick: return "stick";
    case Control::a: return "A";
    case Control::b: return "B";
    case Control::x: return "X";
    case Control::y: return "Y";
    case Control::l_click: return "L-click";
    case Control::r_click: return "R-click";
    case Control::l_analog: return "L-analog";
    case Control::r_analog: return "R-analog";
    case Control::r_half: return "R-128";
    case Control::r_full: return "R-255";
    case Control::z: return "Z(null)";
  }
  return "?";
}

// Raw InputFrame trigger values. kAnalog clamps to 150 (PADClamp: raw - 30, at most 150), past every
// threshold in section 18 and past the game's own analog-L/R bit (clamped 64, raw 94), and below 255,
// which the SDK turns into the digital click. kHalf is M4-E's R=128 (clamped 98).
constexpr std::uint8_t kAnalog = 200;
constexpr std::uint8_t kHalf = 128;
constexpr std::uint8_t kFull = 255;

struct Row {
  std::string_view id;  // the section 18 ID
  std::uint16_t number; // the overlay m<number>Dll
  std::uint8_t type;    // the game's category (mgInfoTbl type), as in section 12
  std::vector<Control> controls;
  bool r_half_vs_full = false;  // M4-E's special check: R=128 and R=255 differ from each other
};

inline const std::vector<Row>& rows() {
  using C = Control;
  static const std::vector<Row> r = {
      {"mp4.minigame.manta_rings", 401, 0, {C::stick, C::a}},
      {"mp4.minigame.slime_time", 402, 0, {C::a}},
      {"mp4.minigame.booksquirm", 403, 0, {C::stick}},
      {"mp4.minigame.trace_race", 404, 4, {C::stick}},
      {"mp4.minigame.mario_medley", 405, 0, {C::a, C::b, C::l_click, C::r_click}},
      {"mp4.minigame.avalanche", 406, 0, {C::stick, C::a}},
      {"mp4.minigame.domination", 407, 0, {C::a}},
      {"mp4.minigame.paratrooper_plunge", 408, 0, {C::stick}},
      {"mp4.minigame.toads_quick_draw", 409, 0, {C::stick, C::a}},
      {"mp4.minigame.three_throw", 410, 0, {C::a, C::b}},
      {"mp4.minigame.photo_finish", 411, 0, {C::stick, C::a, C::l_analog, C::r_analog}},
      {"mp4.minigame.mr_blizzards_brigade", 412, 0, {C::stick}},
      {"mp4.minigame.bob_omb_breakers", 413, 0, {C::stick, C::a, C::b, C::x, C::y}},
      {"mp4.minigame.long_claw_of_the_law", 414, 0, {C::stick, C::a, C::l_analog, C::r_analog}},
      {"mp4.minigame.stamp_out", 415, 0, {C::stick, C::a}},
      {"mp4.minigame.candlelight_flight", 416, 1, {C::stick, C::a}},
      {"mp4.minigame.makin_waves", 417, 1, {C::stick, C::a, C::l_analog}},
      {"mp4.minigame.hide_and_go_boom", 418, 1, {C::a, C::b, C::x, C::y}},
      {"mp4.minigame.tree_stomp", 419, 1, {C::stick, C::a, C::b}},
      {"mp4.minigame.fish_n_drips", 420, 1, {C::a, C::b, C::x, C::y, C::l_click, C::r_click}},
      {"mp4.minigame.hop_or_pop", 421, 1, {C::stick, C::a}},
      {"mp4.minigame.money_belts", 422, 1, {C::stick}},
      {"mp4.minigame.goooooooal", 423, 1, {C::stick, C::a}},
      {"mp4.minigame.blame_it_on_the_crane", 424, 1, {C::stick, C::a}},
      {"mp4.minigame.the_great_deflate", 425, 2, {C::a}},
      {"mp4.minigame.revers_a_bomb", 426, 2, {C::stick, C::a}},
      {"mp4.minigame.right_oar_left", 427, 2, {C::a, C::b}},
      {"mp4.minigame.cliffhangers", 428, 2, {C::stick, C::a}},
      {"mp4.minigame.team_treasure_trek", 429, 2, {C::stick, C::x, C::y}},
      {"mp4.minigame.pair_a_sailing", 430, 2, {C::stick, C::a, C::b}},
      {"mp4.minigame.order_up", 431, 2, {C::stick, C::a}},
      {"mp4.minigame.dungeon_duos", 432, 2, {C::stick, C::a, C::b, C::x, C::y, C::l_analog, C::r_analog}},
      {"mp4.minigame.cheep_cheep_sweep", 434, 2, {C::stick, C::a, C::b}},
      {"mp4.minigame.darts_of_doom", 435, 3, {C::a}},
      {"mp4.minigame.fruits_of_doom", 436, 3, {C::stick, C::a}},
      {"mp4.minigame.balloon_of_doom", 437, 3, {C::a, C::l_click}},
      {"mp4.minigame.chain_chomp_fever", 438, 4, {C::stick}},
      {"mp4.minigame.paths_of_peril", 439, 4, {C::stick}},
      {"mp4.minigame.bowsers_bigger_blast", 440, 4, {C::r_half, C::r_click}, true},
      {"mp4.minigame.butterfly_blitz", 441, 4, {C::stick, C::a, C::b}},
      {"mp4.minigame.mario_speedwagons", 443, 0, {C::a, C::r_analog}},
      {"mp4.minigame.reversal_of_fortune", 444, 5, {C::stick, C::a, C::r_analog}},
      {"mp4.minigame.bowser_bop", 445, 6, {C::stick, C::a}},
      {"mp4.minigame.mystic_match_em", 446, 6, {C::a, C::stick}},
      {"mp4.minigame.archaeologuess", 447, 6, {C::stick, C::a}},
      {"mp4.minigame.goombas_chip_flip", 448, 6, {C::stick, C::a, C::l_click}},
      {"mp4.minigame.kareening_koopa", 449, 6, {C::stick}},
      {"mp4.minigame.rumble_fishing", 455, 4, {C::stick, C::a}},
      {"mp4.minigame.take_a_breather", 456, 0, {C::stick, C::a}},
  };
  return r;
}

// Injection comes in bursts so the check sees the game's reaction, not the pad itself. The state hash
// covers the game's pad arrays (HuPadBtn and the rest), which copy the pad every frame; a hash taken
// while a control is held would differ from idle even in a minigame that ignores it. So every control
// is active for kActive frames of each kCycle and idle for the rest, and checkpoints fall on the last
// frame of each idle gap, when the pad, the game's pad arrays and their repeat counters are back at
// idle. A hash difference there is game state the input changed. The Z null control (a button no
// listed row reads) checks that the gap is long enough: it must not diverge.
constexpr std::uint64_t kCycle = 32;
constexpr std::uint64_t kActive = 24;
// Inside the active part buttons pulse (held kPulseOn of every kPulsePeriod frames), so the game sees
// presses and holds; the stick holds one direction per cycle, turning right, up, left, down.
constexpr std::uint64_t kPulsePeriod = 8;
constexpr std::uint64_t kPulseOn = 4;

// Whether frame t (frames since the minigame started) is a checkpoint: the end of an idle gap.
constexpr bool is_checkpoint(std::uint64_t t) { return t % kCycle == kCycle - 1; }

// An idle port: connected, nothing pressed. Every port is connected in every run.
inline PadState idle_pad() {
  PadState p;
  p.connected = 1;
  return p;
}

// The pad for control `c`, `t` frames after the minigame started (the first frame its overlay runs).
// Control::r_half and r_full hold their pressure through the active part, as a partial pull would.
inline PadState inject(Control c, std::uint64_t t) {
  PadState p = idle_pad();
  if (t % kCycle >= kActive) return p;
  const bool on = t % kPulsePeriod < kPulseOn;
  switch (c) {
    case Control::stick: {
      static constexpr std::int16_t kDir[4][2] = {{32767, 0}, {0, 32767}, {-32767, 0}, {0, -32767}};
      const auto& d = kDir[(t / kCycle) % 4];
      p.stick_x = d[0];
      p.stick_y = d[1];
      break;
    }
    case Control::a: p.buttons = on ? kButtonA : 0; break;
    case Control::b: p.buttons = on ? kButtonB : 0; break;
    case Control::x: p.buttons = on ? kButtonX : 0; break;
    case Control::y: p.buttons = on ? kButtonY : 0; break;
    case Control::z: p.buttons = on ? kButtonZ : 0; break;
    case Control::l_click: p.buttons = on ? kButtonL : 0; break;
    case Control::r_click: p.buttons = on ? kButtonR : 0; break;
    case Control::l_analog: p.trigger_l = on ? kAnalog : 0; break;
    case Control::r_analog: p.trigger_r = on ? kAnalog : 0; break;
    case Control::r_half: p.trigger_r = kHalf; break;
    case Control::r_full: p.trigger_r = kFull; break;
  }
  return p;
}

// One checkpoint of a run: frames since the minigame started, the state hash, and whether the minigame's
// overlay was still running.
struct Checkpoint {
  std::uint64_t t;
  std::uint64_t hash;
  bool in_minigame;
};

// The first checkpoint where `run` leaves `idle` while both are inside the minigame, or nullopt.
// Checkpoints are compared by position; both runs take them at the same frames.
inline std::optional<std::uint64_t> first_divergence(const std::vector<Checkpoint>& idle,
                                                     const std::vector<Checkpoint>& run) {
  const std::size_t n = idle.size() < run.size() ? idle.size() : run.size();
  for (std::size_t i = 0; i < n; ++i) {
    if (idle[i].t != run[i].t) return std::nullopt;  // misaligned trails prove nothing
    if (!idle[i].in_minigame || !run[i].in_minigame) return std::nullopt;
    if (idle[i].hash != run[i].hash) return run[i].t;
  }
  return std::nullopt;
}

// The focus player of a run on `port`: the seat the run's injected port drives in a single-seat
// category. Every port is a human player whose pad is its own index, so focus = port.
constexpr int focus_for_port(int port) { return port; }

}  // namespace mpt::dump_lane::e2
