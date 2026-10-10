#include "mpt/core/replay.h"

#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "mpt/core/session.h"
#include "mpt/core/state.h"
#include "mpt/core/title_runtime.h"

namespace mpt {
namespace {

// ---- Little-endian byte writer and bounds-checked reader --------------------------------

class ByteWriter {
 public:
  void u8(std::uint8_t v) { out_.push_back(static_cast<std::byte>(v)); }
  void u16(std::uint16_t v) { put(v, 2); }
  void u32(std::uint32_t v) { put(v, 4); }
  void u64(std::uint64_t v) { put(v, 8); }
  void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
  void bytes(std::span<const std::byte> b) { out_.insert(out_.end(), b.begin(), b.end()); }
  void digest(const Sha256Digest& d) {
    for (std::uint8_t b : d) u8(b);
  }
  std::vector<std::byte> take() { return std::move(out_); }

 private:
  void put(std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) out_.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xff));
  }
  std::vector<std::byte> out_;
};

class ByteReader {
 public:
  explicit ByteReader(std::span<const std::byte> in) : in_(in) {}

  std::size_t remaining() const { return in_.size() - at_; }
  std::size_t offset() const { return at_; }

  bool u8(std::uint8_t& v) {
    std::uint64_t w = 0;
    if (!get(w, 1)) return false;
    v = static_cast<std::uint8_t>(w);
    return true;
  }
  bool u16(std::uint16_t& v) {
    std::uint64_t w = 0;
    if (!get(w, 2)) return false;
    v = static_cast<std::uint16_t>(w);
    return true;
  }
  bool u32(std::uint32_t& v) {
    std::uint64_t w = 0;
    if (!get(w, 4)) return false;
    v = static_cast<std::uint32_t>(w);
    return true;
  }
  bool u64(std::uint64_t& v) { return get(v, 8); }
  bool i16(std::int16_t& v) {
    std::uint16_t w = 0;
    if (!u16(w)) return false;
    v = static_cast<std::int16_t>(w);
    return true;
  }
  bool digest(Sha256Digest& d) {
    if (remaining() < d.size()) return false;
    for (std::uint8_t& b : d) b = std::to_integer<std::uint8_t>(in_[at_++]);
    return true;
  }
  bool bytes(std::size_t n, std::span<const std::byte>& out) {
    if (remaining() < n) return false;
    out = in_.subspan(at_, n);
    at_ += n;
    return true;
  }

 private:
  bool get(std::uint64_t& v, int n) {
    if (remaining() < static_cast<std::size_t>(n)) return false;
    v = 0;
    for (int i = 0; i < n; ++i) v |= std::to_integer<std::uint64_t>(in_[at_++]) << (8 * i);
    return true;
  }
  std::span<const std::byte> in_;
  std::size_t at_ = 0;
};

constexpr std::size_t kPadBytes = 32;
constexpr std::size_t kRunBytes = 4 + kMaxPlayers * kPadBytes;
constexpr std::size_t kCheckpointBytes = 24;

void write_pad(ByteWriter& w, const PadState& p) {
  w.u32(p.buttons);
  w.i16(p.stick_x);
  w.i16(p.stick_y);
  w.i16(p.c_stick_x);
  w.i16(p.c_stick_y);
  w.u8(p.trigger_l);
  w.u8(p.trigger_r);
  w.u8(p.connected);
  w.u8(p.reserved);
  for (std::int16_t v : p.accel) w.i16(v);
  for (std::int16_t v : p.gyro) w.i16(v);
  w.i16(p.pointer_x);
  w.i16(p.pointer_y);
}

bool read_pad(ByteReader& r, PadState& p) {
  bool ok = r.u32(p.buttons) && r.i16(p.stick_x) && r.i16(p.stick_y) && r.i16(p.c_stick_x) && r.i16(p.c_stick_y) &&
            r.u8(p.trigger_l) && r.u8(p.trigger_r) && r.u8(p.connected) && r.u8(p.reserved);
  for (std::int16_t& v : p.accel) ok = ok && r.i16(v);
  for (std::int16_t& v : p.gyro) ok = ok && r.i16(v);
  return ok && r.i16(p.pointer_x) && r.i16(p.pointer_y);
}

