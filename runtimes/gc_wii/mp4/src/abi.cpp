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
#include "module_symbols.h"
#include "mpt/core/input.h"
#include "mpt/core/state.h"
#include "mpt/gc/digest.h"
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
#ifndef MPT_MP4_MAIN_RAM_BYTES
#error "MPT_MP4_MAIN_RAM_BYTES comes from runtimes/gc_wii/mp4/layout.cmake"
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
  bool debug_hooks = false;  // MPT_MP4_BOOT_DEBUG_HOOKS: test-only entries allowed (PR #48)
  // debug_perturb's last flip, restored by the next call with the same target (test only).
  std::uint32_t perturb_pending_target = 0;
  std::byte* perturb_pending = nullptr;

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

// The module's own symbol table (module_symbols.h), read once per process at the first create. Host
// side: never saved or hashed itself; it names what the state hash visits.
mpt::mp4::ModuleSymbols g_symbols;
bool g_symbols_loaded = false;

// SdkConfig::pointer_id: a stable ID for a pointer into the module (a function or a module data
// symbol, by name and offset), 0 for anything else.
std::uint64_t modulePointerId(const void* p) { return g_symbols_loaded ? g_symbols.idOf(p) : 0; }

// The game's data ranges and their names, which qualify the symbols in them.
std::vector<mpt::mp4::GameRange> gameRanges() {
  std::vector<mpt::mp4::GameRange> ranges;
  ranges.push_back({"core", reinterpret_cast<std::uintptr_t>(mpt_mp4_game_data_lo()),
                    reinterpret_cast<std::uintptr_t>(mpt_mp4_game_data_hi())});
  for (unsigned i = 0; i < mpt_mp4_rel_count; ++i) {
    const MptMp4RelInfo* rel = mpt_mp4_rel_table[i];
    ranges.push_back({rel->name, reinterpret_cast<std::uintptr_t>(rel->data_lo),
                      reinterpret_cast<std::uintptr_t>(rel->data_hi)});
  }
  return ranges;
}

// Loads the symbol table and checks that the game data ranges hold game state only (ADR 0008
// amendment after P1-16: a host C++ object there is moved out, as byteswap.cpp's was).
bool loadSymbols(std::string& error) {
  if (g_symbols_loaded) return true;
  if (!g_symbols.load(gameRanges(), error)) {
    error = "symbol table: " + error;
    return false;
  }
  if (!g_symbols.hostSymbolsInGameData().empty()) {
    error = "a host C++ object sits in the game data range:";
    for (std::size_t i = 0; i < g_symbols.hostSymbolsInGameData().size() && i < 5; ++i)
      error += " " + g_symbols.hostSymbolsInGameData()[i];
    return false;
  }
  g_symbols_loaded = true;
  return true;
}

