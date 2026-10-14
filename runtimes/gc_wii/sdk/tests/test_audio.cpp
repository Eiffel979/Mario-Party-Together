// AI DMA and sound-engine stepping (E10, ADR 0007 §4, ADR 0008 §2, §8).
//
// "ToySynth" is a synthetic pull-mode engine with the shape of MusyX's PC synth:
// a sequencer stepped once per 5 ms block, voices that end on their own, a MIDI
// controller value, and game code that polls voice status in a non-yielding
// busy-wait (MP4's SNDGRP_WAIT). Its state lives in a static (a registered
// state region), like MusyX's. Synthetic data only: the "song" is generated here.
#include <array>
#include <cstring>

#include "harness.h"
#include "mpt_gc/ai.h"
#include "mpt_gc/os.h"
#include "mpt_gc/vi.h"

using namespace mpt;
using namespace mpt::gc;
using namespace mpt::gc::test;

namespace {

struct ToyVoice {
  std::uint32_t active;
  std::uint32_t phase;
  std::uint32_t step;
  std::uint32_t remaining;  // frames until the voice ends
  std::int32_t vol;
};

struct ToySynth {
  ToyVoice voices[4];
  std::uint32_t blocks;      // control ticks so far
  std::uint32_t song_pos;
  std::uint32_t midi_ctrl;   // game-visible controller value
  std::uint32_t starts;      // voices started by the game
};
ToySynth g_toy;

void toyStartVoice(std::uint32_t step, std::uint32_t frames, std::int32_t vol) {
  for (ToyVoice& v : g_toy.voices) {
    if (v.active) continue;
    v = ToyVoice{1, 0, step, frames, vol};
    return;
  }
}

// "snd" calls from game code.
void toyPlay(std::uint32_t frames) {
  ++g_toy.starts;
  toyStartVoice(0x0100'0000u + g_toy.starts * 0x0013'0000u, frames, 6000);
}
bool toyAnyActive() {
  for (const ToyVoice& v : g_toy.voices)
    if (v.active) return true;
  return false;
}

void toyRender(void*, std::int16_t* out, std::uint32_t frames) {
  // Sequencer: every 8th block starts a note; the controller ramps per block.
  ++g_toy.blocks;
  g_toy.midi_ctrl = (g_toy.midi_ctrl + 3) & 0x7F;
  if (g_toy.blocks % 8 == 0) {
    ++g_toy.song_pos;
    toyStartVoice(0x0080'0000u * (1 + g_toy.song_pos % 5), 1000 + 37 * (g_toy.song_pos % 11), 3000);
  }
  for (std::uint32_t i = 0; i < frames; ++i) {
    std::int32_t l = 0, r = 0;
    for (std::uint32_t k = 0; k < 4; ++k) {
      ToyVoice& v = g_toy.voices[k];
      if (!v.active) continue;
      v.phase += v.step;
      const std::int32_t tri = static_cast<std::int32_t>(v.phase >> 16) - 32768;  // saw
      const std::int32_t s = (tri * v.vol) >> 15;
      l += (k & 1) ? s / 2 : s;
      r += (k & 1) ? s : s / 2;
      if (--v.remaining == 0) v.active = 0;
    }
    out[2 * i] = static_cast<std::int16_t>(l);
    out[2 * i + 1] = static_cast<std::int16_t>(r);
  }
}

void toyHash(void*, StateHasher& h) {
  h.update_u64(0x544F'5953'594E'5448ull);
  for (const ToyVoice& v : g_toy.voices) {
    h.update_u64(v.active | (std::uint64_t{v.remaining} << 8));
    h.update_u64(v.phase | (std::uint64_t{v.step} << 32));
    h.update_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(v.vol)));
  }
  h.update_u64(g_toy.blocks);
  h.update_u64(g_toy.song_pos);
  h.update_u64(g_toy.midi_ctrl);
  h.update_u64(g_toy.starts);
}

SdkConfig toyConfig(bool output) {
  g_toy = ToySynth{};
  SdkConfig cfg;
  cfg.seed = 5;
  cfg.audio_engine.render = &toyRender;
  cfg.audio_engine.hash = &toyHash;
  cfg.audio_output = output;
  cfg.state_regions.push_back(std::as_writable_bytes(std::span<ToySynth, 1>(&g_toy, 1)));
  return cfg;
}

// Game memory in the arena: what the game read from the engine.
struct BoardGame {
  std::uint32_t polled_ctrl;
  std::uint32_t waits;
  std::uint32_t wait_ticks;
};
BoardGame* g_board = nullptr;

void boardMain(void*) {
  g_board = static_cast<BoardGame*>(OSAllocFromArenaLo(sizeof(BoardGame), 32));
  *g_board = BoardGame{};
  for (std::uint32_t frame = 0;; ++frame) {
    g_board->polled_ctrl = g_board->polled_ctrl * 131u + g_toy.midi_ctrl;
    if (frame % 25 == 3) {
      // SNDGRP_WAIT: start a sound, then spin on the clock until it stops.
      toyPlay(2000 + frame * 7);
      const OSTick start = OSGetTick();
      while (toyAnyActive() && OSGetTick() - start < static_cast<OSTick>(OSMillisecondsToTicks(500))) {
      }
      ++g_board->waits;
      g_board->wait_ticks += OSGetTick() - start;
    }
    VIWaitForRetrace();
  }
}

struct Run {
  std::vector<std::uint64_t> hashes;
  std::vector<std::uint64_t> frames_out;
  std::vector<std::int16_t> pcm;
  std::uint32_t waits = 0;  // copied out: the arena is gone once the Sdk is
};

void stepRun(Sdk& sdk, Run& run, int frames) {
  for (int i = 0; i < frames; ++i) {
    REQUIRE(step(sdk) == StepStatus::running);
    StateHasher h;
    sdk.hashState(h);
    h.update_u64(g_board->polled_ctrl);
    h.update_u64(g_board->waits);
    h.update_u64(g_board->wait_ticks);
    run.hashes.push_back(h.digest());
    run.frames_out.push_back(sdk.audioFrames());
    auto pcm = sdk.takeAudio();
    run.pcm.insert(run.pcm.end(), pcm.begin(), pcm.end());
    run.waits = g_board->waits;
  }
}

Run runBoard(bool output, int frames) {
  Run run;
  std::unique_ptr<Sdk> sdk;
  REQUIRE(Sdk::create(toyConfig(output), &boardMain, nullptr, sdk).ok());
  stepRun(*sdk, run, frames);
  return run;
}

std::uint64_t hashPcm(const std::vector<std::int16_t>& pcm) {
  StateHasher h;
  for (std::int16_t s : pcm) h.update_u64(static_cast<std::uint16_t>(s));
  return h.digest();
}

}  // namespace

