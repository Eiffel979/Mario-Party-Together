// Host-side control of the GameCube/Wii SDK replacement layer (ADR 0007, 0008).
//
// A title runtime (for example Mp4Runtime, P1-16) creates one Sdk, gives it the
// game's main() as the root coroutine, and calls stepFrame() once per VI field.
// All game-visible state lives in one arena; saveState() is that arena, and
// stateHash() hashes the SDK's part of the hash manifest by value.
//
// Contract dependencies on core/ (v0 headers): InputFrame/PadState (input.h),
// DeterministicRng (rng.h), StateHasher (state.h), Result (result.h). At core
// contract v1 (P1-03) the card image maps onto mpt::SaveMedia and the field
// rate onto FrameRate; see card_image_format_version() and field_rate().
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mpt/core/input.h"
#include "mpt/core/result.h"
#include "mpt/core/state.h"
#include "mpt/gc/file_source.h"

namespace mpt::gc {

enum class VideoStandard : std::uint8_t {
  ntsc,   // 60000/1001 Hz fields
  pal50,  // 50 Hz fields
};

struct CardConfig {
  bool inserted = true;  // slot A; slot B is always empty
  // Initial image (our format, see card_image_format_version()). nullopt = a
  // blank, freshly formatted card with the stable serial (ADR 0004 rules 3, 7).
  std::optional<std::vector<std::byte>> image;
  std::uint16_t size_mbits = 16;  // 4 (59 blocks), 8, 16 (251 blocks); blank cards only
  std::array<char, 4> game_code{{'G', 'A', 'M', 'E'}};  // the disc ID's game code
  std::array<char, 2> maker_code{{'0', '1'}};
};

struct SdkConfig {
  std::uint64_t seed = 0;  // RuntimeConfig::seed; derives the virtual clock's epoch
  VideoStandard video = VideoStandard::ntsc;
  const IFileSource* files = nullptr;  // the extracted disc; nullptr = an empty disc
  CardConfig card;
  // Run DVD/ARAM/CARD completion callbacks before the call returns, instead of
  // at the next SDK call boundary. A per-title choice (ADR 0008 §3 allows both).
  bool io_callbacks_before_return = false;
  std::uint32_t thread_stack_bytes = 256 * 1024;  // per SDK thread / coroutine
  // Arena placement. 0 = anywhere. Fixed bases for fresh-process save states
  // are P1-16 (ADR 0008 §11); create() fails if this address is unavailable.
  std::uintptr_t arena_base = 0;
};

enum class StepStatus : std::uint8_t {
  running,          // the frame ran; call stepFrame again
  halted,           // the root coroutine returned (main() ended)
  reset_requested,  // the game called OSResetSystem
  faulted,          // a fatal SDK error or OSPanic; see fault()
};

// Fixed SDK constants that are part of the determinism contract.
inline constexpr std::uint32_t kTimerClockHz = 40'500'000;    // bus clock / 4
inline constexpr std::uint32_t kTimeQueryQuantumTicks = 405;  // 10 µs per OSGetTime/OSGetTick (ADR 0008 §2)
inline constexpr std::int64_t kWatchdogTicks = 60ll * kTimerClockHz;  // a frame may not run 60 virtual s
inline constexpr std::uint64_t kStableCardSerial = 0x4D50'5447'4352'4431ull;  // "MPTGCRD1", ADR 0004 rule 7
inline constexpr std::uint32_t kMaxThreads = 32;  // SDK threads incl. the root; game processes use their own

class Sdk {
 public:
  using MainFn = void (*)(void* arg);

  // Only one Sdk may exist at a time: the C API (OS*, DVD*, ...) talks to it.
  static Result create(const SdkConfig& config, MainFn main, void* arg, std::unique_ptr<Sdk>& out);
  ~Sdk();
  Sdk(const Sdk&) = delete;
  Sdk& operator=(const Sdk&) = delete;

  // Runs one VI field. `input.frame` must equal frame(). Frame 0 runs main()
  // from boot to its first retrace wait; every later frame starts with the
  // retrace interrupt (pre-retrace callback, PAD latch, post-retrace callback).
  StepStatus stepFrame(const InputFrame& input);

  std::uint64_t frame() const;
  const std::string& fault() const;

  // The SDK's part of the hash manifest (ADR 0008 §8), by value.
  void hashState(StateHasher& hasher) const;
  std::uint64_t stateHash() const;

  // Phase 1 save state: the whole arena (SDK state, main RAM, ARAM, card, stacks).
  // Valid only for the same build in the same process at the same arena base
  // (ADR 0008 §10-11); loadState rejects anything else. Only between frames.
  std::vector<std::byte> saveState() const;
  Result loadState(std::span<const std::byte> state);

  // Save media (ADR 0004). The generation counts committed card writes.
  std::uint64_t saveMediaGeneration() const;
  std::vector<std::byte> cardImage() const;
  static std::uint32_t card_image_format_version();

  // Field rate as num/den Hz (60000/1001 or 50/1).
  std::pair<std::uint32_t, std::uint32_t> field_rate() const;

  // Host-visible outputs (presentation only, never game input).
  std::array<std::uint32_t, 4> rumble() const;  // PADControlMotor state per port
  void setReportSink(std::function<void(const char*)> sink);  // OSReport text

  // Virtual time without a time-query quantum, for the runtime and tests.
  std::int64_t now() const;
  std::int64_t epoch() const;

  // Arena regions, for tests and the runtime (hashing pointers as offsets).
  std::byte* arenaBase() const;
  std::size_t arenaSize() const;
  std::span<std::byte> mainRam() const;
  std::span<std::byte> aram() const;

  struct Impl;

 private:
  explicit Sdk(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace mpt::gc
