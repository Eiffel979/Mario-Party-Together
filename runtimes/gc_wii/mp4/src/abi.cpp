// The module side of include/mpt/mp4/game_abi.h: one MptMp4Game owns the SDK (with MusyX attached), the
// GX renderer, the disc and the user's main.dol, and runs the game's main loop as the SDK's root
// coroutine. Host-side state only: nothing here is game state, so this file is compiled without the
// game-data section header.
#include "mpt/mp4/game_abi.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "mpt/core/input.h"
#include "mpt/gc/file_source.h"
#include "mpt/gc/musyx_engine.h"
#include "mpt/gc/sdk.h"
#include "mpt/gx/renderer.h"
#include "copy_trap.h"
#include "mpt_mp4_port.h"
#include "mpt_mp4_reads.h"
#include "mpt_mp4_rel.h"

#ifndef MPT_MP4_BUILD_ID
#error "MPT_MP4_BUILD_ID must be defined by runtimes/gc_wii/mp4/CMakeLists.txt"
#endif

static_assert(sizeof(MptMp4InputFrame) == sizeof(mpt::InputFrame), "MptMp4InputFrame mirrors mpt::InputFrame");
static_assert(sizeof(MptMp4Pad) == sizeof(mpt::PadState), "MptMp4Pad mirrors mpt::PadState");
static_assert(offsetof(MptMp4InputFrame, pads) == offsetof(mpt::InputFrame, pads));
static_assert(offsetof(MptMp4Pad, accel) == offsetof(mpt::PadState, accel));
static_assert(offsetof(MptMp4Pad, pointer_y) == offsetof(mpt::PadState, pointer_y));

struct MptMp4Game {
  std::unique_ptr<mpt::gc::DirectoryFileSource> files;
  std::vector<unsigned char> dol;
  std::unique_ptr<mpt::gc::Sdk> sdk;
  std::unique_ptr<mpt::gx::Renderer> renderer;
  mpt::gx::FrameImage image;
  std::vector<std::int16_t> audio;
  bool copy_trap = false;
  std::string fault;  // the module's own faults (copy trap); the SDK's otherwise
  MptMp4ReportFn report = nullptr;
  void* report_user = nullptr;
};

