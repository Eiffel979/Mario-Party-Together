// MusyX on the SDK's AI ticks (E10, ADR 0007 §4, ADR 0009 amendment after P1-13b,
// merge conditions (b) and (d)). Synthetic sound data only (synthetic_sound.h).
//
// A synthetic "board": it sets SND_HOOKS on an arena heap, inits MusyX, pushes the
// two groups, plays a looping song, polls a MIDI controller value and the song's
// validity every frame, and starts SFX that it waits on in a non-yielding busy-wait
// (MP4's SNDGRP_WAIT), as MP4's msm and board code do.
#include <cstring>

#include "harness.h"
#include "mpt/gc/musyx_engine.h"
#include "mpt_gc/os.h"
#include "mpt_gc/vi.h"
#include "musyx/musyx.h"
#include "musyx/pc.h"
#include "synthetic_sound.h"

using namespace mpt;
using namespace mpt::gc;
using namespace mpt::gc::test;
namespace data = mpt::gc::test::musyxdata;

namespace {

struct Board {
  std::uint32_t seq;
  std::uint32_t init_result;
  std::uint32_t pushed;
  std::uint32_t ctrl_trail;  // every polled controller value, folded
  std::uint32_t ctrl_last;
  std::uint32_t ctrl_changes;
  std::uint32_t seq_valid_frames;
  std::uint32_t waits;
  std::uint32_t waited_ticks;
  std::uint32_t wait_timeouts;
  std::uint32_t fx_started;
};
Board* g_board = nullptr;
const data::Data* g_data = nullptr;

void* hookMalloc(size_t n) { return OSAlloc(static_cast<u32>(n)); }
void hookFree(void* p) {
  if (p) OSFree(p);
}

void* toArena(const std::vector<std::uint8_t>& v) {
  void* p = OSAlloc(static_cast<u32>(v.size()));
  std::memcpy(p, v.data(), v.size());
  return p;
}

void boardMain(void*) {
  Board& b = *static_cast<Board*>(OSAllocFromArenaLo(sizeof(Board), 32));
  std::memset(&b, 0, sizeof b);
  g_board = &b;
  void* lo = OSInitAlloc(OSGetArenaLo(), OSGetArenaHi(), 1);
  OSSetCurrentHeap(OSCreateHeap(lo, OSGetArenaHi()));

  static SND_HOOKS hooks = {&hookMalloc, &hookFree};
  sndSetHooks(&hooks);
  b.init_result = static_cast<std::uint32_t>(sndInit(32, 16, 16, 1, 0, 0x100000));
  void* project = toArena(g_data->project);
  void* pool = toArena(g_data->pool);
  void* samples = toArena(g_data->samples);
  void* sdir_sfx = toArena(g_data->sdir_sfx);
  void* sdir_song = toArena(g_data->sdir_song);
  void* song = toArena(g_data->song);
  b.pushed = (sndPushGroup(project, data::kGroupSfx, samples, sdir_sfx, pool) ? 1u : 0u) |
             (sndPushGroup(project, data::kGroupSong, samples, sdir_song, pool) ? 2u : 0u);
  b.seq = sndSeqPlay(data::kGroupSong, data::kSongId, song, nullptr);

  for (std::uint32_t frame = 0;; ++frame) {
    const std::uint32_t c = sndSeqGetMidiCtrl(b.seq, 0, data::kController);
    b.ctrl_trail = b.ctrl_trail * 131u + c;
    if (c != b.ctrl_last) ++b.ctrl_changes;
    b.ctrl_last = c;
    if (sndSeqGetValid(b.seq)) ++b.seq_valid_frames;
    if (frame % 30 == 7) {
      // SNDGRP_WAIT: start a sound, then spin on the clock until it has stopped.
      const SND_VOICEID vid = sndFXStart(data::kFxOneShot, 127, 64);
      if (vid != SND_ID_ERROR) ++b.fx_started;
      const OSTick start = OSGetTick();
      bool timeout = false;
      while (sndFXCheck(vid) != SND_ID_ERROR) {
        if (OSGetTick() - start > static_cast<OSTick>(OSMillisecondsToTicks(500))) {
          timeout = true;
          break;
        }
      }
      ++b.waits;
      b.waited_ticks += OSGetTick() - start;
      if (timeout) ++b.wait_timeouts;
    }
    if (frame % 45 == 20 && sndFXStart(data::kFxTimed, 100, 32) != SND_ID_ERROR) ++b.fx_started;
    VIWaitForRetrace();
  }
}

SdkConfig musyxConfig(bool output) {
  MusyxEngine::reset();
  SdkConfig cfg;
  cfg.seed = 11;
  cfg.audio_engine = MusyxEngine::engine();
  cfg.audio_output = output;
  for (auto r : MusyxEngine::stateRegions()) cfg.state_regions.push_back(r);
  return cfg;
}

struct Run {
  std::vector<std::uint64_t> hashes;
  std::vector<std::int16_t> pcm;
  Board last{};
  std::uint64_t audio_frames = 0;
};

void stepRun(Sdk& sdk, Run& run, int frames) {
  for (int i = 0; i < frames; ++i) {
    REQUIRE(step(sdk) == StepStatus::running);
    StateHasher h;
    sdk.hashState(h);  // includes the MusyX manifest through the engine hook
    h.update_u64(g_board->ctrl_trail);
    h.update_u64(g_board->waited_ticks);
    h.update_u64(g_board->seq_valid_frames);
    run.hashes.push_back(h.digest());
    auto pcm = sdk.takeAudio();
    run.pcm.insert(run.pcm.end(), pcm.begin(), pcm.end());
  }
  run.last = *g_board;  // copied out: the arena goes away with the Sdk
  run.audio_frames = sdk.audioFrames();
}

Run runBoard(bool output, int frames) {
  const data::Data d = data::make();
  g_data = &d;
  Run run;
  std::unique_ptr<Sdk> sdk;
  const Result r = Sdk::create(musyxConfig(output), &boardMain, nullptr, sdk);
  REQUIRE_MESSAGE(r.ok(), r.error());
  stepRun(*sdk, run, frames);
  INFO(sdk->fault());
  return run;
}

std::uint64_t hashPcm(const std::vector<std::int16_t>& pcm) {
  StateHasher h;
  for (std::int16_t s : pcm) h.update_u64(static_cast<std::uint16_t>(s));
  return h.digest();
}

}  // namespace

