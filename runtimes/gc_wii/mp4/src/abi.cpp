// The module side of include/mpt/mp4/game_abi.h: one MptMp4Game owns the SDK (with MusyX attached), the
// GX renderer, the disc and the user's main.dol, and runs the game's main loop as the SDK's root
// coroutine. Host-side state only: nothing here is game state, so this file is compiled without the
// game-data section header, and no pointer kept here reaches the arena or a registered state region.
#include "mpt/mp4/game_abi.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "copy_trap.h"
#include "mpt/core/input.h"
#include "mpt/core/state.h"
#include "mpt/gc/file_source.h"
#include "mpt/gc/musyx_engine.h"
#include "mpt/gc/sdk.h"
#include "mpt/gx/renderer.h"
#include "mpt_mp4_port.h"
#include "mpt_mp4_reads.h"
#include "mpt_mp4_rel.h"

#ifndef MPT_MP4_BUILD_ID
#error "MPT_MP4_BUILD_ID must be defined by runtimes/gc_wii/mp4/CMakeLists.txt"
#endif

static_assert(sizeof(MptMp4InputFrame) == sizeof(mpt::InputFrame), "MptMp4InputFrame mirrors mpt::InputFrame");
static_assert(sizeof(MptMp4Pad) == sizeof(mpt::PadState), "MptMp4Pad mirrors mpt::PadState");
static_assert(offsetof(MptMp4InputFrame, pads) == offsetof(mpt::InputFrame, pads));
static_assert(offsetof(MptMp4Pad, stick_x) == offsetof(mpt::PadState, stick_x));
static_assert(offsetof(MptMp4Pad, c_stick_y) == offsetof(mpt::PadState, c_stick_y));
static_assert(offsetof(MptMp4Pad, trigger_l) == offsetof(mpt::PadState, trigger_l));
static_assert(offsetof(MptMp4Pad, connected) == offsetof(mpt::PadState, connected));
static_assert(offsetof(MptMp4Pad, accel) == offsetof(mpt::PadState, accel));
static_assert(offsetof(MptMp4Pad, gyro) == offsetof(mpt::PadState, gyro));
static_assert(offsetof(MptMp4Pad, pointer_y) == offsetof(mpt::PadState, pointer_y));

namespace {
enum class Status : std::uint32_t { running = 0, halted = 1, faulted = 2 };
}  // namespace

struct MptMp4Game {
  // Boot settings, copied at create.
  std::string disc_root;
  std::string cache_dir;
  std::uint64_t seed = 0;
  std::uint32_t gameplay_flags = 0;  // determinism input: hashed and saved
  bool audio_output = true;
  std::uintptr_t arena_base = 0;
  bool copy_trap = false;

  std::unique_ptr<mpt::gc::DirectoryFileSource> files;
  std::vector<unsigned char> dol;  // the user's main.dol; the game gets copies of its blobs
  std::unique_ptr<mpt::gx::Renderer> renderer;
  mpt::gx::ObjectIdCounters boot_ids{};
  std::unique_ptr<mpt::gc::Sdk> sdk;

  // Soft resets (OSResetSystem) start a new SDK; frame numbers and the card's generation continue.
  std::uint64_t frame_base = 0;
  std::uint64_t generation_base = 0;
  std::uint32_t resets = 0;

  Status status = Status::running;
  std::string fault;

  // The last step's outputs, valid until the next step, load_state or destroy.
  mpt::gx::FrameImage image;
  std::vector<std::int16_t> audio;

  MptMp4ReportFn report = nullptr;
  void* report_user = nullptr;
};

