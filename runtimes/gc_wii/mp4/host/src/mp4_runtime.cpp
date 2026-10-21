// See mpt/mp4/runtime.h.
#include "mpt/mp4/runtime.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "module_loader.h"

#ifndef MPT_MP4_MODULE_BASE
#error "MPT_MP4_MODULE_BASE comes from runtimes/gc_wii/mp4/layout.cmake"
#endif
#ifndef MPT_MP4_ARENA_BASE
#error "MPT_MP4_ARENA_BASE comes from runtimes/gc_wii/mp4/layout.cmake"
#endif

static_assert(sizeof(MptMp4InputFrame) == sizeof(mpt::InputFrame), "MptMp4InputFrame mirrors mpt::InputFrame");
static_assert(sizeof(MptMp4Pad) == sizeof(mpt::PadState), "MptMp4Pad mirrors mpt::PadState");
static_assert(offsetof(MptMp4InputFrame, pads) == offsetof(mpt::InputFrame, pads));
static_assert(offsetof(MptMp4Pad, stick_x) == offsetof(mpt::PadState, stick_x));
static_assert(offsetof(MptMp4Pad, trigger_l) == offsetof(mpt::PadState, trigger_l));
static_assert(offsetof(MptMp4Pad, accel) == offsetof(mpt::PadState, accel));
static_assert(offsetof(MptMp4Pad, pointer_y) == offsetof(mpt::PadState, pointer_y));
static_assert(sizeof(MptMp4FrameRate) == sizeof(mpt::FrameRate));

