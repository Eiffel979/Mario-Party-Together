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

}  // namespace mpt::dump_lane