TEST_CASE("MusyX boots on the SDK and plays synthetic groups, a song and SFX") {
  const Run run = runBoard(true, 240);
  CHECK(run.last.init_result == 0);
  CHECK(run.last.pushed == 3);
  REQUIRE(run.last.seq != SND_SEQ_ERROR_ID);
  CHECK(run.last.seq_valid_frames == 240);
  // The sequencer ran: controller 16 followed the song (64, 100, 3, looping).
  CHECK(run.last.ctrl_changes >= 4);
  // SNDGRP_WAIT terminated because AI ticks (and so voices) advance during the busy-wait,
  // after about the one-shot sample's 50 ms.
  CHECK(run.last.waits == 8);
  CHECK(run.last.wait_timeouts == 0);
  const std::uint32_t avg_ms = run.last.waited_ticks / run.last.waits / (OS_TIMER_CLOCK / 1000);
  CHECK(avg_ms >= 45);
  CHECK(avg_ms <= 60);
  CHECK(run.last.fx_started == 8 + 5);
  // Real output carries the mix.
  REQUIRE(run.pcm.size() == run.audio_frames * 2);
  bool loud = false;
  for (std::int16_t s : run.pcm) loud |= (s > 2000 || s < -2000);
  CHECK(loud);
  // Goldens: MusyX's synth runs under strict FP with our math library, so these
  // must match on every platform (the Linux leg runs once CI fetches musyx).
  CHECK(hashPcm(run.pcm) == 5125773035542962029ull);
  CHECK(run.hashes.back() == 3049147310665335572ull);
}

TEST_CASE("null output keeps sequencer and voice state identical to real output (condition d)") {
  const Run real = runBoard(true, 200);
  const Run null = runBoard(false, 200);
  CHECK(null.pcm.empty());
  CHECK_FALSE(real.pcm.empty());
  CHECK(real.audio_frames == null.audio_frames);
  CHECK(real.last.ctrl_trail == null.last.ctrl_trail);
  CHECK(real.last.waited_ticks == null.last.waited_ticks);
  CHECK(real.hashes == null.hashes);
}

TEST_CASE("two runs give the same trail and PCM") {
  const Run a = runBoard(true, 120);
  const Run b = runBoard(true, 120);
  CHECK(a.hashes == b.hashes);
  CHECK(a.pcm == b.pcm);
}

TEST_CASE("MusyX renders only inside stepFrame, on AI ticks (condition b)") {
  const data::Data d = data::make();
  g_data = &d;
  std::unique_ptr<Sdk> sdk;
  REQUIRE(Sdk::create(musyxConfig(false), &boardMain, nullptr, sdk).ok());
  stepN(*sdk, 30);
  const SND_PC_RENDER_INFO before = sndPCGetRenderInfo();
  // Host-side calls between frames do not advance the engine.
  (void)sdk->stateHash();
  (void)sdk->saveState();
  (void)sdk->takeAudio();
  const SND_PC_RENDER_INFO after = sndPCGetRenderInfo();
  CHECK(after.renderedFrames == before.renderedFrames);
  CHECK(after.controlTicks == before.controlTicks);
  // Every rendered frame is one the SDK DMA'd: 160 per AI tick, 5 control ms each.
  stepN(*sdk, 10);
  const SND_PC_RENDER_INFO later = sndPCGetRenderInfo();
  CHECK((later.renderedFrames - after.renderedFrames) % 160 == 0);
  CHECK((later.controlTicks - after.controlTicks) * 32 == later.renderedFrames - after.renderedFrames);
  CHECK(later.renderedFrames <= sdk->audioFrames());
}

