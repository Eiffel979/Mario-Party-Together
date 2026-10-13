#pragma once

// The shell's per-frame step logic, separated from SDL so it runs in headless tests
// (P1-06). One tick steps the session at most once:
//
//   paused or a UI screen has focus  -> nothing is sampled or stepped
//   otherwise                        -> sample inputs for session.frame(), step, record,
//                                       hand over a save write, follow the minigame
//
// Pausing therefore never changes frame numbering or inputs: a replay's hash trail is the
// same with the UI on or off, with or without pauses (ADR 0010 §3.5).

#include <cstdint>
#include <functional>
#include <optional>

#include "mpt/core/content_id.h"
#include "mpt/core/input.h"
#include "mpt/core/replay.h"
#include "mpt/core/result.h"
#include "mpt/core/save_media.h"
#include "mpt/core/session.h"

namespace mpt::shell {

struct FrameLoopHooks {
  // Inputs for `frame` (live input or a replay). nullopt = no more input (end of replay).
  std::function<std::optional<InputFrame>(std::uint64_t frame)> sample;
  // Called with each save write. Leave empty for an ephemeral session (replay, test,
  // netplay guest): nothing is persisted (ADR 0004 rule 6).
  std::function<void(const SaveMedia&)> persist;
  // Called when the Director's current minigame changes (input/ per-minigame profiles).
  std::function<void(const std::optional<MinigameId>&)> minigame_changed;
  // Called after every successful step (replay checks, stats).
  std::function<void(Session&)> after_step;
};

class FrameLoop {
 public:
  enum class Tick { stepped, paused, ui_focus, input_ended, failed };

  // `recorder` may be null. Neither `session` nor `recorder` is owned.
  FrameLoop(Session& session, FrameLoopHooks hooks, ReplayRecorder* recorder = nullptr);

  Tick tick(bool ui_has_focus);

  void pause() { paused_ = true; }
  void resume() { paused_ = false; }
  bool paused() const { return paused_; }

  // Set once tick() has returned Tick::failed.
  const std::optional<Result>& failure() const { return failure_; }
  std::uint64_t frames_stepped() const { return stepped_; }

 private:
  Session& session_;
  FrameLoopHooks hooks_;
  ReplayRecorder* recorder_;
  bool paused_ = false;
  std::optional<Result> failure_;
  std::optional<MinigameId> minigame_;
  std::uint64_t stepped_ = 0;
};

}  // namespace mpt::shell
