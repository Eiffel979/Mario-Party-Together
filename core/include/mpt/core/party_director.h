#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "mpt/core/content_id.h"
#include "mpt/core/party.h"

namespace mpt {

// Something the Party Director saw change between two consecutive snapshots (ADR 0005).
struct PartyEvent {
  // Values are stable: append only, never reorder.
  enum class Kind : std::uint8_t { phase_changed, turn_started, minigame_started, minigame_ended };

  Kind kind = Kind::phase_changed;
  // inputs.frame of the step after which the change was seen.
  std::uint64_t frame = 0;
  // minigame_started / minigame_ended: the minigame. nullopt for the other kinds.
  std::optional<MinigameId> minigame;
  // The snapshot read after that step.
  PartySnapshot snapshot;

  friend bool operator==(const PartyEvent&, const PartyEvent&) = default;
};

// Party Director v0: a read-only mirror of the running title's party (ADR 0005). It never
// calls back into the adapter. Session feeds it one snapshot per stepped frame, and the
// events are a pure function of that snapshot sequence, so they are deterministic.
//
// Within one frame, events come in this fixed order (ADR 0005 amendment 4):
// minigame_ended, phase_changed, turn_started, minigame_started.
class PartyDirector {
 public:
  // The snapshot read after stepping `frame`. Frames must increase between calls,
  // except across resync().
  void observe(std::uint64_t frame, const PartySnapshot& snapshot);

  // Makes `snapshot` the mirror without emitting events and drops every event whose
  // frame is >= `frame`. Session calls it after boot (frame 0) and after loadState
  // (the state's frame).
  void resync(std::uint64_t frame, const PartySnapshot& snapshot);

  const PartySnapshot& party() const { return party_; }

  // All events so far, in frame order. Invalidated by the next observe() or resync().
  std::span<const PartyEvent> events() const { return events_; }

 private:
  PartySnapshot party_;
  std::vector<PartyEvent> events_;
};

}  // namespace mpt