TEST_CASE("save states capture MusyX's statics: identical trail and PCM after loading") {
  const data::Data d = data::make();
  g_data = &d;
  std::unique_ptr<Sdk> sdk;
  REQUIRE(Sdk::create(musyxConfig(true), &boardMain, nullptr, sdk).ok());
  Run warm;
  stepRun(*sdk, warm, 52);  // mid-song, with a timed SFX voice running
  const auto state = sdk->saveState();
  const std::uint64_t h = sdk->stateHash();
  Run first;
  stepRun(*sdk, first, 70);
  REQUIRE(sdk->loadState(state).ok());
  CHECK(sdk->stateHash() == h);
  Run second;
  stepRun(*sdk, second, 70);
  CHECK(first.hashes == second.hashes);
  CHECK(first.pcm == second.pcm);
}

TEST_CASE("MusyX state is in the hash (perturbation)") {
  const data::Data d = data::make();
  g_data = &d;
  std::unique_ptr<Sdk> sdk;
  REQUIRE(Sdk::create(musyxConfig(false), &boardMain, nullptr, sdk).ok());
  stepN(*sdk, 40);
  const std::uint64_t base = sdk->stateHash();
  const std::uint8_t old = sndSeqGetMidiCtrl(g_board->seq, 3, 7);
  REQUIRE(sndSeqSetMidiCtrl(g_board->seq, 3, 7, static_cast<std::uint8_t>(old ^ 1)));
  const std::uint64_t changed = sdk->stateHash();
  CHECK(changed != base);
  sndSeqPause(g_board->seq);
  CHECK(sdk->stateHash() != changed);
}

// ---- musyx patch 0001: samples a group names but no directory has ---------------------------------

namespace {

struct PushResults {
  std::uint32_t pushed_with_missing;  // the SFX group naming sample 99 (in no directory)
  std::uint32_t fx_started;           // its present sample 5 plays
  std::uint32_t fx_active;
  std::uint32_t pushed_with_failed_upload;  // the song group while sample 7's upload cannot allocate
  std::uint32_t done;
};
PushResults* g_push = nullptr;
bool g_fail_sample_upload = false;

// Fails only allocations the size of one 320-frame PCM16 sample upload (640 bytes plus the PC
// loader's small header), so the push gets past decoding and fails at the upload itself.
void* failingMalloc(size_t n) {
  if (g_fail_sample_upload && n >= 640 && n <= 640 + 64) return nullptr;
  return OSAlloc(static_cast<u32>(n));
}

void pushMain(void*) {
  PushResults& r = *static_cast<PushResults*>(OSAllocFromArenaLo(sizeof(PushResults), 32));
  std::memset(&r, 0, sizeof r);
  g_push = &r;
  void* lo = OSInitAlloc(OSGetArenaLo(), OSGetArenaHi(), 1);
  OSSetCurrentHeap(OSCreateHeap(lo, OSGetArenaHi()));
  static SND_HOOKS hooks = {&failingMalloc, &hookFree};
  sndSetHooks(&hooks);
  if (sndInit(32, 16, 16, 1, 0, 0x100000) != 0) return;
  void* project = toArena(g_data->project);
  void* pool = toArena(g_data->pool);
  void* samples = toArena(g_data->samples);
  void* sdir_sfx = toArena(g_data->sdir_sfx);
  void* sdir_song = toArena(g_data->sdir_song);
  r.pushed_with_missing = sndPushGroup(project, data::kGroupSfx, samples, sdir_sfx, pool) ? 1u : 0u;
  const SND_VOICEID vid = sndFXStart(data::kFxOneShot, 127, 64);
  r.fx_started = vid != SND_ID_ERROR ? 1u : 0u;
  r.fx_active = sndFXCheck(vid) != SND_ID_ERROR ? 1u : 0u;
  g_fail_sample_upload = true;
  r.pushed_with_failed_upload = sndPushGroup(project, data::kGroupSong, samples, sdir_song, pool) ? 1u : 0u;
  g_fail_sample_upload = false;
  r.done = 1;
  for (;;) VIWaitForRetrace();
}

}  // namespace

TEST_CASE("patch 0001: a group naming a sample no directory has still pushes; a failed upload fails") {
  const data::Data d = data::make(true);
  g_data = &d;
  std::unique_ptr<Sdk> sdk;
  REQUIRE(Sdk::create(musyxConfig(false), &pushMain, nullptr, sdk).ok());
  stepN(*sdk, 2);
  REQUIRE(g_push != nullptr);
  CHECK(g_push->done == 1);
  CHECK(g_push->pushed_with_missing == 1);  // the console's ScanIDList skips sample 99
  CHECK(g_push->fx_started == 1);           // and the group's present sample 5 is resident
  CHECK(g_push->fx_active == 1);
  CHECK(g_push->pushed_with_failed_upload == 0);  // present but not uploadable: still a failure
}