namespace {

MptMp4Game* g_game = nullptr;

constexpr std::uint32_t kStateMagic = 0x4D503453;  // "MP4S": the module's trailer after the SDK state
constexpr std::uint32_t kStateFormat = 2;          // the blob's layout version (SaveState::format_version)

void setError(MptMp4Error* error, const std::string& message) {
  if (error == nullptr || error->struct_size < MPT_MP4_ERROR_SIZE_V1_0) return;
  std::snprintf(error->message, sizeof error->message, "%s", message.c_str());
}

void gameMain(void*) { mpt_mp4_game_main(); }

bool readFile(const std::filesystem::path& path, std::vector<unsigned char>& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  const std::streamoff size = f.tellg();
  if (size <= 0) return false;
  out.resize(static_cast<std::size_t>(size));
  f.seekg(0);
  f.read(reinterpret_cast<char*>(out.data()), size);
  return static_cast<bool>(f);
}

// Memory outside the arena that holds game state: the game core's data and every REL's (ADR 0008 §10).
std::vector<std::span<std::byte>> gameStateRegions() {
  std::vector<std::span<std::byte>> regions;
  auto add = [&](unsigned char* lo, unsigned char* hi) {
    if (lo != nullptr && hi > lo)
      regions.emplace_back(reinterpret_cast<std::byte*>(lo), static_cast<std::size_t>(hi - lo));
  };
  add(mpt_mp4_game_data_lo(), mpt_mp4_game_data_hi());
  for (unsigned i = 0; i < mpt_mp4_rel_count; ++i) add(mpt_mp4_rel_table[i]->data_lo, mpt_mp4_rel_table[i]->data_hi);
  return regions;
}

// A fresh boot of the game: pristine game data, the DOL blobs, the session's gameplay options, a reset
// sound engine and renderer, and a new SDK on `card` (nullopt = a blank card).
bool boot(MptMp4Game& game, std::optional<mpt::SaveMedia> card, std::string& error) {
  game.sdk.reset();
  if (!mpt_mp4_game_data_reset()) {
    error = "out of memory for the game's pristine data";
    return false;
  }
  char dol_error[256] = {};
  if (!mpt_mp4_dol_attach(game.dol.data(), game.dol.size(), dol_error, sizeof dol_error)) {
    error = std::string("disc: ") + dol_error;
    return false;
  }
  mpt_mp4_port_config = MptMp4PortConfig{};
  mpt_mp4_port_config.skip_boot_sequence = (game.gameplay_flags & MPT_MP4_BOOT_SKIP_BOOT_SEQUENCE) != 0;
  mpt_mp4_port_config.unlock_all_minigames = (game.gameplay_flags & MPT_MP4_BOOT_UNLOCK_ALL_MINIGAMES) != 0;
  mpt_mp4_port_config.unlock_bowsers_gnarly_party =
      (game.gameplay_flags & MPT_MP4_BOOT_UNLOCK_BOWSERS_GNARLY_PARTY) != 0;

  mpt::gx::set_object_id_counters(game.boot_ids);
  game.renderer->reset_for_load_state();
  if (game.copy_trap) mpt::mp4::copy_trap_forget();

  mpt::gc::SdkConfig sc;
  sc.seed = game.seed;
  sc.video = mpt::gc::VideoStandard::ntsc;
  sc.files = game.files.get();
  sc.card.game_code = {{'G', 'M', 'P', 'E'}};
  sc.card.maker_code = {{'0', '1'}};
  sc.card.image = std::move(card);
  // MP4's I/O callback mode (P1-16 audit, mp4.md section 14): before return. armem.c spins on
  // `while (HuARDMACheck());` right after ARQPostRequest with no SDK call in the loop, so a callback
  // left for the next call boundary would never run.
  sc.io_callbacks_before_return = true;
  // Party Board's TARGET_PC heaps are four times the console's (malloc.c, 64-bit structures): 78 MiB of
  // game heaps plus the rest of OSAlloc's arena. A per-title constant (mp4.md section 19).
  sc.main_ram_bytes = 96u << 20;
  sc.arena_base = game.arena_base;
  sc.audio_engine = mpt::gc::MusyxEngine::engine();
  sc.audio_output = game.audio_output;
  sc.state_regions = mpt::gc::MusyxEngine::stateRegions();
  for (auto r : gameStateRegions()) sc.state_regions.push_back(r);

  mpt::gc::MusyxEngine::reset();
  mpt::Result r = mpt::gc::Sdk::create(sc, &gameMain, nullptr, game.sdk);
  if (!r) {
    error = "sdk: " + r.error();
    return false;
  }
  game.sdk->setReportSink([g = &game](const char* text) {
    if (g->report != nullptr) g->report(g->report_user, text);
  });
  return true;
}

MptMp4Status create(const MptMp4BootConfig* in, MptMp4Game** out, MptMp4Error* error) {
  if (out == nullptr || in == nullptr) {
    setError(error, "create: missing arguments");
    return MPT_MP4_ERR_ARGUMENT;
  }
  *out = nullptr;
  if (in->struct_size < MPT_MP4_BOOT_CONFIG_SIZE_V1_0) {
    setError(error, "create: MptMp4BootConfig is shorter than v1.0");
    return MPT_MP4_ERR_VERSION;
  }
  MptMp4BootConfig config{};  // fields past the caller's struct_size keep their v1.0 defaults
  std::memcpy(&config, in, std::min<std::size_t>(in->struct_size, sizeof config));
  if (config.abi_version != MPT_MP4_ABI_VERSION) {
    setError(error, "create: unsupported ABI version");
    return MPT_MP4_ERR_VERSION;
  }
  if (config.disc_root == nullptr) {
    setError(error, "create: no disc_root");
    return MPT_MP4_ERR_ARGUMENT;
  }
  if (g_game != nullptr) {
    setError(error, "create: a game already exists in this process");
    return MPT_MP4_ERR_BUSY;
  }
  auto game = std::make_unique<MptMp4Game>();
  game->disc_root = config.disc_root;
  if (config.cache_dir != nullptr) game->cache_dir = config.cache_dir;
  game->seed = config.seed;
  game->gameplay_flags = config.flags & MPT_MP4_BOOT_GAMEPLAY_MASK;
  game->audio_output = config.audio_output != 0;
  game->arena_base = config.arena_base;

  std::string why;
  // UTF-8 bytes to a path (u8path is deprecated in C++20).
  const std::filesystem::path root{
      std::u8string(reinterpret_cast<const char8_t*>(game->disc_root.data()), game->disc_root.size())};
  game->files = mpt::gc::DirectoryFileSource::open(root, why);
  if (!game->files) {
    setError(error, "disc: " + why);
    return MPT_MP4_ERR_DISC;
  }
  if (!readFile(root / "sys" / "main.dol", game->dol)) {
    setError(error, "disc: cannot read sys/main.dol");
    return MPT_MP4_ERR_DISC;
  }
  if (!mpt_mp4_rel_init()) {
    setError(error, "out of memory for the overlay table");
    return MPT_MP4_ERR_SDK;
  }

  const bool gpu = config.video_backend == MPT_MP4_VIDEO_GPU;
  if (gpu != mpt::gx::renderer_draws()) {
    setError(error, gpu ? "this module was built with the null GX backend" : "this module was built with a GPU GX backend");
    return MPT_MP4_ERR_ARGUMENT;
  }
  mpt::gx::RendererConfig rc;
  rc.internal_scale = std::max<std::uint32_t>(1, config.internal_scale);
  rc.cache_dir = game->cache_dir;
  game->renderer = mpt::gx::create_renderer(rc, why);
  if (!game->renderer) {
    setError(error, "renderer: " + why);
    return MPT_MP4_ERR_SDK;
  }
  game->boot_ids = mpt::gx::object_id_counters();

  std::optional<mpt::SaveMedia> card;
  if (config.save_media != nullptr) {
    if (config.save_media_format != mpt::gc::Sdk::card_image_format_version()) {
      setError(error, "save media: unsupported card image format");
      return MPT_MP4_ERR_ARGUMENT;
    }
    mpt::SaveMedia media;
    media.format_version = config.save_media_format;
    media.data.resize(config.save_media_size);
    std::memcpy(media.data.data(), config.save_media, config.save_media_size);
    card = std::move(media);
  }
  if ((config.flags & MPT_MP4_BOOT_DEBUG_COPY_TRAP) != 0) {
    mpt::mp4::copy_trap_enable();
    game->copy_trap = true;
  }
  if (!boot(*game, std::move(card), why)) {
    if (game->copy_trap) mpt::mp4::copy_trap_disable();
    mpt_mp4_dol_detach();
    setError(error, why);
    return why.rfind("disc:", 0) == 0 ? MPT_MP4_ERR_DISC : MPT_MP4_ERR_SDK;
  }
  g_game = game.release();
  *out = g_game;
  return MPT_MP4_OK;
}

void destroy(MptMp4Game* game) {
  if (game == nullptr || game != g_game) return;
  if (game->copy_trap) mpt::mp4::copy_trap_disable();
  game->sdk.reset();
  game->renderer.reset();
  mpt_mp4_dol_detach();
  delete game;
  g_game = nullptr;
}

std::int16_t foldStick(std::int16_t v) { return v == -32768 ? static_cast<std::int16_t>(-32767) : v; }

void setFault(MptMp4Game& game, Status status, std::string message) {
  game.status = status;
  game.fault = std::move(message);
}

// OSResetSystem: reboot on the same card. Deterministic: the new SDK starts from the same seed.
void softReset(MptMp4Game& game) {
  mpt::SaveMedia card = game.sdk->saveMedia();
  game.frame_base += game.sdk->frame();
  game.generation_base += game.sdk->saveMediaGeneration();
  ++game.resets;
  std::string why;
  if (!boot(game, std::move(card), why)) setFault(game, Status::faulted, "soft reset: " + why);
}

MptMp4StepResult result(const MptMp4Game& game) {
  switch (game.status) {
    case Status::running: return MPT_MP4_STEP_RUNNING;
    case Status::halted: return MPT_MP4_STEP_HALTED;
    case Status::faulted: break;
  }
  return MPT_MP4_STEP_FAULTED;
}

MptMp4StepResult step(MptMp4Game* game, const MptMp4InputFrame* input) {
  if (game == nullptr || input == nullptr) return MPT_MP4_STEP_FAULTED;
  game->audio.clear();
  if (game->status != Status::running) return result(*game);
  if (input->frame < game->frame_base) {
    setFault(*game, Status::faulted, "step: input frame is before the current frame");
    return result(*game);
  }
  mpt::InputFrame frame;
  std::memcpy(&frame, input, sizeof frame);
  frame.frame = input->frame - game->frame_base;
  for (auto& pad : frame.pads) {
    pad.stick_x = foldStick(pad.stick_x);
    pad.stick_y = foldStick(pad.stick_y);
    pad.c_stick_x = foldStick(pad.c_stick_x);
    pad.c_stick_y = foldStick(pad.c_stick_y);
  }
  game->renderer->begin_frame();
  if (game->copy_trap) mpt::mp4::copy_trap_arm();
  const mpt::gc::StepStatus status = game->sdk->stepFrame(frame);
  std::string violation;
  if (game->copy_trap) {
    mpt::mp4::copy_trap_disarm();
    violation = mpt::mp4::copy_trap_take_violation();
  }
  game->renderer->end_frame(&game->image);
  game->audio = game->sdk->takeAudio();
  if (!violation.empty()) {
    setFault(*game, Status::faulted, violation);
  } else if (status == mpt::gc::StepStatus::reset_requested) {
    softReset(*game);
  } else if (status == mpt::gc::StepStatus::halted) {
    setFault(*game, Status::halted, "the game's main loop returned");
  } else if (status == mpt::gc::StepStatus::faulted) {
    setFault(*game, Status::faulted, game->sdk->fault());
  }
  return result(*game);
}

std::uint64_t frame(const MptMp4Game* game) { return game ? game->frame_base + game->sdk->frame() : 0; }
const char* fault(const MptMp4Game* game) { return game ? game->fault.c_str() : ""; }

void frameRate(const MptMp4Game* game, MptMp4FrameRate* out) {
  if (game == nullptr || out == nullptr) return;
  const mpt::FrameRate fr = game->sdk->frameRate();
  out->numerator = fr.numerator;
  out->denominator = fr.denominator;
}

void hashBytes(void* user, const void* data, std::size_t size) {
  static_cast<mpt::StateHasher*>(user)->update({static_cast<const std::byte*>(data), size});
}

// Determinism inputs and game state only: presentation and debug settings never reach the hash.
std::uint64_t stateHash(const MptMp4Game* game) {
  if (game == nullptr) return 0;
  mpt::StateHasher h;
  h.update_u64(game->gameplay_flags);
  h.update_u64(game->frame_base);
  h.update_u64(game->generation_base);
  h.update_u64(game->resets);
  h.update_u64(static_cast<std::uint64_t>(game->status));
  game->sdk->hashState(h);  // includes the attached sound engine
  mpt_mp4_hash_game(&hashBytes, &h);
  return h.digest();
}

void putU32(std::vector<std::byte>& v, std::uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::byte>((x >> (8 * i)) & 0xFF));
}
void putU64(std::vector<std::byte>& v, std::uint64_t x) {
  putU32(v, static_cast<std::uint32_t>(x));
  putU32(v, static_cast<std::uint32_t>(x >> 32));
}
void putString(std::vector<std::byte>& v, const std::string& s) {
  putU32(v, static_cast<std::uint32_t>(s.size()));
  for (char c : s) v.push_back(static_cast<std::byte>(c));
}

