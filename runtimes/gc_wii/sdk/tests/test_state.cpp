// State hash and save states (E10, ADR 0008 §8, §10): hashes are a pure
// function of seed, card image and inputs; save/load resumes coroutines mid-wait.
#include <cstring>

#include "harness.h"
#include "mpt_gc/card.h"
#include "mpt_gc/os.h"
#include "mpt_gc/pad.h"
#include "mpt_gc/vi.h"

using namespace mpt;
using namespace mpt::gc;
using namespace mpt::gc::test;

namespace {

// A small synthetic "game": a worker thread fed through a message queue, an
// alarm, a heap, PAD reads and a card write every 20 frames. Its observable
// output is a running checksum it stores in main RAM.
struct World {
  OSThread worker;
  OSMessageQueue mq;
  OSMessage buf[4];
  OSAlarm alarm;
  std::uint32_t* sum;
  int ticks;
  CARDFileInfo fi;
};
World* g_world = nullptr;
alignas(32) std::byte g_card_work[CARD_WORKAREA_SIZE];

void* worker(void*) {
  for (;;) {
    OSMessage m;
    OSReceiveMessage(&g_world->mq, &m, OS_MESSAGE_BLOCK);
    *g_world->sum = *g_world->sum * 31u + static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(m));
  }
}

void tick(OSAlarm*, OSContext*) { ++g_world->ticks; }

void worldMain(void*) {
  // Game state lives in the arena, so save states capture it. (Globals outside
  // the arena are the P1-16 registered-globals table; this test has none that
  // change after boot: g_world is set once.)
  World& w = *static_cast<World*>(OSAllocFromArenaLo(sizeof(World), 32));
  g_world = &w;
  OSHeapHandle heap;
  void* lo = OSGetArenaLo();
  void* hi = OSGetArenaHi();
  lo = OSInitAlloc(lo, hi, 2);
  heap = OSCreateHeap(lo, hi);
  OSSetCurrentHeap(heap);
  w.sum = static_cast<std::uint32_t*>(OSAlloc(sizeof(std::uint32_t)));
  *w.sum = 1;
  w.ticks = 0;
  OSInitMessageQueue(&w.mq, w.buf, 4);
  OSCreateThread(&w.worker, &worker, nullptr, nullptr, 0, 12, 0);
  OSResumeThread(&w.worker);
  OSCreateAlarm(&w.alarm);
  OSSetPeriodicAlarm(&w.alarm, OSGetTime(), OSMillisecondsToTicks(7), &tick);
  CARDMount(CARD_SLOT_A, g_card_work, nullptr);
  CARDCreate(CARD_SLOT_A, "world", 8192, &w.fi);
  for (std::uint32_t frame = 0;; ++frame) {
    PADStatus pads[4];
    PADRead(pads);
    const std::uint32_t v = pads[0].button ^ static_cast<std::uint32_t>(static_cast<std::uint8_t>(pads[0].stickX)) ^
                            (OSGetTick() & 0xFF) ^ static_cast<std::uint32_t>(w.ticks);
    OSSendMessage(&w.mq, reinterpret_cast<OSMessage>(static_cast<std::uintptr_t>(v)), OS_MESSAGE_BLOCK);
    void* scratch = OSAlloc(64 + (v & 0xFF));
    OSFree(scratch);
    if (frame % 20 == 19) {
      static std::byte block[8192];
      std::memcpy(block, w.sum, 4);
      CARDWrite(&w.fi, block, 8192, 0);
    }
    VIWaitForRetrace();
  }
}

InputFrame inputFor(std::uint64_t n) {
  InputFrame in;
  in.pads[0] = connectedPad((n % 7 == 0) ? kButtonA : 0);
  in.pads[0].stick_x = static_cast<std::int16_t>((n * 977) % 65535 - 32767);
  return in;
}

std::unique_ptr<Sdk> bootWorld(std::uint64_t seed = 1) {
  SdkConfig cfg;
  cfg.seed = seed;
  std::unique_ptr<Sdk> sdk;
  REQUIRE(Sdk::create(cfg, &worldMain, nullptr, sdk).ok());
  return sdk;
}

std::vector<std::uint64_t> trail(Sdk& sdk, std::uint64_t frames, std::uint64_t flip_frame = ~0ull) {
  std::vector<std::uint64_t> out;
  for (std::uint64_t i = 0; i < frames; ++i) {
    InputFrame in = inputFor(sdk.frame());
    if (sdk.frame() == flip_frame) in.pads[0].buttons ^= kButtonB;
    in.frame = sdk.frame();
    REQUIRE(sdk.stepFrame(in) == StepStatus::running);
    // The SDK's manifest plus the synthetic game's own state (a title's manifest
    // covers its game memory, ADR 0008 §8; here that is the checksum).
    StateHasher h;
    sdk.hashState(h);
    h.update_u64(*g_world->sum);
    h.update_u64(static_cast<std::uint64_t>(g_world->ticks));
    out.push_back(h.digest());
  }
  return out;
}

}  // namespace