bool known_title(std::uint8_t value) {
  for (TitleId t : kAllTitles) {
    if (static_cast<std::uint8_t>(t) == value) return true;
  }
  return false;
}

Result fail_at(const ByteReader& r, std::string_view what) {
  return Result::failure("replay: " + std::string(what) + " (at byte " + std::to_string(r.offset()) + ")");
}

Result truncated(const ByteReader& r, std::string_view field) {
  return fail_at(r, "file ends inside " + std::string(field));
}

std::optional<SaveMedia> media_from_image_bytes(std::span<const std::byte> image) {
  if (image.size() < 4) return std::nullopt;
  SaveMedia media;
  ByteReader r(image);
  if (!r.u32(media.format_version)) return std::nullopt;
  media.data.assign(image.begin() + 4, image.end());
  return media;
}

// Checkpoint ordering rule shared by the writer and the reader.
Result check_checkpoint(const ReplayCheckpoint& c, std::optional<std::uint64_t> previous_frame,
                        std::uint32_t interval, std::uint64_t frame_count) {
  if (c.frame == 0 || c.frame % interval != 0) {
    return Result::failure("checkpoint frame " + std::to_string(c.frame) + " is not a positive multiple of " +
                           std::to_string(interval));
  }
  if (previous_frame && c.frame <= *previous_frame) {
    return Result::failure("checkpoint frame " + std::to_string(c.frame) + " does not increase");
  }
  if (c.frame > frame_count) {
    return Result::failure("checkpoint frame " + std::to_string(c.frame) + " is past the last frame " +
                           std::to_string(frame_count));
  }
  return Result::success();
}

void hash_string(StateHasher& h, std::string_view s) {
  h.update_u64(s.size());
  h.update(std::as_bytes(std::span(s.data(), s.size())));
}

void hash_id(StateHasher& h, const std::optional<ContentId>& id) {
  h.update_u64(id ? 1 : 0);
  if (id) hash_string(h, id->str());
}

void hash_snapshot(StateHasher& h, const PartySnapshot& s) {
  hash_id(h, s.board);
  h.update_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(s.turn)));
  h.update_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(s.max_turns)));
  for (const PlayerState& p : s.players) {
    hash_id(h, p.character);
    h.update_u64(p.is_cpu ? 1 : 0);
    h.update_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(p.coins)));
    h.update_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(p.stars)));
    h.update_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(p.board_space)));
  }
  h.update_u64(static_cast<std::uint64_t>(s.phase));
  hash_id(h, s.current_minigame);
}

}  // namespace

// ---- Encoding --------------------------------------------------------------------------

std::vector<std::byte> save_media_image_bytes(const SaveMedia& media) {
  ByteWriter w;
  w.u32(media.format_version);
  w.bytes(media.data);
  return w.take();
}

Sha256Digest save_media_digest(const std::optional<SaveMedia>& media) {
  if (!media) return Sha256Digest{};
  return Sha256::of(save_media_image_bytes(*media));
}

std::vector<std::byte> encode_replay(const Replay& replay) {
  const ReplayHeader& h = replay.header;
  ByteWriter w;
  for (char c : kReplayMagic) w.u8(static_cast<std::uint8_t>(c));
  w.u32(kReplayFormatVersion);
  w.u32(h.core_contract_version);
  w.u8(static_cast<std::uint8_t>(h.title));
  w.digest(h.dump_sha256);
  w.u64(h.seed);
  w.digest(h.save_media_sha256);
  if (h.embedded_save_media) {
    const std::vector<std::byte> image = save_media_image_bytes(*h.embedded_save_media);
    w.u32(static_cast<std::uint32_t>(image.size()));
    w.bytes(image);
  } else {
    w.u32(0);
  }
  w.u32(0);  // ruleset: none in format v0
  w.u64(replay.frame_count);
  w.u32(h.checkpoint_interval);
  for (const ReplayInputRun& run : replay.runs) {
    w.u32(run.length);
    for (const PadState& pad : run.pads) write_pad(w, pad);
  }
  w.u32(static_cast<std::uint32_t>(replay.checkpoints.size()));
  for (const ReplayCheckpoint& c : replay.checkpoints) {
    w.u64(c.frame);
    w.u64(c.state_hash);
    w.u64(c.party_digest);
  }
  return w.take();
}