namespace {

MptMp4Game* g_game = nullptr;

constexpr std::uint32_t kStateMagic = 0x4D503453;  // "MP4S": the module's trailer after the SDK state
constexpr std::uint32_t kStateVersion = 1;

template <typename T>
bool sized(const T* p) {
  return p != nullptr && p->struct_size >= sizeof(std::uint32_t);
}

void setError(MptMp4Error* error, const std::string& message) {
  if (error == nullptr || error->struct_size < sizeof(MptMp4Error)) return;
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

MptMp4Status create(const MptMp4BootConfig* config, MptMp4Game** out, MptMp4Error* error) {
  if (out == nullptr || !sized(config) || config->struct_size < sizeof(MptMp4BootConfig) ||
      config->disc_root == nullptr) {
    setError(error, "create: missing or short MptMp4BootConfig");
    return MPT_MP4_ERR_ARGUMENT;
  }
  *out = nullptr;
  if (config->abi_version != MPT_MP4_ABI_VERSION) {
    setError(error, "create: unsupported ABI version");
    return MPT_MP4_ERR_VERSION;
  }
  if (g_game != nullptr) {
    setError(error, "create: a game already exists in this process");
    return MPT_MP4_ERR_BUSY;
  }
  auto game = std::make_unique<MptMp4Game>();
  const std::filesystem::path root = std::filesystem::u8path(config->disc_root);

  std::string why;
  game->files = mpt::gc::DirectoryFileSource::open(root, why);
  if (!game->files) {
    setError(error, "disc: " + why);
    return MPT_MP4_ERR_DISC;
  }
  if (!readFile(root / "sys" / "main.dol", game->dol)) {
    setError(error, "disc: cannot read sys/main.dol");
    return MPT_MP4_ERR_DISC;
  }
  char dol_error[256] = {};
  if (!mpt_mp4_dol_attach(game->dol.data(), game->dol.size(), dol_error, sizeof dol_error)) {
    setError(error, std::string("disc: ") + dol_error);
    return MPT_MP4_ERR_DISC;
  }
  if (!mpt_mp4_rel_init()) {
    mpt_mp4_dol_detach();
    setError(error, "out of memory for the overlay table");
    return MPT_MP4_ERR_SDK;
  }

  const bool gpu = config->video_backend == MPT_MP4_VIDEO_GPU;
  if (gpu != mpt::gx::renderer_draws()) {
    mpt_mp4_dol_detach();
    setError(error, gpu ? "this module was built with the null GX backend" : "this module was built with a GPU GX backend");
    return MPT_MP4_ERR_ARGUMENT;
  }
  mpt::gx::RendererConfig rc;
  rc.internal_scale = std::max<std::uint32_t>(1, config->internal_scale);
  if (config->cache_dir != nullptr) rc.cache_dir = config->cache_dir;
  game->renderer = mpt::gx::create_renderer(rc, why);
  if (!game->renderer) {
    mpt_mp4_dol_detach();
    setError(error, "renderer: " + why);
    return MPT_MP4_ERR_SDK;
  }

  mpt_mp4_port_config = MptMp4PortConfig{};
  mpt_mp4_port_config.skip_boot_sequence = (config->flags & MPT_MP4_BOOT_SKIP_BOOT_SEQUENCE) != 0;
  mpt_mp4_port_config.unlock_all_minigames = (config->flags & MPT_MP4_BOOT_UNLOCK_ALL_MINIGAMES) != 0;
  mpt_mp4_port_config.unlock_bowsers_gnarly_party = (config->flags & MPT_MP4_BOOT_UNLOCK_BOWSERS_GNARLY_PARTY) != 0;

  mpt::gc::SdkConfig sc;
  sc.seed = config->seed;
  sc.video = mpt::gc::VideoStandard::ntsc;
  sc.files = game->files.get();
  sc.card.game_code = {{'G', 'M', 'P', 'E'}};
  sc.card.maker_code = {{'0', '1'}};
  if (config->save_media != nullptr) {
    if (config->save_media_format != mpt::gc::Sdk::card_image_format_version()) {
      mpt_mp4_dol_detach();
      setError(error, "save media: unsupported card image format");
      return MPT_MP4_ERR_ARGUMENT;
    }
    mpt::SaveMedia media;
    media.format_version = config->save_media_format;
    media.data.resize(config->save_media_size);
    std::memcpy(media.data.data(), config->save_media, config->save_media_size);
    sc.card.image = std::move(media);
  }
  // MP4's I/O callback mode is chosen by P1-16's audit of the completion-wait loops (mp4.md §14).
  sc.io_callbacks_before_return = false;
  sc.arena_base = config->arena_base;
  sc.audio_engine = mpt::gc::MusyxEngine::engine();
  sc.audio_output = config->audio_output != 0;
  sc.state_regions = mpt::gc::MusyxEngine::stateRegions();
  for (auto r : gameStateRegions()) sc.state_regions.push_back(r);

  mpt::gc::MusyxEngine::reset();
  mpt::Result r = mpt::gc::Sdk::create(sc, &gameMain, nullptr, game->sdk);
  if (!r) {
    mpt_mp4_dol_detach();
    setError(error, "sdk: " + r.error());
    return MPT_MP4_ERR_SDK;
  }
  game->sdk->setReportSink([g = game.get()](const char* text) {
    if (g->report != nullptr) g->report(g->report_user, text);
  });
  if ((config->flags & MPT_MP4_BOOT_DEBUG_COPY_TRAP) != 0) {
    mpt::mp4::copy_trap_enable();
    game->copy_trap = true;
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

MptMp4StepResult step(MptMp4Game* game, const MptMp4InputFrame* input) {
  if (game == nullptr || input == nullptr) return MPT_MP4_STEP_FAULTED;
  mpt::InputFrame frame;
  std::memcpy(&frame, input, sizeof frame);
  if (!game->fault.empty()) return MPT_MP4_STEP_FAULTED;
  game->renderer->begin_frame();
  if (game->copy_trap) mpt::mp4::copy_trap_arm();
  const mpt::gc::StepStatus status = game->sdk->stepFrame(frame);
  if (game->copy_trap) {
    mpt::mp4::copy_trap_disarm();
    game->fault = mpt::mp4::copy_trap_take_violation();
  }
  game->renderer->end_frame(&game->image);
  if (!game->fault.empty()) return MPT_MP4_STEP_FAULTED;
  switch (status) {
    case mpt::gc::StepStatus::running: return MPT_MP4_STEP_RUNNING;
    case mpt::gc::StepStatus::halted: return MPT_MP4_STEP_HALTED;
    case mpt::gc::StepStatus::reset_requested: return MPT_MP4_STEP_RESET_REQUESTED;
    case mpt::gc::StepStatus::faulted: break;
  }
  return MPT_MP4_STEP_FAULTED;
}

std::uint64_t frame(const MptMp4Game* game) { return game ? game->sdk->frame() : 0; }
const char* fault(const MptMp4Game* game) {
  if (game == nullptr) return "";
  return game->fault.empty() ? game->sdk->fault().c_str() : game->fault.c_str();
}

void frameRate(const MptMp4Game* game, MptMp4FrameRate* out) {
  if (game == nullptr || out == nullptr) return;
  const mpt::FrameRate fr = game->sdk->frameRate();
  out->numerator = fr.numerator;
  out->denominator = fr.denominator;
}

void hashBytes(void* user, const void* data, std::size_t size) {
  static_cast<mpt::StateHasher*>(user)->update({static_cast<const std::byte*>(data), size});
}

std::uint64_t stateHash(const MptMp4Game* game) {
  if (game == nullptr) return 0;
  mpt::StateHasher h;
  game->sdk->hashState(h);  // includes the attached sound engine
  mpt_mp4_hash_game(&hashBytes, &h);
  return h.digest();
}

void putU32(std::vector<std::byte>& v, std::uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::byte>((x >> (8 * i)) & 0xFF));
}
std::uint32_t getU32(const std::uint8_t* p) {
  return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
}

// The SDK's state (arena and registered regions) followed by the GX object ID counters and a trailer.
std::vector<std::byte> buildState(const MptMp4Game& game) {
  std::vector<std::byte> state = game.sdk->saveState();
  const mpt::gx::ObjectIdCounters ids = mpt::gx::object_id_counters();
  putU32(state, ids.next_tex_obj_id);
  putU32(state, ids.next_tlut_obj_id);
  putU32(state, kStateVersion);
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
  if (game == nullptr || data == nullptr || size < 16) {
    setError(error, "load_state: no state");
    return MPT_MP4_ERR_ARGUMENT;
  }
  const std::uint8_t* tail = data + size - 16;
  if (getU32(tail + 12) != kStateMagic || getU32(tail + 8) != kStateVersion) {
    setError(error, "load_state: not an MP4 module state of this version");
    return MPT_MP4_ERR_STATE;
  }
  mpt::Result r = game->sdk->loadState({reinterpret_cast<const std::byte*>(data), size - 16});
  if (!r) {
    setError(error, "load_state: " + r.error());
    return MPT_MP4_ERR_STATE;
  }
  mpt::gx::set_object_id_counters({getU32(tail), getU32(tail + 4)});
  if (game->copy_trap) mpt::mp4::copy_trap_forget();
  game->renderer->reset_for_load_state();
  return MPT_MP4_OK;
}

std::uint64_t saveMediaGeneration(const MptMp4Game* game) { return game ? game->sdk->saveMediaGeneration() : 0; }

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

void video(MptMp4Game* game, MptMp4Video* out) {
  if (game == nullptr || !sized(out) || out->struct_size < sizeof(MptMp4Video)) return;
  const auto& img = game->image;
  const bool have = img.width != 0 && img.height != 0 && img.rgba.size() >= std::size_t{img.width} * img.height * 4;
  out->width = have ? img.width : 0;
  out->height = have ? img.height : 0;
  out->reserved = 0;
  out->pixels = have ? reinterpret_cast<const std::uint32_t*>(img.rgba.data()) : nullptr;
}

void audio(MptMp4Game* game, MptMp4Audio* out) {
  if (game == nullptr || !sized(out) || out->struct_size < sizeof(MptMp4Audio)) return;
  game->audio = game->sdk->takeAudio();
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
  if (game == nullptr || !sized(out) || out->struct_size < sizeof(MptMp4Party)) return;
  mpt_mp4_read_party(out);
}

const MptMp4Api kApi = {
    sizeof(MptMp4Api), MPT_MP4_ABI_VERSION, MPT_MP4_BUILD_ID,
    &create, &destroy, &step, &frame, &fault, &frameRate, &stateHash, &saveState, &loadState,
    &saveMediaGeneration, &saveMedia, &cardFormat, &video, &audio, &rumble, &setReport, &party,
};

}  // namespace

extern "C" MPT_MP4_EXPORT const MptMp4Api* mpt_mp4_get_api(std::uint32_t abi_version) {
  return abi_version == MPT_MP4_ABI_VERSION ? &kApi : nullptr;
}
