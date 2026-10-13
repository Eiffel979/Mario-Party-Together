#include "frame_loop.h"

#include <utility>

namespace mpt::shell {

FrameLoop::FrameLoop(Session& session, FrameLoopHooks hooks, ReplayRecorder* recorder)
    : session_(session), hooks_(std::move(hooks)), recorder_(recorder) {}

FrameLoop::Tick FrameLoop::tick(bool ui_has_focus) {
  if (failure_) return Tick::failed;
  if (ui_has_focus) return Tick::ui_focus;
  if (paused_) return Tick::paused;

  const std::optional<InputFrame> inputs = hooks_.sample ? hooks_.sample(session_.frame()) : std::nullopt;
  if (!inputs) return Tick::input_ended;

  if (Result stepped = session_.step(*inputs); !stepped) {
    failure_ = std::move(stepped);
    return Tick::failed;
  }
  ++stepped_;
  if (recorder_) {
    if (Result recorded = recorder_->record(*inputs, session_); !recorded) {
      failure_ = std::move(recorded);
      return Tick::failed;
    }
  }
  // Always drain the pending image; persist it only for a non-ephemeral session.
  if (std::optional<SaveMedia> written = session_.takeSaveMedia(); written && hooks_.persist) hooks_.persist(*written);

  const std::optional<MinigameId>& current = session_.director().party().current_minigame;
  if (current != minigame_) {
    minigame_ = current;
    if (hooks_.minigame_changed) hooks_.minigame_changed(minigame_);
  }
  if (hooks_.after_step) hooks_.after_step(session_);
  return Tick::stepped;
}

}  // namespace mpt::shell