struct Reader {
  const std::uint8_t* p;
  std::size_t left;
  bool ok = true;
  std::uint32_t u32() {
    if (left < 4) return ok = false, 0;
    const std::uint32_t x = std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
                            (std::uint32_t{p[3]} << 24);
    p += 4;
    left -= 4;
    return x;
  }
  std::uint64_t u64() {
    const std::uint64_t lo = u32();
    return lo | (std::uint64_t{u32()} << 32);
  }
  std::string str() {
    const std::uint32_t n = u32();
    if (!ok || left < n) return ok = false, std::string();
    std::string s(reinterpret_cast<const char*>(p), n);
    p += n;
    left -= n;
    return s;
  }
};

// [SDK state: arena and registered regions, including the card and its generation]
// [trailer] [trailer size: u32] [format: u32] [magic: u32]
std::vector<std::byte> buildState(const MptMp4Game& game) {
  std::vector<std::byte> state = game.sdk->saveState();
  const std::size_t start = state.size();
  putString(state, MPT_MP4_BUILD_ID);
  putU32(state, game.gameplay_flags);
  putU64(state, game.frame_base);
  putU64(state, game.generation_base);
  putU32(state, game.resets);
  putU32(state, static_cast<std::uint32_t>(game.status));
  putString(state, game.fault);
  const mpt::gx::ObjectIdCounters ids = mpt::gx::object_id_counters();
  putU32(state, ids.next_tex_obj_id);
  putU32(state, ids.next_tlut_obj_id);
  putU32(state, static_cast<std::uint32_t>(state.size() - start));
  putU32(state, kStateFormat);
  putU32(state, kStateMagic);
  return state;
}