TEST_CASE("an attached engine is stepped on 5 ms AI ticks and its PCM reaches the host") {
  const Run run = runBoard(true, 240);
  REQUIRE(run.frames_out.back() > 0);
  CHECK(run.pcm.size() == run.frames_out.back() * 2);
  CHECK(run.waits == 10);
  // Audio follows virtual time: the field clock (a floor), plus whatever the
  // busy-waits ran ahead of it, within one 5 ms block.
  const double secs = static_cast<double>(run.frames_out.back()) / kAiRate32k;
  CHECK(secs >= 240.0 * 1001 / 60000 - 0.005);
  CHECK(secs < 240.0 * 1001 / 60000 + 10 * 0.5 + 0.01);
  bool loud = false;
  for (std::int16_t s : run.pcm) loud |= (s > 1000 || s < -1000);
  CHECK(loud);
  CHECK(hashPcm(run.pcm) == 10260993172534503239ull);  // golden PCM: integer synth, must match everywhere
  CHECK(run.hashes.back() == 2645071316495301120ull);  // golden trail
}

TEST_CASE("null audio output drops only the PCM: identical state, identical sample count") {
  const Run real = runBoard(true, 180);
  const Run null = runBoard(false, 180);
  CHECK(null.pcm.empty());
  CHECK(real.hashes == null.hashes);
  CHECK(real.frames_out == null.frames_out);
}

