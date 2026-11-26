#pragma once

// The dump lane (P1-20d, ADR 0006): verifies committed replay fixtures against a real
// game on a developer's own machine. This header holds the whole verification path so
// that CI can prove it with FakeRuntime (tests/unit/test_dump_lane.cpp), while the
// dump-labelled mpt_dump_lane runs it for real titles once their runtimes exist (P1-16).
//
// One fixture is tests/replays/<name>.mptr plus its sidecar <name>.trail.txt, the
// cross-platform golden trail: one "<frame> <state_hash>" line per checkpoint.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "../support/adapter_checks.h"
#include "../support/determinism_harness.h"
#include "mpt/core/party_director.h"
#include "mpt/core/replay.h"
#include "mpt/core/session.h"
#include "mpt/core/title_registry.h"

namespace mpt::dump_lane {

struct FixtureFiles {
  std::string name;               // file stem, e.g. "mp4_full_party"
  std::filesystem::path replay{};  // <name>.mptr
  std::filesystem::path trail{};   // <name>.trail.txt (may not exist)
};

// Every .mptr in 'dir' (not recursive), sorted by name. Empty if 'dir' does not exist.
inline std::vector<FixtureFiles> find_fixtures(const std::filesystem::path& dir) {
  std::vector<FixtureFiles> out;
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) return out;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".mptr") continue;
    FixtureFiles f;
    f.name = entry.path().stem().string();
    f.replay = entry.path();
    f.trail = entry.path().parent_path() / (f.name + ".trail.txt");
    out.push_back(std::move(f));
  }
  std::sort(out.begin(), out.end(), [](const FixtureFiles& a, const FixtureFiles& b) { return a.name < b.name; });
  return out;
}

inline std::optional<std::vector<std::byte>> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(chars.size());
  std::transform(chars.begin(), chars.end(), out.begin(), [](char c) { return static_cast<std::byte>(c); });
  return out;
}

// The checkpoints as a sidecar trail: frame and state hash only (ADR 0006 amendment, point 5).
inline test::HashTrail state_trail(std::span<const ReplayCheckpoint> checkpoints) {
  test::HashTrail trail;
  for (const ReplayCheckpoint& c : checkpoints) trail.push_back(test::Checkpoint{c.frame, c.state_hash});
  return trail;
}

// Verifies one fixture:
//   1. the committed-fixture rule (validate_replay_fixture) and the decode;
//   2. the sidecar parses and equals the replay's own recorded state trail;
//   3. session_config_for() with the user's verified dump (and save image, if needed);
//   4. a Session started through the registry plays the replay, and verify_replay()
//      finds every checkpoint's state hash and party digest equal;
//   5. the trail this playback produced equals the sidecar (the cross-OS golden).
inline Result verify_fixture(std::span<const std::byte> replay_bytes, std::string_view sidecar_text,
                             const TitleRegistry& registry, const VerifiedDump& dump,
                             const std::optional<SaveMedia>& supplied_media = std::nullopt) {
  if (Result r = validate_replay_fixture(replay_bytes); !r) return Result::failure("fixture rule: " + r.error());
  auto decoded = decode_replay(replay_bytes);
  if (Result* failure = std::get_if<Result>(&decoded)) return std::move(*failure);
  const Replay& replay = std::get<Replay>(decoded);

  const std::optional<test::HashTrail> sidecar = test::parse_trail(sidecar_text);
  if (!sidecar) return Result::failure("the .trail.txt sidecar is malformed");
  const test::HashTrail recorded = state_trail(replay.checkpoints);
  if (test::first_divergence(*sidecar, recorded)) {
    return Result::failure("the sidecar does not match the replay's checkpoints: " +
                           test::describe_divergence(*sidecar, recorded));
  }

  auto config = session_config_for(replay.header, dump, supplied_media);
  if (Result* failure = std::get_if<Result>(&config)) return std::move(*failure);
  auto started = Session::start(registry, std::get<SessionConfig>(config));
  if (Result* failure = std::get_if<Result>(&started)) return Result::failure("session: " + failure->error());
  std::unique_ptr<Session>& session = std::get<std::unique_ptr<Session>>(started);

  const ReplayVerification verification = verify_replay(*session, replay);
  if (!verification.status) return verification.status;
  if (verification.frames_played != replay.frame_count) {
    return Result::failure("played " + std::to_string(verification.frames_played) + " of " +
                           std::to_string(replay.frame_count) + " frames");
  }
  const test::HashTrail produced = state_trail(verification.trail);
  if (test::first_divergence(*sidecar, produced)) {
    return Result::failure("this build's trail differs from the sidecar (sidecar vs this build): " +
                           test::describe_divergence(*sidecar, produced));
  }
  return Result::success();
}