MptMp4Status saveState(const MptMp4Game* game, std::uint8_t* buffer, std::size_t capacity, std::size_t* size) {
  if (game == nullptr || size == nullptr) return MPT_MP4_ERR_ARGUMENT;
  const std::vector<std::byte> state = buildState(*game);
  *size = state.size();
  if (buffer == nullptr) return MPT_MP4_OK;
  if (capacity < state.size()) return MPT_MP4_ERR_BUFFER;
  std::memcpy(buffer, state.data(), state.size());
  return MPT_MP4_OK;
}

MptMp4Status loadState(MptMp4Game* game, const std::uint8_t* data, std::size_t size, MptMp4Error* error) {
  if (game == nullptr || data == nullptr || size < 12) {
    setError(error, "load_state: no state");
    return MPT_MP4_ERR_ARGUMENT;
  }
  Reader end{data + size - 12, 12};
  const std::uint32_t trailer = end.u32();
  const std::uint32_t format = end.u32();
  const std::uint32_t magic = end.u32();
  if (magic != kStateMagic || format != kStateFormat || trailer > size - 12) {
    setError(error, "load_state: not an MP4 module state of this layout version");
    return MPT_MP4_ERR_STATE;
  }
  const std::size_t sdk_size = size - 12 - trailer;
  Reader r{data + sdk_size, trailer};
  const std::string build = r.str();
  const std::uint32_t flags = r.u32();
  const std::uint64_t frame_base = r.u64();
  const std::uint64_t generation_base = r.u64();
  const std::uint32_t resets = r.u32();
  const std::uint32_t status = r.u32();
  std::string fault_text = r.str();
  const std::uint32_t tex_ids = r.u32();
  const std::uint32_t tlut_ids = r.u32();
  if (!r.ok || status > static_cast<std::uint32_t>(Status::faulted)) {
    setError(error, "load_state: damaged trailer");
    return MPT_MP4_ERR_STATE;
  }
  if (build != MPT_MP4_BUILD_ID) {
    setError(error, "load_state: the state is from build " + build + ", this module is " MPT_MP4_BUILD_ID);
    return MPT_MP4_ERR_STATE;
  }
  mpt::Result result = game->sdk->loadState({reinterpret_cast<const std::byte*>(data), sdk_size});
  if (!result) {
    setError(error, "load_state: " + result.error());
    return MPT_MP4_ERR_STATE;
  }
  game->gameplay_flags = flags;
  game->frame_base = frame_base;
  game->generation_base = generation_base;
  game->resets = resets;
  game->status = static_cast<Status>(status);
  game->fault = std::move(fault_text);
  mpt::gx::set_object_id_counters({tex_ids, tlut_ids});
  game->renderer->reset_for_load_state();
  if (game->copy_trap) mpt::mp4::copy_trap_forget();
  game->image = mpt::gx::FrameImage{};
  game->audio.clear();
  return MPT_MP4_OK;
}

