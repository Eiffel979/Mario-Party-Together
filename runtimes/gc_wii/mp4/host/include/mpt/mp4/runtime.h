// Mp4Runtime: ITitleRuntime for Mario Party 4 (P1-16). Host side, built with the tree's compiler; it
// reaches the Clang-built game module only through its C ABI (mpt/mp4/game_abi.h, ADR 0008 amendment
// after P1-06).
//
// boot() loads <build_dir>/module/mp4_game.dll (.so) at its fixed base, checks the base, and creates
// the game on <build_dir>/disc with the SDK arena at its fixed address (ADR 0008 section 11). The
// module stays loaded for the rest of the process: it holds one game at a time, and boot() replaces it.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "mpt/core/title_runtime.h"
#include "mpt/mp4/game_abi.h"

namespace mpt::mp4 {

class Mp4Runtime final : public ITitleRuntime {
 public:
  // The Phase 1 gameplay boot flags (ADR 0008 ABI review, change 3): fixed, never user settings.
  static constexpr std::uint32_t kGameplayFlags = MPT_MP4_PHASE1_GAMEPLAY_FLAGS;

  Mp4Runtime();
  ~Mp4Runtime() override;
  Mp4Runtime(const Mp4Runtime&) = delete;
  Mp4Runtime& operator=(const Mp4Runtime&) = delete;

  TitleId id() const override { return TitleId::mp4; }
  Result boot(const VerifiedDump& dump, const RuntimeConfig& config) override;
  void stepFrame(const InputFrame& inputs) override;
  FrameOutput video() override;
  AudioChunk audio() override;
  SaveState saveState() override;
  Result loadState(const SaveState& state) override;
  std::uint64_t stateHash() override;
  FrameRate frameRate() const override;
  std::uint64_t saveMediaGeneration() const override;
  SaveMedia saveMedia() const override;

  // ADR 0011: the module's step result and fault text. Part of the save state (the module saves it).
  RuntimeHealth status() const override { return health_; }

  // Raw party reads for Mp4Adapter (MptMp4Party). False before boot.
  bool readParty(MptMp4Party& out) const;

  // Test only: boot the next game with MPT_MP4_BOOT_DEBUG_HOOKS, which debugPerturb and debugDeepHash
  // need (PR #48).
  // Not a determinism input and never in a save state. Call before boot().
  void setDebugHooks(bool on) { debug_hooks_ = on; }

  // Test only (E4 hash coverage, ABI v1.2 debug_perturb): flips one byte of a memory class
  // (MptMp4PerturbTarget); the same call again restores it. Between frames only. False if the
  // module is older than v1.2, the game was booted without debug hooks, or the class has no byte
  // right now.
  bool debugPerturb(std::uint32_t target);

  // Test only (ABI v1.3 debug_deep_hash): the deep hash, P1-16's raw-memory digest (ADR 0008
  // section 9). Same build and platform only; never a replay or netplay hash. nullopt if the module
  // is older than v1.3 or the game was booted without debug hooks.
  std::optional<std::uint64_t> debugDeepHash() const;

  // Test only (ABI v1.3 debug_hash_items): state_hash's parts with their digests, in hash order, as
  // "name size digest" lines, for diffing two platforms (E3c). Empty under the same conditions as
  // debugDeepHash.
  std::string debugHashItems() const;

  // The OSReport text sink (presentation only). Called from inside stepFrame.
  void setReportSink(void (*sink)(void* user, const char* text), void* user);

 private:
  void updateHealth(MptMp4StepResult result);

  const MptMp4Api* api_ = nullptr;
  bool holds_module_ = false;
  bool debug_hooks_ = false;  // setDebugHooks  // one hold on the loaded module (module_loader.h)
  MptMp4Game* game_ = nullptr;
  RuntimeHealth health_{};
  void (*report_)(void*, const char*) = nullptr;
  void* report_user_ = nullptr;
};

// Registration for the shell (mpt_register_title in runtimes/gc_wii/mp4/host/CMakeLists.txt).
}  // namespace mpt::mp4
