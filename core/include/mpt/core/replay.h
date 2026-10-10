#pragma once

// Replay format v0, `.mptr` (ADR 0006 and its P1-05 amendment). Pure library: no SDL,
// no filesystem, no clock. Callers read and write the bytes themselves.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mpt/core/dump.h"
#include "mpt/core/input.h"
#include "mpt/core/party_director.h"
#include "mpt/core/result.h"
#include "mpt/core/save_media.h"
#include "mpt/core/session.h"
#include "mpt/core/sha256.h"
#include "mpt/core/title_id.h"

namespace mpt {

inline constexpr std::array<char, 4> kReplayMagic = {'M', 'P', 'T', 'R'};
inline constexpr std::uint32_t kReplayFormatVersion = 0;
inline constexpr std::uint32_t kDefaultCheckpointInterval = 60;

// Taken after `frame` frames have been stepped (Session::frame() == frame).
struct ReplayCheckpoint {
  std::uint64_t frame = 0;
  std::uint64_t state_hash = 0;    // ITitleRuntime::stateHash()
  std::uint64_t party_digest = 0;  // party_events_digest(director().events())

  friend bool operator==(const ReplayCheckpoint&, const ReplayCheckpoint&) = default;
};

// The same pads for `length` consecutive frames (length >= 1).
struct ReplayInputRun {
  std::uint32_t length = 0;
  std::array<PadState, kMaxPlayers> pads{};

  friend bool operator==(const ReplayInputRun&, const ReplayInputRun&) = default;
};

struct ReplayHeader {
  std::uint32_t core_contract_version = 0;  // kCoreContractVersion at record time
  TitleId title{};
  Sha256Digest dump_sha256{};
  std::uint64_t seed = 0;                    // RuntimeConfig::seed
  Sha256Digest save_media_sha256{};          // all zero = blank media
  std::optional<SaveMedia> embedded_save_media{};  // the initial image, if carried
  std::uint32_t checkpoint_interval = kDefaultCheckpointInterval;

  bool blank_save_media() const { return save_media_sha256 == Sha256Digest{}; }

  friend bool operator==(const ReplayHeader&, const ReplayHeader&) = default;
};

struct Replay {
  ReplayHeader header{};
  std::uint64_t frame_count = 0;
  std::vector<ReplayInputRun> runs{};
  std::vector<ReplayCheckpoint> checkpoints{};

  friend bool operator==(const Replay&, const Replay&) = default;
};

// ---- Encoding --------------------------------------------------------------------------

// The image bytes a save-media digest covers: u32 format_version (LE) + data.
std::vector<std::byte> save_media_image_bytes(const SaveMedia& media);
// All zero for blank media (nullopt), else SHA-256 of save_media_image_bytes().
Sha256Digest save_media_digest(const std::optional<SaveMedia>& media);

// Canonical bytes of `replay`. Precondition: `replay` satisfies the strict-parse rules,
// including the u32 limits (embedded image, run count per u32 run length, checkpoint
// count). encode_replay does not re-check them. Replays from ReplayWriter and
// decode_replay always satisfy them.
std::vector<std::byte> encode_replay(const Replay& replay);

// Strict parse (ADR 0006 amendment, point 3): every byte must be consumed, and every
// rule must hold. Never crashes on hostile input.
std::variant<Replay, Result> decode_replay(std::span<const std::byte> bytes);

struct ReplayValidationRules {
  bool allow_embedded_save_media = true;
};
// decode_replay plus the extra rules. For tooling that only needs a verdict.
Result validate_replay(std::span<const std::byte> bytes, const ReplayValidationRules& rules = {});
// The rule for committed fixtures under tests/replays/ (P1-20e2): a full parse, no
// trailing bytes, and no embedded save media.
Result validate_replay_fixture(std::span<const std::byte> bytes);

// ---- Writer and reader -----------------------------------------------------------------

class ReplayWriter {
 public:
  // Overwrites header.core_contract_version with kCoreContractVersion. If the header is
  // unusable (checkpoint interval 0, an embedded image over UINT32_MAX - 4 bytes or not
  // matching save_media_sha256), every append() and addCheckpoint() fails with the reason.
  explicit ReplayWriter(ReplayHeader header);