std::variant<Replay, Result> decode_replay(std::span<const std::byte> bytes) {
  ByteReader r(bytes);
  Replay replay;
  ReplayHeader& h = replay.header;

  std::span<const std::byte> magic;
  if (!r.bytes(kReplayMagic.size(), magic)) return truncated(r, "the magic");
  if (std::memcmp(magic.data(), kReplayMagic.data(), kReplayMagic.size()) != 0) return fail_at(r, "not an .mptr file");

  std::uint32_t format_version = 0;
  if (!r.u32(format_version)) return truncated(r, "format_version");
  if (format_version != kReplayFormatVersion) {
    return fail_at(r, "unsupported format_version " + std::to_string(format_version));
  }
  if (!r.u32(h.core_contract_version)) return truncated(r, "core_contract_version");
  if (h.core_contract_version != kCoreContractVersion) {
    return fail_at(r, "recorded with core contract " + std::to_string(h.core_contract_version) + ", this build is " +
                          std::to_string(kCoreContractVersion));
  }
  std::uint8_t title = 0;
  if (!r.u8(title)) return truncated(r, "title");
  if (!known_title(title)) return fail_at(r, "unknown title " + std::to_string(title));
  h.title = static_cast<TitleId>(title);
  if (!r.digest(h.dump_sha256)) return truncated(r, "dump_sha256");
  if (!r.u64(h.seed)) return truncated(r, "seed");
  if (!r.digest(h.save_media_sha256)) return truncated(r, "save_media_sha256");

  std::uint32_t embedded_size = 0;
  if (!r.u32(embedded_size)) return truncated(r, "save_media_embedded length");
  std::span<const std::byte> embedded;
  if (!r.bytes(embedded_size, embedded)) return truncated(r, "save_media_embedded");
  if (embedded_size != 0) {
    if (h.blank_save_media()) return fail_at(r, "embedded save media in a replay of blank media");
    if (Sha256::of(embedded) != h.save_media_sha256) return fail_at(r, "embedded save media does not match its digest");
    h.embedded_save_media = media_from_image_bytes(embedded);
    if (!h.embedded_save_media) return fail_at(r, "embedded save media is shorter than its 4-byte version");
  }

  std::uint32_t ruleset_size = 0;
  if (!r.u32(ruleset_size)) return truncated(r, "ruleset length");
  if (ruleset_size != 0) return fail_at(r, "a ruleset is not supported in format v0");

  if (!r.u64(replay.frame_count)) return truncated(r, "frame_count");
  if (!r.u32(h.checkpoint_interval)) return truncated(r, "checkpoint_interval");
  if (h.checkpoint_interval == 0) return fail_at(r, "checkpoint_interval is 0");

  std::uint64_t frames = 0;
  while (frames < replay.frame_count) {
    if (r.remaining() < kRunBytes) return truncated(r, "the input runs");
    ReplayInputRun run;
    if (!r.u32(run.length)) return truncated(r, "an input run");
    if (run.length == 0) return fail_at(r, "an input run of length 0");
    if (run.length > replay.frame_count - frames) return fail_at(r, "the input runs exceed frame_count");
    for (PadState& pad : run.pads) {
      if (!read_pad(r, pad)) return truncated(r, "an input run");
    }
    frames += run.length;
    replay.runs.push_back(run);
  }

  std::uint32_t checkpoint_count = 0;
  if (!r.u32(checkpoint_count)) return truncated(r, "the checkpoint count");
  if (r.remaining() / kCheckpointBytes < checkpoint_count) return truncated(r, "the checkpoints");
  replay.checkpoints.reserve(checkpoint_count);
  std::optional<std::uint64_t> previous;
  for (std::uint32_t i = 0; i < checkpoint_count; ++i) {
    ReplayCheckpoint c;
    if (!r.u64(c.frame) || !r.u64(c.state_hash) || !r.u64(c.party_digest)) return truncated(r, "a checkpoint");
    if (Result ok = check_checkpoint(c, previous, h.checkpoint_interval, replay.frame_count); !ok) {
      return fail_at(r, ok.error());
    }
    previous = c.frame;
    replay.checkpoints.push_back(c);
  }

  if (r.remaining() != 0) return fail_at(r, std::to_string(r.remaining()) + " trailing bytes");
  return replay;
}