TEST_CASE("without busy-waits, samples per frame follow the field clock exactly") {
  g_toy = ToySynth{};
  Game game{[] {
    for (;;) VIWaitForRetrace();
  }};
  auto sdk = boot(game, toyConfig(true));
  std::int64_t prev_ticks = 0;
  for (int n = 0; n < 600; ++n) {
    REQUIRE(step(*sdk) == StepStatus::running);
    // Ticks due before the next field ran in this step: deadlines epoch + k * 202500.
    const std::int64_t field_end = static_cast<std::int64_t>(n + 1) * 675'675;
    const std::int64_t ticks = (field_end - 1) / kAiEngineBlockTicks;
    CHECK(sdk->audioFrames() == static_cast<std::uint64_t>(ticks) * kAiEngineBlockFrames);
    CHECK(ticks >= prev_ticks);
    prev_ticks = ticks;
  }
}

TEST_CASE("save state restores engine statics and arena: identical trail and PCM") {
  std::unique_ptr<Sdk> sdk;
  REQUIRE(Sdk::create(toyConfig(true), &boardMain, nullptr, sdk).ok());
  Run warm;
  stepRun(*sdk, warm, 77);  // mid-note
  const auto state = sdk->saveState();
  const std::uint64_t h = sdk->stateHash();
  Run first;
  stepRun(*sdk, first, 60);
  REQUIRE(sdk->loadState(state).ok());
  CHECK(sdk->stateHash() == h);
  Run second;
  stepRun(*sdk, second, 60);
  CHECK(first.hashes == second.hashes);
  CHECK(first.pcm == second.pcm);
}

TEST_CASE("engine state is in the hash (perturbation)") {
  std::unique_ptr<Sdk> sdk;
  REQUIRE(Sdk::create(toyConfig(false), &boardMain, nullptr, sdk).ok());
  stepN(*sdk, 30);
  const std::uint64_t base = sdk->stateHash();
  g_toy.midi_ctrl ^= 1;
  CHECK(sdk->stateHash() != base);
  g_toy.midi_ctrl ^= 1;
  g_toy.voices[3].phase ^= 1;
  CHECK(sdk->stateHash() != base);
  g_toy.voices[3].phase ^= 1;
  CHECK(sdk->stateHash() == base);
}

TEST_CASE("a chained DMA callback mixes into the engine's block (THP movie audio)") {
  static AIDCallback s_old = nullptr;
  static int s_calls = 0;
  s_old = nullptr;
  s_calls = 0;
  Game game{[] {
    s_old = AIRegisterDMACallback([] {
      ++s_calls;
      s_old();  // the engine renders the next block
      auto* block = reinterpret_cast<std::int16_t*>(AIGetDMAStartAddr());
      const u32 n = AIGetDMALength() / 2;
      for (u32 i = 0; i < n; ++i) block[i] = static_cast<std::int16_t>(block[i] + 7);
    });
    for (;;) VIWaitForRetrace();
  }};
  SdkConfig cfg = toyConfig(true);
  auto sdk = boot(game, cfg);
  stepN(*sdk, 10);
  CHECK(s_old != nullptr);
  CHECK(s_calls > 0);
  const auto pcm = sdk->takeAudio();
  // The first block played before the callback was registered; from the
  // second on, every sample carries the +7 (the toy synth is silent until its
  // first note on block 8).
  REQUIRE(pcm.size() > 2 * 8 * kAiEngineBlockFrames);
  for (std::size_t i = 2 * kAiEngineBlockFrames; i < 2 * 7 * kAiEngineBlockFrames; ++i) CHECK(pcm[i] == 7);
}

