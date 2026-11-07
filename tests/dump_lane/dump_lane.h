#pragma once

// The dump lane (P1-20d, ADR 0006): verifies committed replay fixtures against a real
// game on a developer's own machine. This header holds the whole verification path so
// that CI can prove it with FakeRuntime (tests/unit/test_dump_lane.cpp), while the
// dump-labelled mpt_dump_lane runs it for real titles once their runtimes exist (P1-16).
//
// One fixture is tests/replays/<name>.mptr plus its sidecar <name>.trail.txt, the
// cross-platform golden trail: one "<frame> <state_hash>" line per checkpoint.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "../support/determinism_harness.h"
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
  // The wheel's window in mentDll, as frames since mentDll started (measured on seed 1; see
  // docs/titles/mp4.md section 21). Outside it the autopilot never presses a direction.
  std::uint64_t wheel_from = 0, wheel_to = 0;
};

// mentDll's wheel order: position -> GWSystem.board (mentDll main.c, fn_1_99CC); it starts at
// position 2. Returns the signed number of moves from the default to `board`, or 0 if not listed.
inline int board_wheel_moves(std::int32_t board) {
  constexpr std::int32_t kWheel[6] = {1, 2, 0, 3, 4, 5};
  for (int p = 0; p < 6; ++p)
    if (kWheel[p] == board) return p - 2;
  return 0;
}

// Player 1's pad for the step of `frame`, given the key and frames since its last change as of the
// step before. The autopilot:
//   - presses nothing before boot_wait;
//   - pulses START on the boot/title overlay and on the minigame instruction screen (instDll);
//   - in a minigame, repeats a 120-frame cycle: 40 frames of the stick held down, then A mashed (2
//     frames on, 2 off) with the stick released. Mashing alone soft-locks a human seat in Reversal of
//     Fortune (m444), whose plunger only fires once the stick has pulled it back;
//   - otherwise pulses A (4 frames every 30) on menus, setup, the board and the results;
//   - on mentDll's board wheel window, holds the direction toward script.board instead.
inline PadState party_autopilot(const PartyScript& script, std::uint64_t frame, const PartyKey& key,
                                std::uint64_t since_change) {
  PadState pad;
  pad.connected = 1;
  if (frame < script.boot_wait) return pad;
  const std::string_view overlay = key.overlay;
  if (overlay == "bootDll" || overlay == "instDll") {
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
    // One 4-frame direction pulse per 30 frames, one per move: the wheel takes a held direction once
    // it has been idle for 21 frames, then waits again, so each pulse moves it exactly one board. No A
    // is pressed in the window.
    const int moves = board_wheel_moves(script.board);
    const std::uint64_t in_window = since_change - script.wheel_from;
    const std::uint64_t pulses = static_cast<std::uint64_t>(moves < 0 ? -moves : moves);
    if (in_window / 30 < pulses && in_window % 30 < 4) pad.buttons |= moves < 0 ? kButtonDpadLeft : kButtonDpadRight;
    return pad;
  }
  if (since_change >= 30 && since_change % 30 < 4) pad.buttons |= kButtonA;
  return pad;
}

}  // namespace mpt::dump_lane