Result validate_replay(std::span<const std::byte> bytes, const ReplayValidationRules& rules) {
  auto decoded = decode_replay(bytes);
  if (Result* failure = std::get_if<Result>(&decoded)) return std::move(*failure);
  const Replay& replay = std::get<Replay>(decoded);
  if (!rules.allow_embedded_save_media && replay.header.embedded_save_media) {
    return Result::failure("replay: embedded save media is not allowed here (it is user data)");
  }
  return Result::success();
}

Result validate_replay_fixture(std::span<const std::byte> bytes) {
  return validate_replay(bytes, ReplayValidationRules{.allow_embedded_save_media = false});
}

// ---- Writer and reader -----------------------------------------------------------------

ReplayWriter::ReplayWriter(ReplayHeader header) {
  replay_.header = std::move(header);
  replay_.header.core_contract_version = kCoreContractVersion;
}

Result ReplayWriter::append(const InputFrame& inputs) {
  if (inputs.frame != replay_.frame_count) {
    return Result::failure("replay: input for frame " + std::to_string(inputs.frame) + " appended at frame " +
                           std::to_string(replay_.frame_count));
  }
  if (!replay_.runs.empty() && replay_.runs.back().pads == inputs.pads &&
      replay_.runs.back().length < std::numeric_limits<std::uint32_t>::max()) {
    ++replay_.runs.back().length;
  } else {
    replay_.runs.push_back(ReplayInputRun{1, inputs.pads});
  }
  ++replay_.frame_count;
  return Result::success();
}

Result ReplayWriter::addCheckpoint(const ReplayCheckpoint& checkpoint) {
  std::optional<std::uint64_t> previous;
  if (!replay_.checkpoints.empty()) previous = replay_.checkpoints.back().frame;
  if (Result ok = check_checkpoint(checkpoint, previous, replay_.header.checkpoint_interval, replay_.frame_count);
      !ok) {
    return Result::failure("replay: " + ok.error());
  }
  replay_.checkpoints.push_back(checkpoint);
  return Result::success();
}

Replay ReplayWriter::finish() { return std::exchange(replay_, Replay{replay_.header, 0, {}, {}}); }

bool ReplayReader::next(InputFrame& out) {
  while (run_ < replay_->runs.size() && used_in_run_ >= replay_->runs[run_].length) {
    ++run_;
    used_in_run_ = 0;
  }
  if (run_ >= replay_->runs.size()) return false;
  out.frame = position_++;
  out.pads = replay_->runs[run_].pads;
  ++used_in_run_;
  return true;
}

// ---- Session glue ----------------------------------------------------------------------

std::uint64_t party_events_digest(std::span<const PartyEvent> events) {
  StateHasher h;
  h.update_u64(events.size());
  for (const PartyEvent& e : events) {
    h.update_u64(static_cast<std::uint64_t>(e.kind));
    h.update_u64(e.frame);
    hash_id(h, e.minigame);
    hash_snapshot(h, e.snapshot);
  }
  return h.digest();
}

ReplayCheckpoint checkpoint_of(Session& session) {
  return ReplayCheckpoint{session.frame(), session.runtime().stateHash(),
                          party_events_digest(session.director().events())};
}