TEST_CASE("game-driven AI DMA without an engine: blocks, bytes left, 48 kHz") {
  struct Bufs {
    alignas(32) std::int16_t b[2][96 * 2];  // 2 ms blocks at 48 kHz
    int next;
    u32 left_mid;
    u32 left_start;
  };
  static Bufs* s_bufs = nullptr;
  Game game{[] {
    s_bufs = static_cast<Bufs*>(OSAllocFromArenaLo(sizeof(Bufs), 32));
    std::memset(s_bufs, 0, sizeof(Bufs));
    for (int i = 0; i < 96 * 2; ++i) {
      s_bufs->b[0][i] = 100;
      s_bufs->b[1][i] = -100;
    }
    AIInit(nullptr);
    CHECK(AIRegisterDMACallback([] {
            s_bufs->next ^= 1;
            AIInitDMA(reinterpret_cast<uintptr_t>(s_bufs->b[s_bufs->next]), sizeof(s_bufs->b[0]));
          }) == nullptr);
    AISetDSPSampleRate(AI_SAMPLERATE_48KHZ);
    AIInitDMA(reinterpret_cast<uintptr_t>(s_bufs->b[0]), sizeof(s_bufs->b[0]));
    AIStartDMA();
    s_bufs->left_start = AIGetDMABytesLeft();
    OSTick t0 = OSGetTick();
    while (OSGetTick() - t0 < static_cast<OSTick>(OSMicrosecondsToTicks(1000))) {
    }
    s_bufs->left_mid = AIGetDMABytesLeft();
    for (;;) VIWaitForRetrace();
  }};
  auto sdk = boot(game);
  stepN(*sdk, 3);
  CHECK(sdk->audioRate() == kAiRate48k);
  CHECK(s_bufs->left_start == sizeof(s_bufs->b[0]));
  CHECK(s_bufs->left_mid < s_bufs->left_start);
  CHECK(s_bufs->left_mid > 0);
  const auto pcm = sdk->takeAudio();
  REQUIRE(pcm.size() >= 4 * 96 * 2);
  // Blocks alternate: +100 block first, then -100.
  CHECK(pcm[0] == 100);
  CHECK(pcm[96 * 2] == -100);
  CHECK(pcm[2 * 96 * 2] == 100);
  // 2 ms blocks: 48000 Hz * 3 fields of 1001/60000 s, give or take one block.
  const std::uint64_t expect = 48000ull * 3 * 1001 / 60000;
  CHECK(sdk->audioFrames() + 96 >= expect);
  CHECK(sdk->audioFrames() <= expect + 96);
}

TEST_CASE("AIStopDMA stops the ticks") {
  Game game{[] {
    alignas(32) static std::int16_t buf[64];
    AIInitDMA(reinterpret_cast<uintptr_t>(buf), sizeof buf);
    AIStartDMA();
    VIWaitForRetrace();
    AIStopDMA();
    CHECK_FALSE(AIGetDMAEnableFlag());
    CHECK(AIGetDMABytesLeft() == 0);
    for (;;) VIWaitForRetrace();
  }};
  auto sdk = boot(game);
  stepN(*sdk, 2);
  const auto frames = sdk->audioFrames();
  CHECK(frames > 0);
  stepN(*sdk, 5);
  CHECK(sdk->audioFrames() == frames);
}

TEST_CASE("the idle function runs once per frame, after every other thread waits") {
  static Log* s_log = nullptr;
  Log log;
  s_log = &log;
  Game game{[] {
    OSSetIdleFunction([](void* p) { s_log->add("idle" + std::to_string(reinterpret_cast<std::uintptr_t>(p))); },
                      reinterpret_cast<void*>(std::uintptr_t{7}), nullptr, 0);
    for (int f = 0;; ++f) {
      s_log->add("main" + std::to_string(f));
      if (f == 3) OSSetIdleFunction(nullptr, nullptr, nullptr, 0);
      VIWaitForRetrace();
    }
  }};
  auto sdk = boot(game);
  stepN(*sdk, 6);
  // Frame 0 sets it after the step began (armed from frame 1); frame 3 clears it.
  CHECK(log.joined() == "main0 main1 idle7 main2 idle7 main3 main4 main5");
}