namespace mpt::mp4 {
namespace {

#if defined(_WIN32)
constexpr const char* kModuleFile = "mp4_game.dll";
#else
constexpr const char* kModuleFile = "mp4_game.so";
#endif

// UTF-8 bytes of a path, for the C ABI.
std::string utf8(const std::filesystem::path& p) {
  const std::u8string u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::string message(const MptMp4Error& e) { return std::string(e.message, strnlen(e.message, sizeof e.message)); }

MptMp4Error newError() {
  MptMp4Error e{};
  e.struct_size = sizeof e;
  return e;
}

}  // namespace

Mp4Runtime::Mp4Runtime() = default;

Mp4Runtime::~Mp4Runtime() {
  if (api_ != nullptr && game_ != nullptr) api_->destroy(game_);
}

Result Mp4Runtime::boot(const VerifiedDump& dump, const RuntimeConfig& config) {
  if (dump.title != TitleId::mp4) return Result::failure("Mp4Runtime: the dump is not Mario Party 4");
  std::string error;
  api_ = load_module(dump.build_dir / "module" / kModuleFile, static_cast<std::uintptr_t>(MPT_MP4_MODULE_BASE), error);
  if (api_ == nullptr) return Result::failure("Mp4Runtime: " + error);
  if (game_ != nullptr) {
    api_->destroy(game_);
    game_ = nullptr;
  }
  const std::string disc = utf8(dump.build_dir / "disc");
  const std::string cache = utf8(dump.build_dir / "gx-cache");
  MptMp4BootConfig boot{};
  boot.struct_size = sizeof boot;
  boot.abi_version = MPT_MP4_ABI_VERSION;
  boot.seed = config.seed;
  boot.disc_root = disc.c_str();
  boot.flags = kGameplayFlags;
  boot.internal_scale = config.internal_scale == 0 ? 1 : config.internal_scale;
  boot.audio_output = 1;
  if (config.save_media) {
    boot.save_media = reinterpret_cast<const std::uint8_t*>(config.save_media->data.data());
    boot.save_media_size = config.save_media->data.size();
    boot.save_media_format = config.save_media->format_version;
  }
  boot.arena_base = static_cast<std::uintptr_t>(MPT_MP4_ARENA_BASE);
  boot.cache_dir = cache.c_str();

  // The module's GX backend is fixed when it is built: try the renderer, then the null backend.
  MptMp4Error e = newError();
  boot.video_backend = MPT_MP4_VIDEO_GPU;
  MptMp4Status status = api_->create(&boot, &game_, &e);
  if (status == MPT_MP4_ERR_ARGUMENT && std::string(e.message).find("null GX backend") != std::string::npos) {
    e = newError();
    boot.video_backend = MPT_MP4_VIDEO_NULL;
    status = api_->create(&boot, &game_, &e);
  }
  if (status != MPT_MP4_OK) {
    game_ = nullptr;
    return Result::failure("Mp4Runtime: " + message(e));
  }
  api_->set_report(game_, report_, report_user_);
  health_ = RuntimeHealth{};
  return Result::success();
}

void Mp4Runtime::updateHealth(MptMp4StepResult result) {
  switch (result) {
    case MPT_MP4_STEP_RUNNING:
      health_ = RuntimeHealth{};
      return;
    case MPT_MP4_STEP_HALTED:
      health_.status = RuntimeStatus::halted;
      break;
    default:
      health_.status = RuntimeStatus::faulted;
      break;
  }
  health_.message = api_->fault(game_);
}

void Mp4Runtime::stepFrame(const InputFrame& inputs) {
  if (game_ == nullptr || health_.status != RuntimeStatus::running) return;
  MptMp4InputFrame in;
  std::memcpy(&in, &inputs, sizeof in);
  updateHealth(api_->step(game_, &in));
}

FrameOutput Mp4Runtime::video() {
  FrameOutput out;
  if (game_ == nullptr) return out;
  MptMp4Video v{};
  v.struct_size = sizeof v;
  api_->video(game_, &v);
  if (v.pixels == nullptr) return out;
  out.kind = FrameOutput::Kind::cpu_rgba8;
  out.width = v.width;
  out.height = v.height;
  out.pixels = {v.pixels, std::size_t{v.width} * v.height};
  return out;
}

AudioChunk Mp4Runtime::audio() {
  AudioChunk out;
  if (game_ == nullptr) return out;
  MptMp4Audio a{};
  a.struct_size = sizeof a;
  api_->audio(game_, &a);
  out.sample_rate = a.sample_rate;
  out.channels = static_cast<std::uint8_t>(a.channels);
  if (a.samples != nullptr) out.samples = {a.samples, a.frame_count * a.channels};
  return out;
}

SaveState Mp4Runtime::saveState() {
  SaveState state;
  state.title = TitleId::mp4;
  if (game_ == nullptr) return state;
  state.format_version = api_->state_format();
  state.frame = api_->frame(game_);
  std::size_t size = 0;
  if (api_->save_state(game_, nullptr, 0, &size) != MPT_MP4_OK) return state;
  state.data.resize(size);
  if (api_->save_state(game_, reinterpret_cast<std::uint8_t*>(state.data.data()), size, &size) != MPT_MP4_OK)
    state.data.clear();
  return state;
}

Result Mp4Runtime::loadState(const SaveState& state) {
  if (game_ == nullptr) return Result::failure("Mp4Runtime: loadState before boot");
  if (state.title != TitleId::mp4) return Result::failure("Mp4Runtime: the state is not from Mario Party 4");
  if (state.format_version != api_->state_format())
    return Result::failure("Mp4Runtime: state format " + std::to_string(state.format_version) + ", this build reads " +
                           std::to_string(api_->state_format()));
  MptMp4Error e = newError();
  if (api_->load_state(game_, reinterpret_cast<const std::uint8_t*>(state.data.data()), state.data.size(), &e) !=
      MPT_MP4_OK)
    return Result::failure("Mp4Runtime: " + message(e));
  if (api_->frame(game_) != state.frame) return Result::failure("Mp4Runtime: the state's frame does not match its data");
  // Status is part of the state (ADR 0011): a halted or faulted state stays so after the load.
  updateHealth(api_->status(game_));
  return Result::success();
}

std::uint64_t Mp4Runtime::stateHash() { return game_ != nullptr ? api_->state_hash(game_) : 0; }

FrameRate Mp4Runtime::frameRate() const {
  if (game_ == nullptr) return FrameRate{60000, 1001};
  MptMp4FrameRate r{};
  api_->frame_rate(game_, &r);
  return FrameRate{r.numerator, r.denominator};
}

std::uint64_t Mp4Runtime::saveMediaGeneration() const {
  return game_ != nullptr ? api_->save_media_generation(game_) : 0;
}

SaveMedia Mp4Runtime::saveMedia() const {
  SaveMedia media;
  if (game_ == nullptr) return media;
  std::size_t size = 0;
  std::uint32_t format = 0;
  if (api_->save_media(game_, nullptr, 0, &size, &format) != MPT_MP4_OK) return media;
  media.format_version = format;
  media.data.resize(size);
  if (api_->save_media(game_, reinterpret_cast<std::uint8_t*>(media.data.data()), size, &size, &format) != MPT_MP4_OK)
    media.data.clear();
  return media;
}

bool Mp4Runtime::readParty(MptMp4Party& out) const {
  if (game_ == nullptr) return false;
  out = MptMp4Party{};
  out.struct_size = sizeof out;
  api_->party(game_, &out);
  return true;
}

void Mp4Runtime::setReportSink(void (*sink)(void* user, const char* text), void* user) {
  report_ = sink;
  report_user_ = user;
  if (game_ != nullptr) api_->set_report(game_, sink, user);
}

}  // namespace mpt::mp4