std::variant<ReplayHeader, Result> make_replay_header(const SessionConfig& config, bool embed_save_media,
                                                      std::uint32_t checkpoint_interval) {
  const std::optional<Sha256Digest> dump = parse_sha256_hex(config.dump.sha256);
  if (!dump) return Result::failure("replay: the dump's sha256 is not 64 hex digits");
  if (config.rules) return Result::failure("replay: format v0 cannot record a ruleset");
  if (checkpoint_interval == 0) return Result::failure("replay: checkpoint interval 0");

  ReplayHeader h;
  h.core_contract_version = kCoreContractVersion;
  h.title = config.dump.title;
  h.dump_sha256 = *dump;
  h.seed = config.runtime.seed;
  h.save_media_sha256 = save_media_digest(config.runtime.save_media);
  if (embed_save_media && config.runtime.save_media) h.embedded_save_media = config.runtime.save_media;
  h.checkpoint_interval = checkpoint_interval;
  return h;
}

std::variant<SessionConfig, Result> session_config_for(const ReplayHeader& header, const VerifiedDump& dump,
                                                       const std::optional<SaveMedia>& supplied) {
  if (dump.title != header.title) {
    return Result::failure("replay: recorded for " + std::string(to_string(header.title)) + ", the dump is " +
                           std::string(to_string(dump.title)));
  }
  const std::optional<Sha256Digest> dump_digest = parse_sha256_hex(dump.sha256);
  if (!dump_digest || *dump_digest != header.dump_sha256) {
    return Result::failure("replay: recorded with dump " + to_hex(header.dump_sha256) + ", this dump is " + dump.sha256);
  }

  SessionConfig config;
  config.dump = dump;
  config.runtime.seed = header.seed;
  if (header.blank_save_media()) {
    config.runtime.save_media = std::nullopt;
  } else if (header.embedded_save_media) {
    config.runtime.save_media = header.embedded_save_media;
  } else if (supplied && save_media_digest(supplied) == header.save_media_sha256) {
    config.runtime.save_media = supplied;
  } else {
    return Result::failure("replay: needs the save image with SHA-256 " + to_hex(header.save_media_sha256) +
                           (supplied ? ", and the supplied image does not match" : ", and none was supplied"));
  }
  return config;
}

Result ReplayRecorder::record(const InputFrame& inputs, Session& session) {
  if (session.frame() != inputs.frame + 1) {
    return Result::failure("replay: record() must follow the step of frame " + std::to_string(inputs.frame));
  }
  if (Result appended = writer_.append(inputs); !appended) return appended;
  if (session.frame() % writer_.header().checkpoint_interval == 0) return writer_.addCheckpoint(checkpoint_of(session));
  return Result::success();
}

ReplayVerification verify_replay(Session& session, const Replay& replay) {
  ReplayVerification v;
  if (session.frame() != 0) {
    v.status = Result::failure("replay: verification needs a session at frame 0");
    return v;
  }
  const std::uint32_t interval = replay.header.checkpoint_interval;
  std::size_t next_checkpoint = 0;
  ReplayReader reader(replay);
  InputFrame inputs;
  while (reader.next(inputs)) {
    if (Result stepped = session.step(inputs); !stepped) {
      v.status = Result::failure("replay: step failed at frame " + std::to_string(inputs.frame) + ": " + stepped.error());
      return v;
    }
    ++v.frames_played;
    if (session.frame() % interval != 0) continue;
    const ReplayCheckpoint got = checkpoint_of(session);
    v.trail.push_back(got);
    if (next_checkpoint < replay.checkpoints.size() && replay.checkpoints[next_checkpoint].frame == got.frame) {
      const ReplayCheckpoint& want = replay.checkpoints[next_checkpoint];
      if (want != got) {
        v.first_mismatch = next_checkpoint;
        const char* what = want.state_hash != got.state_hash ? "state hash" : "party digest";
        v.status = Result::failure("replay: " + std::string(what) + " differs at frame " + std::to_string(got.frame));
        return v;
      }
      ++next_checkpoint;
    }
  }
  if (next_checkpoint != replay.checkpoints.size()) {
    v.first_mismatch = next_checkpoint;
    v.status = Result::failure("replay: checkpoint at frame " + std::to_string(replay.checkpoints[next_checkpoint].frame) +
                               " was never reached");
  }
  return v;
}

}  // namespace mpt
