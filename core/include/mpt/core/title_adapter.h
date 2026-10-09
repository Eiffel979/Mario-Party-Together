#pragma once

#include <optional>
#include <string>
#include <vector>

#include "mpt/core/content_id.h"
#include "mpt/core/party.h"
#include "mpt/core/result.h"
#include "mpt/core/ruleset.h"

namespace mpt {

struct CatalogEntry {
  ContentId id;
  std::string display_name{};
};

// Everything a title offers, by stable content ID.
struct Catalog {
  std::vector<CatalogEntry> boards{};
  std::vector<CatalogEntry> minigames{};
  std::vector<CatalogEntry> characters{};
};

// Title-specific knowledge layered on a running ITitleRuntime: what content
// exists, how to apply rules, and how to read and write party state.
//
// Lifetime (ADR 0002 rule 10): an adapter must not outlive the runtime it was
// created with.
//
// Call timing (ADR 0002 rule 9): the mutating calls (applyRuleset, writeParty,
// launchMinigame) may only be made between stepFrame() calls, at a frame the Party
// Director picks deterministically (the same frame on every peer), and must be
// deterministic functions of their arguments and the runtime state. The read calls
// (catalog, readParty, pollMinigameResult) must not change game state.
class ITitleAdapter {
 public:
  virtual ~ITitleAdapter() = default;

  virtual Catalog catalog() = 0;

  // Fails if the ruleset asks for something this title cannot do.
  virtual Result applyRuleset(const ResolvedRuleset& rules) = 0;

  // Called by the Director after every stepFrame() (ADR 0005), so keep it cheap.
  // Fills PartySnapshot::phase and current_minigame as well as the title allows.
  virtual PartySnapshot readParty() = 0;
  virtual void writeParty(const PartySnapshot& party) = 0;

  // Phase 5 (cross-title parties). Adapters may return a failure until then.
  virtual Result launchMinigame(const MinigameId& minigame, const MinigameSetup& setup) = 0;
  virtual std::optional<MinigameResult> pollMinigameResult() = 0;
};

}  // namespace mpt
