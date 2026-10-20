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

  // ADR 0011 (core PR #39): becomes the status() override once that interface lands.
  enum class Health : std::uint8_t { running, halted, faulted };
  Health health() const { return health_; }
  const std::string& healthMessage() const { return health_message_; }

  // Raw party reads for Mp4Adapter (MptMp4Party). False before boot.
  bool readParty(MptMp4Party& out) const;

  // The OSReport text sink (presentation only). Called from inside stepFrame.
  void setReportSink(void (*sink)(void* user, const char* text), void* user);

 private:
  void updateHealth(MptMp4StepResult result);

  const MptMp4Api* api_ = nullptr;
  MptMp4Game* game_ = nullptr;
  Health health_ = Health::running;
  std::string health_message_;
  void (*report_)(void*, const char*) = nullptr;
  void* report_user_ = nullptr;
};

// Registration for the shell (mpt_register_title in runtimes/gc_wii/mp4/host/CMakeLists.txt).
}  // namespace mpt::mp4
