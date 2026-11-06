// MP4's host side without a dump or the game module (qa follow-up to #49, so public CI covers it):
// Mp4Adapter's phase mapping and ruleset check on synthetic MptMp4Party values, an unbooted
// Mp4Runtime, and the host's ABI v1.1 check (mpt_mp4_api_has_v1_1) on synthetic function tables.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "mpt/core/content_id.h"
#include "mpt/core/ruleset.h"
#include "mpt/mp4/adapter.h"
#include "mpt/mp4/game_abi.h"
#include "mpt/mp4/runtime.h"

namespace {

constexpr std::uint32_t kMode = MPT_MP4_PARTY_MODE;
constexpr std::uint32_t kSetup = MPT_MP4_PARTY_IN_SETUP;
constexpr std::uint32_t kInProgress = MPT_MP4_PARTY_IN_PROGRESS;
constexpr std::uint32_t kTurnsOver = MPT_MP4_PARTY_TURNS_OVER;
constexpr std::uint32_t kFinal = MPT_MP4_PARTY_RESULTS_FINAL;

// A party read as the module reports it: the flags and the running overlay's name.
MptMp4Party party(std::uint32_t flags, const char* overlay = "") {
  MptMp4Party p{};
  p.struct_size = sizeof p;
  p.overlay = overlay[0] != 0 ? 0 : -1;
  std::memcpy(p.overlay_name, overlay, std::min(std::strlen(overlay), sizeof p.overlay_name - 1));
  p.flags = flags;
  return p;
}

MptMp4StepResult fakeStatus(const MptMp4Game*) { return MPT_MP4_STEP_RUNNING; }

}  // namespace

TEST_CASE("Mp4Adapter phase_of maps the module's party flags onto PartyPhase") {
  using mpt::PartyPhase;
  using mpt::mp4::phase_of;
  // Outside party mode (boot, title, story, minigame modes) there is no party.
  CHECK(phase_of(party(0)) == PartyPhase::none);
  CHECK(phase_of(party(kInProgress, "w01Dll")) == PartyPhase::none);
  CHECK(phase_of(party(kMode)) == PartyPhase::none);
  CHECK(phase_of(party(kMode | kSetup, "mstoryDll")) == PartyPhase::setup);
  CHECK(phase_of(party(kMode | kInProgress, "w01Dll")) == PartyPhase::board);
  // The minigame overlays are m401Dll..m463Dll; the m3xx overlays are empty placeholders. phase_of is
  // the raw classifier: readParty must still pair the minigame phase with current_minigame (ADR 0005).
  CHECK(phase_of(party(kMode | kInProgress, "m401Dll")) == PartyPhase::minigame);
  CHECK(phase_of(party(kMode | kInProgress, "m463Dll")) == PartyPhase::minigame);
  CHECK(phase_of(party(kMode | kInProgress, "m300Dll")) == PartyPhase::board);
  CHECK(phase_of(party(kMode | kInProgress, "m4")) == PartyPhase::board);
  CHECK(phase_of(party(kMode | kInProgress)) == PartyPhase::board);
  CHECK(phase_of(party(kMode | kInProgress | kTurnsOver, "w01Dll")) == PartyPhase::results);
  CHECK(phase_of(party(kMode | kInProgress | kTurnsOver | kFinal)) == PartyPhase::finished);
  // Later stages win over earlier flags that are still set.
  CHECK(phase_of(party(kMode | kSetup | kInProgress, "w01Dll")) == PartyPhase::board);
  CHECK(phase_of(party(kMode | kSetup | kTurnsOver, "m401Dll")) == PartyPhase::results);
  CHECK(phase_of(party(kMode | kFinal, "m401Dll")) == PartyPhase::finished);
}

TEST_CASE("Mp4Adapter on an unbooted Mp4Runtime: no party, empty catalog, only the empty ruleset") {
  mpt::mp4::Mp4Runtime runtime;  // never booted, so no game module is loaded
  mpt::mp4::Mp4Adapter adapter(runtime);
  const mpt::PartySnapshot snap = adapter.readParty();
  CHECK(snap.phase == mpt::PartyPhase::none);
  CHECK_FALSE(snap.current_minigame.has_value());
  const mpt::Catalog catalog = adapter.catalog();
  CHECK(catalog.boards.empty());
  CHECK(catalog.minigames.empty());
  CHECK(runtime.stateHash() == 0);
  CHECK(runtime.status().status == mpt::RuntimeStatus::running);

  mpt::ResolvedRuleset empty;
  empty.seed = 42;  // always set by the host; never a rule
  CHECK(adapter.applyRuleset(empty).ok());

  const auto rejects = [&](auto&& set) {
    mpt::ResolvedRuleset rules;
    rules.seed = 42;
    set(rules);
    return !adapter.applyRuleset(rules).ok();
  };
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.board = mpt::ContentId::parse("mp4.board.test_board"); }));
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.turn_count = 20; }));
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.starting_coins = 10; }));
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.star_cost = 20; }));
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.starting_stars = 0; }));  // set, even to zero
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.bonus_stars = false; }));
  CHECK(rejects([](mpt::ResolvedRuleset& r) { r.minigame_time_limit_frames = 3600; }));
  CHECK(rejects([](mpt::ResolvedRuleset& r) {
    r.minigame_pool.push_back(*mpt::ContentId::parse("mp4.minigame.test_minigame"));
  }));
}

TEST_CASE("mpt_mp4_api_has_v1_1 accepts only a v1.1 table with status") {
  MptMp4Api api{};
  api.abi_version = MPT_MP4_ABI_VERSION;
  api.struct_size = MPT_MP4_API_SIZE_V1_1;
  api.status = &fakeStatus;
  CHECK(mpt_mp4_api_has_v1_1(&api));
  api.struct_size = MPT_MP4_API_SIZE_V1_2;  // a later minor version still has it
  CHECK(mpt_mp4_api_has_v1_1(&api));

  // A v1.0 table ends before status: whatever lies past struct_size must not count.
  api.struct_size = MPT_MP4_API_SIZE_V1_0;
  CHECK_FALSE(mpt_mp4_api_has_v1_1(&api));
  api.struct_size = MPT_MP4_API_SIZE_V1_1 - 1;
  CHECK_FALSE(mpt_mp4_api_has_v1_1(&api));
  api.struct_size = MPT_MP4_API_SIZE_V1_1;
  api.status = nullptr;  // entries added in a minor version may be NULL
  CHECK_FALSE(mpt_mp4_api_has_v1_1(&api));
  api.status = &fakeStatus;
  api.abi_version = MPT_MP4_ABI_VERSION + 1;  // another major version
  CHECK_FALSE(mpt_mp4_api_has_v1_1(&api));
  CHECK_FALSE(mpt_mp4_api_has_v1_1(nullptr));

  // The appends sit where the size constants say (ABI v1.1, v1.2 and v1.3).
  CHECK(offsetof(MptMp4Api, status) == MPT_MP4_API_SIZE_V1_0);
  CHECK(offsetof(MptMp4Api, debug_perturb) == MPT_MP4_API_SIZE_V1_1);
  CHECK(offsetof(MptMp4Api, debug_deep_hash) == MPT_MP4_API_SIZE_V1_2);
  CHECK(sizeof(MptMp4Api) == MPT_MP4_API_SIZE_V1_3);
}