TEST_CASE("the hash trail is a pure function of seed and inputs") {
  std::vector<std::uint64_t> a, b;
  {
    auto sdk = bootWorld();
    a = trail(*sdk, 120);
  }
  {
    auto sdk = bootWorld();
    b = trail(*sdk, 120);
  }
  CHECK(a == b);
  CHECK(a.back() == 12072965418954085887ull);  // golden: must match on every platform (P1-13b added the AI/THP/idle areas)
}

TEST_CASE("the seed and a single input bit both change the trail") {
  std::vector<std::uint64_t> base, seeded, flipped;
  {
    auto sdk = bootWorld(1);
    base = trail(*sdk, 60);
  }
  {
    auto sdk = bootWorld(2);
    seeded = trail(*sdk, 60);
  }
  {
    auto sdk = bootWorld(1);
    flipped = trail(*sdk, 60, 30);
  }
  CHECK(seeded[0] != base[0]);
  for (int i = 0; i < 30; ++i) CHECK(flipped[i] == base[i]);
  for (int i = 30; i < 60; ++i) CHECK(flipped[i] != base[i]);
}

TEST_CASE("save state, run, load, run again: identical trail and outputs") {
  auto sdk = bootWorld();
  trail(*sdk, 45);  // mid-run: the worker is blocked, an alarm is pending, the card has data
  const std::vector<std::byte> state = sdk->saveState();
  const std::uint64_t h_at_save = sdk->stateHash();
  const std::uint64_t gen_at_save = sdk->saveMediaGeneration();
  const auto first = trail(*sdk, 50);
  const auto image_first = sdk->saveMedia().data;
  CHECK(sdk->saveMediaGeneration() > gen_at_save);

  REQUIRE(sdk->loadState(state).ok());
  CHECK(sdk->stateHash() == h_at_save);
  CHECK(sdk->frame() == 45);
  CHECK(sdk->saveMediaGeneration() == gen_at_save);  // ADR 0004: loading restores image and generation
  const auto second = trail(*sdk, 50);
  CHECK(first == second);
  CHECK(sdk->saveMedia().data == image_first);
}

TEST_CASE("loadState rejects foreign or damaged states") {
  auto sdk = bootWorld();
  trail(*sdk, 3);
  auto state = sdk->saveState();
  CHECK_FALSE(sdk->loadState({}).ok());
  auto bad = state;
  bad[0] = std::byte{0};
  CHECK_FALSE(sdk->loadState(bad).ok());
  bad = state;
  bad.pop_back();
  CHECK_FALSE(sdk->loadState(bad).ok());
  bad = state;
  bad[24] ^= std::byte{0x10};  // arena base differs
  CHECK_FALSE(sdk->loadState(bad).ok());
  CHECK(sdk->loadState(state).ok());
}

TEST_CASE("presentation-only outputs do not affect the hash") {
  std::uint64_t h1 = 0, h2 = 0;
  {
    auto sdk = bootWorld();
    trail(*sdk, 10);
    h1 = sdk->stateHash();
  }
  {
    auto sdk = bootWorld();
    sdk->setReportSink([](const char*) {});
    trail(*sdk, 10);
    (void)sdk->saveState();  // taking a snapshot is not a game event
    (void)sdk->saveMedia().data;
    h2 = sdk->stateHash();
  }
  CHECK(h1 == h2);
}

TEST_CASE("OSAlloc heaps live in the arena and coalesce") {
  bool ok = true;
  Game g{[&] {
    void* lo = OSInitAlloc(OSGetArenaLo(), OSGetArenaHi(), 1);
    const OSHeapHandle h = OSCreateHeap(lo, static_cast<std::byte*>(lo) + 0x10000);
    const s32 free0 = OSCheckHeap(h);
    void* a = OSAllocFromHeap(h, 100);
    void* b = OSAllocFromHeap(h, 200);
    void* c = OSAllocFromHeap(h, 300);
    ok = ok && a && b && c && reinterpret_cast<std::uintptr_t>(a) % 32 == 0;
    ok = ok && static_cast<std::byte*>(b) == static_cast<std::byte*>(a) + 128 + 32;
    OSFreeToHeap(h, b);
    OSFreeToHeap(h, a);
    OSFreeToHeap(h, c);
    ok = ok && OSCheckHeap(h) == free0;
    ok = ok && OSAllocFromHeap(h, 0x20000) == nullptr;
    void* fs = static_cast<std::byte*>(lo) + 0x1000;
    void* fe = static_cast<std::byte*>(lo) + 0x2000;
    ok = ok && OSAllocFixed(&fs, &fe) != nullptr;
    ok = ok && OSCheckHeap(h) == free0 - 0x1000;
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(ok);
}
