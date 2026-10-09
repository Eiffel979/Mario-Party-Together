#include "mpt/core/party_director.h"

#include <algorithm>

namespace mpt {

void PartyDirector::observe(std::uint64_t frame, const PartySnapshot& snapshot) {
  if (snapshot == party_) return;  // the common case: nothing changed this frame

  const PartySnapshot& prev = party_;
  const auto emit = [&](PartyEvent::Kind kind, const std::optional<MinigameId>& minigame) {
    events_.push_back(PartyEvent{kind, frame, minigame, snapshot});
  };

  if (prev.current_minigame && prev.current_minigame != snapshot.current_minigame) {
    emit(PartyEvent::Kind::minigame_ended, prev.current_minigame);
  }
  if (prev.phase != snapshot.phase) emit(PartyEvent::Kind::phase_changed, std::nullopt);
  if (snapshot.turn > 0 && snapshot.turn != prev.turn) emit(PartyEvent::Kind::turn_started, std::nullopt);
  if (snapshot.current_minigame && snapshot.current_minigame != prev.current_minigame) {
    emit(PartyEvent::Kind::minigame_started, snapshot.current_minigame);
  }

  party_ = snapshot;
}

void PartyDirector::resync(std::uint64_t frame, const PartySnapshot& snapshot) {
  // Events are in frame order, so the ones to drop are a suffix.
  const auto first_dropped =
      std::find_if(events_.begin(), events_.end(), [frame](const PartyEvent& e) { return e.frame >= frame; });
  events_.erase(first_dropped, events_.end());
  party_ = snapshot;
}

}  // namespace mpt
