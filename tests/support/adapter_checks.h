#pragma once

// Title-agnostic ITitleAdapter invariants (ADR 0002, ADR 0005), written for reuse by
// the conformance suite (P1-20c). Each check returns an empty string when the invariant
// holds, or a description of the first violation, so callers can use it in any test
// framework: CHECK_MESSAGE(problem.empty(), problem).

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "mpt/core/title_adapter.h"
#include "mpt/core/title_runtime.h"

namespace mpt::test {

inline bool catalog_lists(const std::vector<CatalogEntry>& entries, const ContentId& id) {
  for (const CatalogEntry& entry : entries) {
    if (entry.id == id) return true;
  }
  return false;
}

// Every ID belongs to 'title', has the kind of the list it is in, is unique across the
// catalog, and has a display name.
inline std::string check_catalog(const Catalog& catalog, TitleId title) {
  std::set<std::string> seen;
  const auto check_list = [&](const std::vector<CatalogEntry>& entries, std::string_view kind) -> std::string {
    for (const CatalogEntry& entry : entries) {
      const std::string& text = entry.id.str();
      if (entry.id.title() != title) return text + ": belongs to another title";
      if (entry.id.kind() != kind) return text + ": listed as a " + std::string(kind) + " but has another kind";
      if (entry.display_name.empty()) return text + ": empty display name";
      if (!seen.insert(text).second) return text + ": listed twice";
    }
    return {};
  };
  if (std::string problem = check_list(catalog.boards, "board"); !problem.empty()) return problem;
  if (std::string problem = check_list(catalog.minigames, "minigame"); !problem.empty()) return problem;
  return check_list(catalog.characters, "character");
}

// One snapshot on its own: current_minigame is set exactly when phase == minigame and
// names a catalogued minigame; the board and characters are catalogued; turns are in range.
inline std::string check_snapshot(const PartySnapshot& s, const Catalog& catalog) {
  const bool in_minigame = s.phase == PartyPhase::minigame;
  if (in_minigame && !s.current_minigame) return "phase is minigame but current_minigame is not set";
  if (!in_minigame && s.current_minigame) return "current_minigame is set outside the minigame phase";
  if (s.current_minigame && !catalog_lists(catalog.minigames, *s.current_minigame)) {
    return "current_minigame " + s.current_minigame->str() + " is not in the catalog";
  }
  if (s.board && !catalog_lists(catalog.boards, *s.board)) return "board " + s.board->str() + " is not in the catalog";
  for (const PlayerState& player : s.players) {
    if (player.character && !catalog_lists(catalog.characters, *player.character)) {
      return "character " + player.character->str() + " is not in the catalog";
    }
  }
  if (s.turn < 0 || s.max_turns < 0) return "negative turn or max_turns";
  if (s.max_turns > 0 && s.turn > s.max_turns) return "turn is past max_turns";
  if (s.phase == PartyPhase::none && s.turn != 0) return "turn is set while not in a party";
  return {};
}

// readParty() is a read call (ADR 0002 rule 9): two calls in a row return the same
// snapshot and leave stateHash() unchanged.
inline std::string check_read_party_is_pure(ITitleRuntime& runtime, ITitleAdapter& adapter) {
  const std::uint64_t before = runtime.stateHash();
  const PartySnapshot first = adapter.readParty();
  const PartySnapshot second = adapter.readParty();
  if (!(first == second)) return "two readParty() calls in a row disagree";
  if (runtime.stateHash() != before) return "readParty() changed stateHash()";
  return {};
}

}  // namespace mpt::test