// ---- The --party autopilot (M4-01) -----------------------------------------------------------
//
// A scripted party on one human seat: player 1's pad is a pure function of the frame, the Director's
// phase, the running overlay and the frames since the last party change. No clock, no RNG: the same
// inputs give the same pads, so a recorded run replays exactly.

// What the autopilot keys on. A "party change" is any change of the Director's phase, the running
// overlay or the turn; `since_change` is the number of frames stepped since the last one.
struct PartyKey {
  PartyPhase phase = PartyPhase::none;
  std::string overlay;  // the running overlay's name, empty before the first
  std::int32_t turn = 0;

  friend bool operator==(const PartyKey&, const PartyKey&) = default;
};

// Tracks the frames since the last party change, and is the soft-lock watchdog: stuck() once the key
// has stayed the same for `limit` frames.
class PartyChangeTracker {
 public:
  explicit PartyChangeTracker(std::uint64_t limit = 36'000) : limit_(limit) {}

  // The key read after stepping frame `frame` (frames increase by one per call).
  void observe(std::uint64_t frame, const PartyKey& key) {
    if (!seen_ || key != key_) {
      key_ = key;
      changed_at_ = frame;
      seen_ = true;
    }
    last_ = frame;
  }
  // Puts the tracker back as it was at frame `now` (after loading a state taken then).
  void restore(const PartyKey& key, std::uint64_t changed_at, std::uint64_t now) {
    key_ = key;
    changed_at_ = changed_at;
    last_ = now;
    seen_ = true;
  }
  // Frames stepped since the last change (0 on the frame of the change).
  std::uint64_t sinceChange() const { return seen_ ? last_ - changed_at_ : 0; }
  bool stuck() const { return seen_ && sinceChange() >= limit_; }
  const PartyKey& key() const { return key_; }
  std::uint64_t changedAt() const { return changed_at_; }

 private:
  std::uint64_t limit_;
  PartyKey key_{};
  bool seen_ = false;
  std::uint64_t changed_at_ = 0;
  std::uint64_t last_ = 0;
};

// The menu script's choices.
struct PartyScript {
  // Board index on mentDll's board wheel, by GWSystem.board (BOARD_ID_*). 0 (Toad's Midway Madness)
  // is the wheel's default and needs no move; any other board is reached by holding left or right
  // in the wheel's window (below).
  std::int32_t board = 0;
  // No input before this frame (the boot logos).
  std::uint64_t boot_wait = 900;
  // The wheel's window in mentDll, as frames since mentDll started (kBoardWheelFrom by default with
  // --party-board; see docs/titles/mp4.md, M4-03). Outside it the autopilot never pulls the stick.
  std::uint64_t wheel_from = 0, wheel_to = 0;
};

// The first frame of mentDll (frames since it started) at which the board wheel takes the stick, with
// the autopilot's A pulses before it (measured on seeds 1 and 2; docs/titles/mp4.md, M4-03).
inline constexpr std::uint64_t kBoardWheelFrom = 1448;
// Frames between the autopilot's stick pulses on the wheel, one move each.
inline constexpr std::uint64_t kBoardWheelPulse = 60;

// mentDll's wheel order: position -> GWSystem.board (mentDll main.c, fn_1_99CC); it starts at
// position 2. Returns the signed number of moves from the default to `board`, or 0 if not listed.
inline int board_wheel_moves(std::int32_t board) {
  constexpr std::int32_t kWheel[6] = {1, 2, 0, 3, 4, 5};
  for (int p = 0; p < 6; ++p)
    if (kWheel[p] == board) return p - 2;
  return 0;
}

// Frames with no party change (phase, overlay or turn) after which the autopilot also pulses B.
inline constexpr std::uint64_t kPartyStallFrames = 18000;
// The stall's stick cycle (party_autopilot): kStallTurn frames of turning the stick, kStallCorner
// frames toward the top left corner, then rows of kStallRow frames to the end of the cycle. The box
// is 192 pixels wide (24 frames at 8 pixels) and 96 high (12 one-frame steps down, 13 rows); 40
// frames on the diagonal cross it from any corner, and the cycle leaves room for 16 rows.
inline constexpr std::uint64_t kStallCycle = 720;
inline constexpr std::uint64_t kStallTurn = 264;
inline constexpr std::uint64_t kStallCorner = 40;
inline constexpr std::uint64_t kStallRow = 26;

// Player 1's pad for the step of `frame`, given the key and frames since its last change as of the
// step before. The autopilot:
//   - presses nothing before boot_wait;
//   - pulses START on the boot/title overlay, on the minigame instruction screen (instDll) and on the
//     final ranking;
//   - in a minigame, repeats a 120-frame cycle: 40 frames of the stick held down, then A mashed (2
//     frames on, 2 off) with the stick released. Mashing alone soft-locks a human seat in Reversal of
//     Fortune (m444), whose plunger only fires once the stick has pulled it back;
//   - otherwise pulses A (4 frames every 30) on menus, setup, the board and the results;
//   - on mentDll's board wheel window, pulls the stick toward script.board instead;
//   - after kPartyStallFrames with no party change, also pulses B and moves the stick: it turns it
//     clockwise, then sweeps a box row by row, in turn.
inline PadState party_autopilot(const PartyScript& script, std::uint64_t frame, const PartyKey& key,
                                std::uint64_t since_change) {
  PadState pad;
  pad.connected = 1;
  if (frame < script.boot_wait) return pad;
  const std::string_view overlay = key.overlay;
  // START also leaves the final ranking (mstory3Dll, once finished): A there opens the records view,
  // which only B leaves.
  if (overlay == "bootDll" || overlay == "instDll" || (overlay == "mstory3Dll" && key.phase == PartyPhase::finished)) {
    if (since_change >= 60 && since_change % 60 < 6) pad.buttons |= kButtonStart;
    return pad;
  }
  if (key.phase == PartyPhase::minigame) {
    if (frame % 120 < 40) {
      pad.stick_y = -32767;
    } else if (frame % 4 < 2) {
      pad.buttons |= kButtonA;
    }
    return pad;
  }
  if (overlay == "mentDll" && key.phase == PartyPhase::none && script.board != 0 &&
      since_change >= script.wheel_from && since_change < script.wheel_to) {
    // One 4-frame stick pulse per kBoardWheelPulse frames, one per move: the wheel takes a held direction
    // once it has been idle for 21 frames and its board models have settled (measured: 30 frames is too
    // short), then waits again, so each pulse moves it exactly one board. It
    // reads the stick (|x| >= 50) or HuPadBtn's d-pad bits, but Party Board's pad.c masks the d-pad out
    // of HuPadBtn, so only the stick moves it. No A is pressed in the window.
    const int moves = board_wheel_moves(script.board);
    const std::uint64_t in_window = since_change - script.wheel_from;
    const std::uint64_t pulses = static_cast<std::uint64_t>(moves < 0 ? -moves : moves);
    if (in_window / kBoardWheelPulse < pulses && in_window % kBoardWheelPulse < 4) pad.stick_x = moves < 0 ? -32767 : 32767;
    return pad;
  }
  if (since_change >= 30 && since_change % 30 < 4) pad.buttons |= kButtonA;
  // A stall (M4-03): after kPartyStallFrames with no party change, B also pulses (4 frames every 60,
  // between the A pulses), for a menu that A alone would loop in and only B leaves. It was added for
  // the seed-2 soft locks before they were traced to the lottery (below); no run is known to need it.
  // The longest normal stretch seen is 10,480 frames, so runs that never stall are unchanged, and the
  // watchdog still catches a real lock.
  if (since_change >= kPartyStallFrames && since_change % 60 >= 45 && since_change % 60 < 49) pad.buttons |= kButtonB;
  // The same stall also moves the stick, for the lottery's two games (board/lottery.c), which wait on
  // a human seat's stick with no time limit and which nothing else on the board leaves:
  //   - the ball game (ExecBallGame) until the stick has cranked its handle one full turn. It counts
  //     a sample only when an axis is past 57.6, the sample differs from the last one and the angle
  //     grew, so the stick turns clockwise through up, right, down and left, two frames each;
  //   - the scratch ticket (ExecScratchTicket) until the hand, which moves 8 pixels a frame toward
  //     the stick inside a 192 by 96 box, has rubbed off all but 300 of the ticket's pixels. The
  //     stick sends the hand to the top left corner, then sweeps it across row by row.
  // The two alternate in a kStallCycle-frame cycle (kStallTurn frames of turning, then the sweep).
  if (since_change >= kPartyStallFrames) {
    const std::uint64_t in_cycle = (since_change - kPartyStallFrames) % kStallCycle;
    if (in_cycle < kStallTurn) {
      constexpr std::int16_t kTurn[4][2] = {{0, 32767}, {32767, 0}, {0, -32767}, {-32767, 0}};
      const auto& dir = kTurn[(in_cycle / 2) % 4];
      pad.stick_x = dir[0];
      pad.stick_y = dir[1];
    } else if (in_cycle < kStallTurn + kStallCorner) {
      pad.stick_x = -32767;  // up and left, to the corner
      pad.stick_y = 32767;
    } else {
      // Rows of kStallRow frames: across (right on even rows, left on odd ones), then one frame down.
      const std::uint64_t in_sweep = in_cycle - kStallTurn - kStallCorner;
      if (in_sweep % kStallRow == kStallRow - 1) {
        pad.stick_y = -32767;
      } else {
        pad.stick_x = (in_sweep / kStallRow) % 2 == 0 ? 32767 : -32767;
      }
    }
  }
  return pad;
}


// ---- P1-17: the expected PartyEvent sequence of a whole party ------------------------------------------
// Structural rules, not a recorded golden: what any full party of `expected` must produce, whatever the
// seed's minigames and frames. The dump lane checks the M4-01 autopilot party against them
// (--party-events); tests/unit/test_dump_lane_party.cpp proves the rules on synthetic event lists.
struct FullPartyExpectation {
  TitleId title = TitleId::mp4;
  std::int32_t max_turns = 20;
  std::optional<BoardId> board{};
  std::array<bool, kMaxPlayers> is_cpu{{false, true, true, true}};
  // Whether a regular party can launch the minigame (mpt::mp4::party_eligible for MP4). Unset: any.
  std::function<bool(const MinigameId&)> party_eligible{};
};

inline const char* phase_text(PartyPhase phase) {
  switch (phase) {
    case PartyPhase::none: return "none";
    case PartyPhase::setup: return "setup";
    case PartyPhase::board: return "board";
    case PartyPhase::minigame: return "minigame";
    case PartyPhase::results: return "results";
    case PartyPhase::finished: return "finished";
  }
  return "?";
}

// The phase changes a party may make (ADR 0005, docs/titles/mp4.md section 21): none, setup, then
// board turns with their minigames, then results and finished, never back.
inline bool party_phase_step_allowed(PartyPhase from, PartyPhase to) {
  switch (from) {
    case PartyPhase::none: return to == PartyPhase::setup;
    case PartyPhase::setup: return to == PartyPhase::board;
    case PartyPhase::board: return to == PartyPhase::minigame || to == PartyPhase::results;
    case PartyPhase::minigame: return to == PartyPhase::board;
    case PartyPhase::results: return to == PartyPhase::finished;
    case PartyPhase::finished: return false;
  }
  return false;
}

inline std::string id_text(const std::optional<ContentId>& id) { return id ? id->str() : std::string("nothing"); }

// Every problem with `events` (the Director's, from boot on) and `final_party` (its mirror at the end)
// as one full party of `expected`; empty when there is none.
inline std::vector<std::string> check_full_party(std::span<const PartyEvent> events, const PartySnapshot& final_party,
                                                 const Catalog& catalog, const FullPartyExpectation& expected) {
  std::vector<std::string> problems;
  const auto at = [](const PartyEvent& e, const std::string& what) {
    return "frame " + std::to_string(e.frame) + ": " + what;
  };
  const auto check_minigame = [&](const PartyEvent& e, const MinigameId& id) {
    if (id.title() != expected.title || id.kind() != "minigame") {
      problems.push_back(at(e, id.str() + " is not a minigame of this title"));
    } else if (!test::catalog_lists(catalog.minigames, id)) {
      problems.push_back(at(e, id.str() + " is not in the catalog"));
    } else if (expected.party_eligible && !expected.party_eligible(id)) {
      problems.push_back(at(e, id.str() + " is not party-eligible"));
    }
  };

  PartyPhase phase = PartyPhase::none;
  std::int32_t last_turn = 0;
  std::optional<MinigameId> running;
  std::size_t minigames = 0;
  std::uint64_t last_frame = 0;
  for (const PartyEvent& e : events) {
    if (e.frame < last_frame) problems.push_back(at(e, "events out of frame order"));
    last_frame = e.frame;
    if (const std::string p = test::check_snapshot(e.snapshot, catalog); !p.empty()) problems.push_back(at(e, p));
    const bool in_party = e.snapshot.phase != PartyPhase::none && e.snapshot.phase != PartyPhase::setup;
    if (in_party && e.snapshot.max_turns != expected.max_turns) {
      problems.push_back(at(e, "max_turns " + std::to_string(e.snapshot.max_turns) + ", expected " +
                                   std::to_string(expected.max_turns)));
    }
    if (in_party && expected.board && e.snapshot.board != expected.board) {
      problems.push_back(at(e, "the board is " + id_text(e.snapshot.board) + ", expected " + expected.board->str()));
    }
    switch (e.kind) {
      case PartyEvent::Kind::phase_changed:
        if (!party_phase_step_allowed(phase, e.snapshot.phase)) {
          problems.push_back(at(e, std::string("phase ") + phase_text(phase) + " -> " + phase_text(e.snapshot.phase) +
                                       " is not a step of a party"));
        }
        phase = e.snapshot.phase;
        break;
      case PartyEvent::Kind::turn_started:
        if (e.snapshot.turn != last_turn + 1) {
          problems.push_back(at(e, "turn " + std::to_string(e.snapshot.turn) + " started after turn " +
                                       std::to_string(last_turn)));
        }
        last_turn = e.snapshot.turn;
        break;
      case PartyEvent::Kind::minigame_started:
        if (!e.minigame) {
          problems.push_back(at(e, "minigame_started without a minigame"));
          break;
        }
        if (running) problems.push_back(at(e, e.minigame->str() + " started while " + running->str() + " runs"));
        if (e.snapshot.phase != PartyPhase::minigame) {
          problems.push_back(at(e, e.minigame->str() + " started outside the minigame phase"));
        }
        check_minigame(e, *e.minigame);
        running = e.minigame;
        ++minigames;
        break;
      case PartyEvent::Kind::minigame_ended:
        if (!e.minigame || e.minigame != running) {
          problems.push_back(at(e, "minigame_ended for " + id_text(e.minigame) + ", running " + id_text(running)));
        }
        running.reset();
        break;
    }
  }
  if (running) problems.push_back(running->str() + " never ended");
  if (last_turn != expected.max_turns) {
    problems.push_back("the last turn started was " + std::to_string(last_turn) + ", expected " +
                       std::to_string(expected.max_turns));
  }
  // Every turn ends in a minigame; battle, Bowser and other space events can add more.
  if (minigames < static_cast<std::size_t>(expected.max_turns)) {
    problems.push_back(std::to_string(minigames) + " minigames for " + std::to_string(expected.max_turns) + " turns");
  }
  if (phase != PartyPhase::finished) problems.push_back(std::string("the events end in phase ") + phase_text(phase));

  // The final snapshot: the finished party's last turn, its board and its seats.
  const PartySnapshot& f = final_party;
  if (const std::string p = test::check_snapshot(f, catalog); !p.empty()) problems.push_back("final: " + p);
  if (f.phase != PartyPhase::finished) problems.push_back(std::string("final: phase ") + phase_text(f.phase));
  if (f.turn != expected.max_turns || f.max_turns != expected.max_turns) {
    problems.push_back("final: turn " + std::to_string(f.turn) + "/" + std::to_string(f.max_turns) + ", expected " +
                       std::to_string(expected.max_turns) + "/" + std::to_string(expected.max_turns));
  }
  if (expected.board && f.board != expected.board) problems.push_back("final: not on " + expected.board->str());
  for (std::size_t i = 0; i < kMaxPlayers; ++i) {
    const PlayerState& p = f.players[i];
    const std::string who = "final: player " + std::to_string(i + 1);
    if (!p.character) problems.push_back(who + " has no character");
    if (p.is_cpu != expected.is_cpu[i]) problems.push_back(who + (p.is_cpu ? " is a CPU" : " is human"));
    if (p.coins < 0 || p.stars < 0) problems.push_back(who + " has negative coins or stars");
  }
  return problems;
}

}  // namespace mpt::dump_lane