std::uint32_t stateFormat() { return kStateFormat; }

std::uint64_t saveMediaGeneration(const MptMp4Game* game) {
  return game ? game->generation_base + game->sdk->saveMediaGeneration() : 0;
}

MptMp4Status saveMedia(const MptMp4Game* game, std::uint8_t* buffer, std::size_t capacity, std::size_t* size,
                       std::uint32_t* format) {
  if (game == nullptr || size == nullptr) return MPT_MP4_ERR_ARGUMENT;
  const mpt::SaveMedia media = game->sdk->saveMedia();
  *size = media.data.size();
  if (format != nullptr) *format = media.format_version;
  if (buffer == nullptr) return MPT_MP4_OK;
  if (capacity < media.data.size()) return MPT_MP4_ERR_BUFFER;
  std::memcpy(buffer, media.data.data(), media.data.size());
  return MPT_MP4_OK;
}

std::uint32_t cardFormat() { return mpt::gc::Sdk::card_image_format_version(); }

void video(const MptMp4Game* game, MptMp4Video* out) {
  if (game == nullptr || out == nullptr || out->struct_size < MPT_MP4_VIDEO_SIZE_V1_0) return;
  const auto& img = game->image;
  const bool have = img.width != 0 && img.height != 0 && img.rgba.size() >= std::size_t{img.width} * img.height * 4;
  out->width = have ? img.width : 0;
  out->height = have ? img.height : 0;
  out->reserved = 0;
  out->pixels = have ? reinterpret_cast<const std::uint32_t*>(img.rgba.data()) : nullptr;
}