  // inputs.frame must equal frameCount().
  Result append(const InputFrame& inputs);
  // Frames must be strictly increasing multiples of the interval, <= frameCount().
  Result addCheckpoint(const ReplayCheckpoint& checkpoint);

  std::uint64_t frameCount() const { return replay_.frame_count; }
  const ReplayHeader& header() const { return replay_.header; }

  // The finished replay. The writer is empty afterwards.
  Replay finish();

 private:
  Replay replay_;
  std::optional<std::string> header_error_;
};

// Iterates a decoded replay's frames in order, with InputFrame::frame filled in.
class ReplayReader {
 public:
  explicit ReplayReader(const Replay& replay) : replay_(&replay) {}

  const ReplayHeader& header() const { return replay_->header; }
  std::uint64_t frameCount() const { return replay_->frame_count; }
  std::span<const ReplayCheckpoint> checkpoints() const { return replay_->checkpoints; }

  // Writes the next frame and returns true, or returns false at the end.
  bool next(InputFrame& out);
  std::uint64_t position() const { return position_; }

 private:
  const Replay* replay_;
  std::size_t run_ = 0;
  std::uint32_t used_in_run_ = 0;
  std::uint64_t position_ = 0;
};

// ---- Session glue ----------------------------------------------------------------------

// Digest of the Director's events (ADR 0006 amendment, point 5).
std::uint64_t party_events_digest(std::span<const PartyEvent> events);

// The checkpoint for a session as it stands now.
ReplayCheckpoint checkpoint_of(Session& session);

// A header for recording a session started with `config`. Fails if config.dump.sha256
// is not 64 hex digits, if config.rules holds a ruleset (format v0 has none), or if
// `checkpoint_interval` is 0. `embed_save_media` copies the initial image into the file.
std::variant<ReplayHeader, Result> make_replay_header(const SessionConfig& config, bool embed_save_media,
                                                      std::uint32_t checkpoint_interval = kDefaultCheckpointInterval);

// The SessionConfig that plays `header` back with the user's verified dump. Fails if the
// dump's title or SHA-256 differs from the header. Save media: blank -> nullopt;
// embedded -> the embedded image; otherwise `supplied` if its digest matches, else a
// failure naming the digest the replay needs. `supplied` is ignored when not needed.
std::variant<SessionConfig, Result> session_config_for(const ReplayHeader& header, const VerifiedDump& dump,
                                                       const std::optional<SaveMedia>& supplied = std::nullopt);

// Records a live session. Call record() after every successful Session::step, with the
// same inputs. It appends them and, when session.frame() is a multiple of the interval,
// adds a checkpoint.
class ReplayRecorder {
 public:
  explicit ReplayRecorder(ReplayHeader header) : writer_(std::move(header)) {}

  Result record(const InputFrame& inputs, Session& session);
  std::uint64_t frameCount() const { return writer_.frameCount(); }
  Replay finish() { return writer_.finish(); }

 private:
  ReplayWriter writer_;
};

struct ReplayVerification {
  Result status = Result::success();        // failure if a step failed or a checkpoint differed
  std::uint64_t frames_played = 0;
  std::vector<ReplayCheckpoint> trail{};     // the checkpoints this playback produced
  std::optional<std::size_t> first_mismatch{};  // index into replay.checkpoints
};

// Plays `replay` on `session`, which must be at frame 0 (a fresh start from
// session_config_for). Stops at the first checkpoint that differs in state_hash or
// party_digest, or at the end.
ReplayVerification verify_replay(Session& session, const Replay& replay);

}  // namespace mpt
