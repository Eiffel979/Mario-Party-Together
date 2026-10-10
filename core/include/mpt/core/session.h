#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

#include "mpt/core/dump.h"
#include "mpt/core/input.h"
#include "mpt/core/party_director.h"
#include "mpt/core/result.h"
#include "mpt/core/ruleset.h"
#include "mpt/core/save_media.h"
#include "mpt/core/state.h"
#include "mpt/core/title_adapter.h"
#include "mpt/core/title_registry.h"
#include "mpt/core/title_runtime.h"

namespace mpt {

struct SessionConfig {
  VerifiedDump dump{};
  RuntimeConfig runtime{};
  std::optional<ResolvedRuleset> rules{};  // Phase 1: nullopt or empty
};

// One running title: its runtime, its adapter and the Party Director (ADR 0005). The
// same object runs live, in replays, in tests and (Phase 2) under lockstep netplay.
//
// Session has no clock, no SDL, no filesystem and no threads. The shell owns pacing,
// input sampling, presentation, audio and save persistence. Replay recording is outside
// Session too: the shell tees the same InputFrames to a ReplayWriter (ADR 0006).
//
// Instances (ADR 0002 rule 10, ADR 0005 amendment 6): at most one live Session per
// TitleId per process. The adapter is destroyed before the runtime.
class Session {
 public:
  // Creates the runtime and adapter through the registry, boots, applies config.rules,
  // and takes the first party snapshot as the Director's baseline. Fails if the title is
  // not registered, a Session of the title is still alive, a factory returns null, the
  // runtime reports another id(), or boot() or applyRuleset() fails.
  static std::variant<std::unique_ptr<Session>, Result> start(const TitleRegistry& registry,
                                                              const SessionConfig& config);

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  ~Session();

  // Steps the runtime once, detects a save-media write (ADR 0004 rule 10) and lets the
  // Director observe. Fails without stepping unless inputs.frame == frame().
  Result step(const InputFrame& inputs);

  // Loads a state and resyncs: frame() becomes state.frame, the Director drops events at
  // or after it, and the restored save-media generation becomes the baseline without
  // counting as a write (ADR 0004 rules 9-10). On failure frame() and the Director are
  // unchanged.
  Result loadState(const SaveState& state);

  // The frame number the next step() takes.
  std::uint64_t frame() const { return frame_; }

  // The save image as of the most recent step() that changed the save-media generation,
  // or nullopt if no write happened since the last call. loadState() neither sets nor
  // clears it. Persisting it, or not, is the shell's decision (ADR 0004 rules 5-6).
  std::optional<SaveMedia> takeSaveMedia();

  // For video(), audio(), frameRate(), saveState() and stateHash(). Never call boot(),
  // stepFrame() or loadState() through it: use step() and loadState() above.
  ITitleRuntime& runtime() { return *runtime_; }

  const PartyDirector& director() const { return director_; }

 private:
  // Reserves a TitleId for the lifetime of one Session (ADR 0002 rule 10).
  class TitleLease {
   public:
    static std::optional<TitleLease> acquire(TitleId title);
    TitleLease(TitleLease&& other) noexcept;
    TitleLease& operator=(TitleLease&&) = delete;
    ~TitleLease();

   private:
    explicit TitleLease(TitleId title) : title_(title) {}
    std::optional<TitleId> title_;
  };

  Session(TitleLease lease, std::unique_ptr<ITitleRuntime> runtime, std::unique_ptr<ITitleAdapter> adapter);

  // Members are destroyed in reverse order: director, adapter, runtime, then the lease.
  TitleLease lease_;
  std::unique_ptr<ITitleRuntime> runtime_;
  std::unique_ptr<ITitleAdapter> adapter_;
  PartyDirector director_;
  std::uint64_t frame_ = 0;
  std::optional<SaveMedia> pending_save_media_;
};

}  // namespace mpt