void audio(const MptMp4Game* game, MptMp4Audio* out) {
  if (game == nullptr || out == nullptr || out->struct_size < MPT_MP4_AUDIO_SIZE_V1_0) return;
  out->sample_rate = game->sdk->audioRate();
  out->channels = 2;
  out->reserved = 0;
  out->samples = game->audio.empty() ? nullptr : game->audio.data();
  out->frame_count = game->audio.size() / 2;
}

void rumble(const MptMp4Game* game, std::uint32_t out[4]) {
  if (game == nullptr || out == nullptr) return;
  const auto r = game->sdk->rumble();
  for (int i = 0; i < 4; ++i) out[i] = r[static_cast<std::size_t>(i)];
}

void setReport(MptMp4Game* game, MptMp4ReportFn fn, void* user) {
  if (game == nullptr) return;
  game->report = fn;
  game->report_user = user;
}

void party(const MptMp4Game* game, MptMp4Party* out) {
  if (game == nullptr || out == nullptr || out->struct_size < MPT_MP4_PARTY_SIZE_V1_0) return;
  mpt_mp4_read_party(out);
}

MptMp4StepResult statusOf(const MptMp4Game* game) { return game ? result(*game) : MPT_MP4_STEP_FAULTED; }

const MptMp4Api kApi = {
    sizeof(MptMp4Api), MPT_MP4_ABI_VERSION, MPT_MP4_BUILD_ID,
    &create, &destroy, &step, &frame, &fault, &frameRate, &stateHash, &saveState, &loadState, &stateFormat,
    &saveMediaGeneration, &saveMedia, &cardFormat, &video, &audio, &rumble, &setReport, &party, &statusOf,
};

}  // namespace

extern "C" MPT_MP4_EXPORT const MptMp4Api* mpt_mp4_get_api(std::uint32_t abi_version) {
  return abi_version == MPT_MP4_ABI_VERSION ? &kApi : nullptr;
}