constexpr std::uint32_t kStateMagic = 0x4D503453;  // "MP4S": the module's trailer after the SDK state
constexpr std::uint32_t kStateFormat = 3;          // the blob's layout version (SaveState::format_version)

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
  // Party Board's TARGET_PC heaps are four times the console's (malloc.c, 64-bit structures): a build
  // constant (layout.cmake, part of the build ID), and an OSAlloc failure is a fault (PR #44).
  sc.main_ram_bytes = MPT_MP4_MAIN_RAM_BYTES;
  sc.fault_on_alloc_failure = true;
  sc.arena_base = game.arena_base;
  sc.audio_engine = mpt::gc::MusyxEngine::engine();
  sc.audio_output = game.audio_output;
  sc.state_regions = mpt::gc::MusyxEngine::stateRegions();
  // The GX front end's game-visible state (the draw-sync callback): saved with the state, cleared here.
  mpt::gx::reset_game_visible_state();
  sc.state_regions.push_back(mpt::gx::game_visible_state());
  sc.pointer_id = &modulePointerId;  // stable function and module-data IDs in the SDK's manifest
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
  if (!loadSymbols(why)) {
    setError(error, why);
    return MPT_MP4_ERR_SDK;
  }

  const bool gpu = config.video_backend == MPT_MP4_VIDEO_GPU;
  if (gpu != mpt::gx::renderer_draws()) {
    setError(error, gpu ? "this module was built with the null GX backend" : "this module was built with a GPU GX backend");
    return MPT_MP4_ERR_BACKEND;
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
  game->debug_hooks = (config.flags & MPT_MP4_BOOT_DEBUG_HOOKS) != 0;
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
  game->perturb_pending = nullptr;
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

// Raw memory goes through the fast digest; its size goes in too, so moving bytes between ranges
// changes the hash.
void hashRange(mpt::StateHasher& h, std::span<const std::byte> bytes) {
  h.update_u64(bytes.size());
  h.update_u64(mpt::gc::memoryDigest(bytes));
}
void digestBytes(void* user, const void* data, std::size_t size) {
  hashRange(*static_cast<mpt::StateHasher*>(user), {static_cast<const std::byte*>(data), size});
}

// The memory part of MP4's hash manifest (P1-16): what a save state carries, less free heap space.
//  - The game core's and every REL's data range (gameStateRegions).
//  - Main RAM: each allocated OSAlloc block; a block holding a HuMem heap is walked instead and
//    only its allocated HuMem blocks count; main RAM outside the OS heaps counts in full.
//  - ARAM below the ARAlloc top.
// Floats in these ranges are hashed as raw bits: within one build and platform the game's NaN bits
// are as deterministic as everything else (docs/titles/mp4.md §20 on ADR 0008's NaN rule).
void hashMemory(const MptMp4Game& game, mpt::StateHasher& h) {
  h.update_u64(0x4D50'344D'454D'0001ull);  // "MP4MEM", manifest part version 1
  for (const std::span<std::byte>& region : gameStateRegions()) hashRange(h, region);
  std::uint64_t broken = 0;
  game.sdk->forEachHashedRamRange([&](const mpt::gc::Sdk::RamRange& r) {
    h.update_u64(mpt::gc::hashGamePointer(r.bytes.data()));
    if (r.data != nullptr) {
      const int humem = mpt_mp4_hash_humem_heap(r.data, &hashBytes, &digestBytes, &h);
      if (humem == 1) {
        hashRange(h, r.bytes.first(r.bytes.size() - r.data_size));  // the OSAlloc header
        return;
      }
      if (humem < 0) ++broken;  // a corrupt HuMem chain: hash the whole block instead
    }
    hashRange(h, r.bytes);
  });
  h.update_u64(broken);
  hashRange(h, game.sdk->aramInUse());
}

// The module's own fields: determinism inputs and status. Presentation and debug settings never
// reach a hash.
void hashManifestFields(const MptMp4Game& game, mpt::StateHasher& h) {
  h.update_u64(game.gameplay_flags);
  h.update_u64(game.frame_base);
  h.update_u64(game.generation_base);
  h.update_u64(game.resets);
  h.update_u64(static_cast<std::uint64_t>(game.status));
}

// The deep hash (ADR 0008 section 9, amendment after P1-16 hash coverage): P1-16's raw-memory hash.
// Same build, same platform only: debug_deep_hash and the E4 coverage oracle.
std::uint64_t deepHash(const MptMp4Game& game) {
  mpt::StateHasher h;
  hashManifestFields(game, h);
  game.sdk->hashState(h);  // includes the attached sound engine
  mpt_mp4_hash_game(&hashBytes, &h);
  hashMemory(game, h);
  hashRange(h, mpt::gx::game_visible_state());  // the GX front end's game-visible state, raw
  return h.digest();
}

// ---- The cross-platform state hash (ADR 0008 section 8 and its amendment after P1-16) ------------
//
// Untyped memory is digested as 64-bit words, each canonicalized first. Canonicalizing can only merge
// values, never split them:
//  - a word inside the SDK arena becomes a tagged arena offset;
//  - a word inside the module becomes its symbol by name and offset, or a string literal by its text
//    (ModuleSymbols::canonical), because the DLL and the .so lay the module out differently;
//  - the default NaNs hash as one value each (ADR 0008, NaN rule for untyped memory, PR #56): an f32
//    half 0xFFC00000 becomes 0x7FC00000, and an aligned f64 word 0xFFF8000000000000 becomes
//    0x7FF8000000000000, so both f64 default NaNs give the same value. Every other bit pattern stays
//    exact. Merging every NaN bit pattern, as typed fields do (update_f32/update_f64), would merge every
//    s32 from -1 to -8388607 in untyped words, so the untyped rule is never widened: a NaN difference
//    found between platforms becomes a typed-entry candidate for architect (docs/titles/mp4.md 20).
std::uint64_t splitmix(std::uint64_t x) {
  x ^= x >> 30;
  x *= 0xBF58476D1CE4E5B9ull;
  x ^= x >> 27;
  x *= 0x94D049BB133111EBull;
  x ^= x >> 31;
  return x;
}

// How many words each rule changed, for debug_hash_items (a false module-pointer match shows up here).
struct CanonCounts {
  std::uint64_t arena = 0, module_symbol = 0, module_text = 0, module_cxx = 0, module_unknown = 0;
  std::uint64_t nan32 = 0, nan64 = 0;
  // Layout hazards, counted but not changed (the hash is unaffected): a code address at a nonzero
  // offset (most likely an integer pair, which then hashes by a layout-dependent function and offset),
  // and a module address at 4 mod 8, which no rule sees, so it hashes raw.
  std::uint64_t module_code_interior = 0, module_misaligned = 0;
  // Where the walk is (a symbol or a range), and what each C++ or unknown module hit pointed at:
  // diagnostics for names that differ between platforms.
  std::string where;
  std::vector<std::string> notes;
};

struct Canon {
  std::uint64_t arena_lo = 0, arena_size = 0;
  std::uint64_t module_lo = 0, module_size = 0;
  const mpt::mp4::ModuleSymbols* symbols = nullptr;
  CanonCounts* counts = nullptr;  // debug_hash_items only

  static constexpr std::uint64_t kArenaTag = 0xA7E4'0000'0000'0000ull;  // arena offsets are below 2^40
  static constexpr std::uint32_t kNan32 = 0x7FC0'0000u;
  static constexpr std::uint64_t kNan64 = 0x7FF8'0000'0000'0000ull;

  // A superset of the words slow() may change, in three compares: the fixed arena and module bases sit
  // at 0x4000'0000'0000 and 0x5000'0000'0000 (layout.cmake), so their high halves lie in
  // [0x4000, 0x6000); a high half of 0xFFC00000 or 0xFFF80000 (OR 0x00380000 gives 0xFFF80000 for both);
  // a low half of 0xFFC00000. canonFor() checks that the live ranges stay inside that window.
  static bool special(std::uint64_t w) {
    const std::uint32_t lo = static_cast<std::uint32_t>(w), hi = static_cast<std::uint32_t>(w >> 32);
    return (hi - 0x4000u < 0x2000u) | ((hi | 0x0038'0000u) == 0xFFF8'0000u) | (lo == 0xFFC0'0000u);
  }
  std::uint64_t operator()(std::uint64_t w) const { return special(w) ? slow(w) : w; }
  std::uint64_t slow(std::uint64_t w) const {
    if (w - arena_lo < arena_size) {
      if (counts != nullptr) ++counts->arena;
      return kArenaTag | (w - arena_lo);
    }
    if (w - module_lo < module_size) {
      mpt::mp4::PointerClass cls;
      const std::uint64_t v = symbols->canonical(static_cast<std::uintptr_t>(w), &cls);
      if (counts != nullptr) {
        switch (cls) {
          case mpt::mp4::PointerClass::symbol: ++counts->module_symbol; break;
          case mpt::mp4::PointerClass::code_interior:
            ++counts->module_symbol;
            ++counts->module_code_interior;
            counts->notes.push_back("code_interior " + counts->where + " -> " +
                                    symbols->nameAt(static_cast<std::uintptr_t>(w)));
            break;
          case mpt::mp4::PointerClass::text: ++counts->module_text; break;
          case mpt::mp4::PointerClass::cxx:
            ++counts->module_cxx;
            counts->notes.push_back("cxx " + counts->where + " -> " + symbols->nameAt(static_cast<std::uintptr_t>(w)));
            break;
          case mpt::mp4::PointerClass::unknown:
            ++counts->module_unknown;
            counts->notes.push_back("unknown " + counts->where + " -> " +
                                    symbols->nameAt(static_cast<std::uintptr_t>(w)));
            break;
        }
      }
      return v;
    }
    if (w == 0xFFF8'0000'0000'0000ull) {
      if (counts != nullptr) ++counts->nan64;
      return kNan64;
    }
    std::uint32_t lo = static_cast<std::uint32_t>(w), hi = static_cast<std::uint32_t>(w >> 32);
    if (counts != nullptr) counts->nan32 += (lo == 0xFFC0'0000u ? 1u : 0u) + (hi == 0xFFC0'0000u ? 1u : 0u);
    if (lo == 0xFFC0'0000u) lo = kNan32;
    if (hi == 0xFFC0'0000u) hi = kNan32;
    return (std::uint64_t{hi} << 32) | lo;
  }
  // debug_hash_items only: notes each module address at offset 4 mod 8 in [b, b + n), where the
  // canonical words (taken from b) cannot see it. counts->where names the range for the note.
  void noteMisaligned(const std::byte* b, std::size_t n) const {
    if (counts == nullptr) return;
    for (std::size_t i = 4; i + 8 <= n; i += 8) {
      std::uint64_t w;
      std::memcpy(&w, b + i, 8);
      if (w - module_lo >= module_size) continue;
      ++counts->module_misaligned;
      char off[24];
      std::snprintf(off, sizeof off, "+0x%zx", i);
      counts->notes.push_back("misaligned " + counts->where + off + " -> " +
                              symbols->nameAt(static_cast<std::uintptr_t>(w)));
    }
  }
};

Canon canonFor(const MptMp4Game& game) {
  Canon c;
  c.arena_lo = reinterpret_cast<std::uintptr_t>(game.sdk->arenaBase());
  c.arena_size = game.sdk->arenaSize();
  c.module_lo = g_symbols.base();
  c.module_size = g_symbols.end() - g_symbols.base();
  c.symbols = &g_symbols;
  // special()'s window: both ranges must lie in [0x4000'0000'0000, 0x6000'0000'0000).
  const auto inside = [](std::uint64_t lo, std::uint64_t size) {
    return size == 0 || ((lo >> 44) - 4u < 2u && ((lo + size - 1) >> 44) - 4u < 2u);
  };
  // Outside it (only with arena_base 0, which the runtime never passes) a range is left raw: still
  // deterministic on one platform, never a wrong merge.
  if (!inside(c.arena_lo, c.arena_size)) c.arena_size = 0;
  if (!inside(c.module_lo, c.module_size)) c.module_size = 0;
  return c;
}

// A range of untyped memory: its size and its canonical digest.
void hashCanonRange(mpt::StateHasher& h, std::span<const std::byte> bytes, const Canon& c) {
  c.noteMisaligned(bytes.data(), bytes.size());
  h.update_u64(bytes.size());
  h.update_u64(mpt::gc::memoryDigestWords(bytes, c));
}

std::string hex32(std::uint64_t v) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "0x%08llx", static_cast<unsigned long long>(v));
  return buf;
}

struct CanonSink {
  mpt::StateHasher* h;
  const Canon* canon;
};
// Header fields and stack records: whole words canonicalized (a record's entry is a function).
void canonFields(void* user, const void* data, std::size_t size) {
  const auto* sink = static_cast<const CanonSink*>(user);
  const auto* b = static_cast<const unsigned char*>(data);
  std::size_t i = 0;
  for (; i + 8 <= size; i += 8) {
    std::uint64_t w;
    std::memcpy(&w, b + i, 8);
    sink->h->update_u64((*sink->canon)(w));
  }
  if (i < size) sink->h->update({reinterpret_cast<const std::byte*>(b + i), size - i});
}
void canonMemory(void* user, const void* data, std::size_t size) {
  const auto* sink = static_cast<const CanonSink*>(user);
  if (sink->canon->counts != nullptr && g_game != nullptr)
    sink->canon->counts->where = "humem block " + hex32(static_cast<std::uint64_t>(
                                     static_cast<const std::byte*>(data) - g_game->sdk->arenaBase()));
  hashCanonRange(*sink->h, {static_cast<const std::byte*>(data), size}, *sink->canon);
}

// debug_hash_items (ABI v1.3): each part of state_hash with its own digest, in hash order, so two
// platforms' lists can be diffed to the symbol or range that differs. Null in a normal hash.
struct Items {
  MptMp4HashItemFn fn = nullptr;
  void* user = nullptr;
  void operator()(const std::string& name, std::uint64_t size, std::uint64_t digest) const {
    if (fn != nullptr) fn(user, name.c_str(), size, digest);
  }
};

// Globals by symbol: every game data symbol in name order, so section layout does not matter. A
// symbol's bytes run to the next symbol; they are canonicalized as words from the symbol's start and
// trailing zeros are dropped, so alignment padding (always zero in these ranges, which are restored
// from their load-time image) never counts. Static symbols of one name in one range are combined
// order-free, because the two linkers need not order archive members alike.
std::uint64_t hashGameSymbols(const Canon& c, const Items* items) {
  static std::vector<std::byte> scratch;  // host side, reused between calls
  static std::vector<std::uint64_t> out;
  out.clear();
  const std::vector<mpt::mp4::ModuleSymbol>& syms = g_symbols.gameData();
  for (std::size_t i = 0; i < syms.size();) {
    std::uint64_t acc = 0, bytes = 0;
    std::size_t j = i;
    for (; j < syms.size() && syms[j].id == syms[i].id; ++j) {
      const mpt::mp4::ModuleSymbol& s = syms[j];
      if (c.counts != nullptr) c.counts->where = "sym " + s.name;
      scratch.resize(static_cast<std::size_t>(s.gap));
      std::memcpy(scratch.data(), reinterpret_cast<const void*>(s.addr), scratch.size());
      c.noteMisaligned(scratch.data(), scratch.size());
      for (std::size_t k = 0; k + 8 <= scratch.size(); k += 8) {
        std::uint64_t w;
        std::memcpy(&w, scratch.data() + k, 8);
        w = c(w);
        std::memcpy(scratch.data() + k, &w, 8);
      }
      std::size_t n = scratch.size();
      while (n > 0 && scratch[n - 1] == std::byte{0}) --n;
      acc += splitmix(mpt::gc::memoryDigest({scratch.data(), n}) ^ n);
      bytes += n;
    }
    out.push_back(syms[i].id);
    out.push_back(acc);
    out.push_back(j - i);
    if (items != nullptr) (*items)("sym " + syms[i].name, bytes, acc);
    i = j;
  }
  mpt::StateHasher h;
  h.update_u64(out.size());
  h.update_u64(mpt::gc::memoryDigest(std::as_bytes(std::span(out))));
  return h.digest();
}

// Heap blocks and the rest of main RAM, as hashMemory walks them, and ARAM in use; every range is
// canonicalized. Blocks are visited in address order, which for two builds that make the same
// allocations with the same sizes is the same permutation of allocation order; a build whose sizes
// differ already splits on every pointer's arena offset.
std::uint64_t hashMemoryXplat(const MptMp4Game& game, const Canon& c, const Items* items) {
  mpt::StateHasher h;
  h.update_u64(0x4D50'3458'504C'0001ull);  // "MP4XPL", manifest part version 1
  std::uint64_t broken = 0;
  const std::byte* arena = game.sdk->arenaBase();
  game.sdk->forEachHashedRamRange([&](const mpt::gc::Sdk::RamRange& r) {
    mpt::StateHasher part;
    CanonSink sink{&part, &c};
    part.update_u64(mpt::gc::hashGamePointer(r.bytes.data()));
    const char* kind = "ram";
    bool done = false;
    if (c.counts != nullptr)
      c.counts->where = "range " + hex32(static_cast<std::uint64_t>(r.bytes.data() - arena));
    if (r.data != nullptr) {
      kind = "block";
      const int humem = mpt_mp4_hash_humem_heap(r.data, &canonFields, &canonMemory, &sink);
      if (humem == 1) {
        kind = "humem";
        hashCanonRange(part, r.bytes.first(r.bytes.size() - r.data_size), c);  // the OSAlloc header
        done = true;
      } else if (humem < 0) {
        kind = "broken";
        ++broken;
      }
    }
    if (!done) hashCanonRange(part, r.bytes, c);
    const std::uint64_t d = part.digest();
    h.update_u64(d);
    if (items != nullptr)
      (*items)(std::string(kind) + " " + hex32(static_cast<std::uint64_t>(r.bytes.data() - arena)), r.bytes.size(), d);
  });
  h.update_u64(broken);
  mpt::StateHasher aram;
  if (c.counts != nullptr) c.counts->where = "aram";
  hashCanonRange(aram, game.sdk->aramInUse(), c);
  h.update_u64(aram.digest());
  if (items != nullptr) (*items)("aram", game.sdk->aramInUse().size(), aram.digest());
  return h.digest();
}

// state_hash: the digests of its parts, in this order.
std::uint64_t crossPlatformHash(const MptMp4Game& game, const Items* items) {
  CanonCounts counts;
  Canon c = canonFor(game);
  if (items != nullptr) c.counts = &counts;
  mpt::StateHasher h;
  h.update_u64(0x4D50'3448'4153'4801ull);  // "MP4HASH", manifest version 1
  auto part = [&](const char* name, std::uint64_t d) {
    h.update_u64(d);
    if (items != nullptr) (*items)(name, 0, d);
  };
  {
    mpt::StateHasher p;
    hashManifestFields(game, p);
    part("fields", p.digest());
  }
  {
    mpt::StateHasher p;
    game.sdk->hashState(p);  // the SDK and the sound engine by value; module pointers by symbol
    part("sdk", p.digest());
  }
  {
    // The GX front end's game-visible state: a callback (by symbol) and a token, as canonical words.
    mpt::StateHasher p;
    CanonSink sink{&p, &c};
    const std::span<std::byte> gx = mpt::gx::game_visible_state();
    canonFields(&sink, gx.data(), gx.size());
    part("gx", p.digest());
  }
  // GWPlayerCfg, GWPlayer, GWSystem and GWGameStat are not hashed here as raw structs: they hold
  // bitfields, whose layout and padding the MSVC and Itanium ABIs may lay out differently. The symbol
  // pass covers them as globals; the deep hash still hashes them raw (#55 review).
  part("symbols", hashGameSymbols(c, items));
  part("memory", hashMemoryXplat(game, c, items));
  if (items != nullptr) {
    // Not part of the hash: how often each canonical rule fired. A module_symbol count from words that
    // are really integers (an upper half of 0x00005000) or module_unknown hits point at false matches.
    const std::pair<const char*, std::uint64_t> n[] = {
        {"count arena", counts.arena},           {"count module_symbol", counts.module_symbol},
        {"count module_text", counts.module_text}, {"count module_cxx", counts.module_cxx},
        {"count module_unknown", counts.module_unknown}, {"count nan32", counts.nan32},
        {"count nan64", counts.nan64}, {"count module_code_interior", counts.module_code_interior},
        {"count module_misaligned", counts.module_misaligned}};
    for (const auto& [name, value] : n) (*items)(name, value, 0);
    for (const std::string& note : counts.notes) (*items)("note " + note, 0, 0);
  }
  return h.digest();
}

std::uint64_t stateHash(const MptMp4Game* game) {
  if (game == nullptr) return 0;
  return crossPlatformHash(*game, nullptr);
}

bool debugAllowed(const MptMp4Game* game) {
  return game != nullptr && game == g_game && game->debug_hooks && game->sdk != nullptr;
}

// Test only (ABI v1.3): the deep hash, on a game created with MPT_MP4_BOOT_DEBUG_HOOKS.
std::int32_t debugDeepHash(const MptMp4Game* game, std::uint64_t* out) {
  if (!debugAllowed(game) || out == nullptr) return MPT_MP4_ERR_ARGUMENT;
  *out = deepHash(*game);
  return MPT_MP4_OK;
}

// Test only (ABI v1.3): state_hash's parts and their digests.
std::int32_t debugHashItems(const MptMp4Game* game, MptMp4HashItemFn fn, void* user) {
  if (!debugAllowed(game) || fn == nullptr) return MPT_MP4_ERR_ARGUMENT;
  const Items items{fn, user};
  const std::uint64_t total = crossPlatformHash(*game, &items);
  items("state_hash", 0, total);
  return MPT_MP4_OK;
}

// Everything the deep hash digests as memory, in its order: the game data ranges, each RAM range
// (for a HuMem heap: its OSAlloc header and its allocated blocks' data, stacks excluded) and ARAM in
// use. For MPT_MP4_PERTURB_SAMPLE.
void collectSpans(void* user, const void* data, std::size_t size) {
  static_cast<std::vector<std::span<std::byte>>*>(user)->emplace_back(
      static_cast<std::byte*>(const_cast<void*>(data)), size);
}
void ignoreFields(void*, const void*, std::size_t) {}

std::vector<std::span<std::byte>> deepCoverage(const MptMp4Game& game, std::uint32_t cls) {
  std::vector<std::span<std::byte>> spans;
  auto add = [&](std::span<const std::byte> s) {
    if (!s.empty()) spans.emplace_back(const_cast<std::byte*>(s.data()), s.size());
  };
  if (cls == 0 || cls == 3)
    for (const std::span<std::byte>& r : gameStateRegions()) add(r);
  if (cls == 1 || cls == 3) {
    game.sdk->forEachHashedRamRange([&](const mpt::gc::Sdk::RamRange& r) {
      if (r.data != nullptr) {
        std::vector<std::span<std::byte>> blocks;
        if (mpt_mp4_hash_humem_heap(r.data, &ignoreFields, &collectSpans, &blocks) == 1) {
          add(r.bytes.first(r.bytes.size() - r.data_size));
          for (const auto& b : blocks) add(b);
          return;
        }
      }
      add(r.bytes);
    });
  }
  if (cls == 2 || cls == 3) add(game.sdk->aramInUse());
  return spans;
}

// MPT_MP4_PERTURB_SAMPLE | (k << 8): a byte chosen from k alone, in class k % 4 (game data, RAM, ARAM,
// all), uniformly by byte.
std::byte* sampleByte(const MptMp4Game& game, std::uint32_t k) {
  const std::vector<std::span<std::byte>> spans = deepCoverage(game, k % 4);
  std::uint64_t total = 0;
  for (const auto& s : spans) total += s.size();
  if (total == 0) return nullptr;
  std::uint64_t at = splitmix(0x5A4D'504C'0000'0000ull ^ k) % total;
  for (const auto& s : spans) {
    if (at < s.size()) return s.data() + at;
    at -= s.size();
  }
  return nullptr;
}

// Test only (ABI v1.2): the byte debug_perturb flips for `target`, or nullptr.
std::byte* perturbByte(MptMp4Game& game, std::uint32_t target) {
  auto middle = [](std::span<const std::byte> s) -> std::byte* {
    return s.empty() ? nullptr : const_cast<std::byte*>(s.data() + s.size() / 2);
  };
  if ((target & 0xFF) == MPT_MP4_PERTURB_SAMPLE) return sampleByte(game, target >> 8);
  switch (target) {
    case MPT_MP4_PERTURB_GAME_DATA:
      return middle({reinterpret_cast<const std::byte*>(mpt_mp4_game_data_lo()),
                     static_cast<std::size_t>(mpt_mp4_game_data_hi() - mpt_mp4_game_data_lo())});
    case MPT_MP4_PERTURB_HEAP_BLOCK:
    case MPT_MP4_PERTURB_FREE_HEAP: {
      void* data = mpt_mp4_humem_find_block(target == MPT_MP4_PERTURB_HEAP_BLOCK, 64);
      return data != nullptr ? static_cast<std::byte*>(data) + 32 : nullptr;
    }
    case MPT_MP4_PERTURB_STACK:
      return static_cast<std::byte*>(mpt_mp4_humem_find_stack());
    case MPT_MP4_PERTURB_ARAM:
      return middle(game.sdk->aramInUse());
    case MPT_MP4_PERTURB_GLOBAL: {
      std::size_t size = 0;
      return static_cast<std::byte*>(mpt_mp4_registered_global(&size));
    }
    default:
      return nullptr;
  }
}

std::int32_t debugPerturb(MptMp4Game* game, std::uint32_t target) {
  const std::uint32_t cls = target & 0xFF;
  const bool sample = cls == MPT_MP4_PERTURB_SAMPLE;
  if (!debugAllowed(game) || (!sample && (target < MPT_MP4_PERTURB_GAME_DATA || target > MPT_MP4_PERTURB_STACK)))
    return MPT_MP4_ERR_ARGUMENT;
  // A flip of a sampled byte can change the walk that chose it (a heap header), so the second call
  // restores the remembered byte instead of choosing again.
  if (game->perturb_pending_target == target && game->perturb_pending != nullptr) {
    *game->perturb_pending ^= std::byte{0xFF};
    game->perturb_pending = nullptr;
    return MPT_MP4_OK;
  }
  std::byte* b = perturbByte(*game, target);
  if (b == nullptr) return MPT_MP4_ERR_STATE;
  *b ^= std::byte{0xFF};
  game->perturb_pending_target = target;
  game->perturb_pending = b;
  return MPT_MP4_OK;
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
  // The arena layout (PR #44): a state only loads into the same layout.
  putU64(state, game.sdk->mainRam().size());
  putU64(state, game.sdk->aram().size());
  putU64(state, game.sdk->arenaSize());
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
  const std::uint64_t main_ram = r.u64();
  const std::uint64_t aram = r.u64();
  const std::uint64_t arena = r.u64();
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
  if (main_ram != game->sdk->mainRam().size() || aram != game->sdk->aram().size() ||
      arena != game->sdk->arenaSize()) {
    setError(error, "load_state: the state's arena layout (main RAM, ARAM, stacks) differs from this module's");
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
  game->perturb_pending = nullptr;
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

// ABI v1.4: arena use. OSAlloc's allocated blocks are the ranges forEachHashedRamRange reports with
// data (each block's header and data); the HuMem heaps are among them.
std::int32_t memoryUse(const MptMp4Game* game, MptMp4MemoryUse* out) {
  if (game == nullptr || out == nullptr || out->struct_size < MPT_MP4_MEMORY_USE_SIZE_V1_4) return MPT_MP4_ERR_ARGUMENT;
  const std::uint32_t size = out->struct_size;
  *out = MptMp4MemoryUse{};
  out->struct_size = size;
  out->main_ram = game->sdk->mainRam().size();
  std::uint64_t used = 0;
  game->sdk->forEachHashedRamRange([&](const mpt::gc::Sdk::RamRange& r) {
    if (r.data != nullptr) used += r.bytes.size();
  });
  out->osalloc_used = used;
  const int heaps = std::min<int>(mpt_mp4_heap_count(), static_cast<int>(MPT_MP4_MEMORY_HEAPS));
  out->heap_count = static_cast<std::uint32_t>(heaps);
  for (int i = 0; i < heaps; ++i) {
    std::size_t heap_size = 0, heap_used = 0;
    mpt_mp4_heap_use(i, &heap_size, &heap_used);
    out->heap_size[i] = heap_size;
    out->heap_used[i] = heap_used;
  }
  out->aram_used = game->sdk->aramInUse().size();
  return MPT_MP4_OK;
}

const MptMp4Api kApi = {
    sizeof(MptMp4Api), MPT_MP4_ABI_VERSION, MPT_MP4_BUILD_ID,
    &create, &destroy, &step, &frame, &fault, &frameRate, &stateHash, &saveState, &loadState, &stateFormat,
    &saveMediaGeneration, &saveMedia, &cardFormat, &video, &audio, &rumble, &setReport, &party, &statusOf,
    &debugPerturb, &debugDeepHash, &debugHashItems, &memoryUse,
};

}  // namespace

extern "C" MPT_MP4_EXPORT const MptMp4Api* mpt_mp4_get_api(std::uint32_t abi_version) {
  return abi_version == MPT_MP4_ABI_VERSION ? &kApi : nullptr;
}
