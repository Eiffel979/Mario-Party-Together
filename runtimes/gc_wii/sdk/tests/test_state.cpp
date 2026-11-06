// State hash and save states (E10, ADR 0008 §8, §10): hashes are a pure
// function of seed, card image and inputs; save/load resumes coroutines mid-wait.
#include <cstring>

#include "harness.h"
#include "mpt/gc/digest.h"
#include "mpt_gc/ar.h"
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
    // As on the console, OSCheckHeap is what one OSAlloc can take from a single free block.
    ok = ok && free0 == 0x10000 - 32;
    ok = ok && OSAllocFromHeap(h, 0x20000) == nullptr;
    void* fs = static_cast<std::byte*>(lo) + 0x1000;
    void* fe = static_cast<std::byte*>(lo) + 0x2000;
    ok = ok && OSAllocFixed(&fs, &fe) != nullptr;
    ok = ok && OSCheckHeap(h) == free0 - 0x1000 - 32;  // the carve leaves two free blocks, two headers
    void* rest = OSAllocFromHeap(h, static_cast<u32>(0x10000 - 0x2000 - 32));
    ok = ok && rest != nullptr;
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(ok);
}

// P1-16: the memory a title's state hash covers. Free heap space never appears; allocated blocks
// and main RAM outside every heap do, in address order.
TEST_CASE("hashed RAM ranges cover allocated blocks and RAM outside heaps, never free space") {
  std::byte* heap_lo = nullptr;
  void* kept = nullptr;
  void* freed = nullptr;
  Game g{[&] {
    void* lo = OSInitAlloc(OSGetArenaLo(), OSGetArenaHi(), 1);
    heap_lo = static_cast<std::byte*>(lo);
    const OSHeapHandle h = OSCreateHeap(lo, heap_lo + 0x10000);
    kept = OSAllocFromHeap(h, 100);
    freed = OSAllocFromHeap(h, 200);
    OSFreeToHeap(h, freed);
    u32 stack[8];
    ARInit(stack, 8);
    ARAlloc(0x1000);
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::halted);
  REQUIRE(heap_lo != nullptr);

  const std::span<std::byte> ram = sdk->mainRam();
  std::vector<Sdk::RamRange> ranges;
  sdk->forEachHashedRamRange([&](const Sdk::RamRange& r) { ranges.push_back(r); });
  std::size_t covered = 0;
  bool kept_seen = false;
  const std::byte* last_end = ram.data();
  for (const Sdk::RamRange& r : ranges) {
    CHECK(r.bytes.data() >= last_end);  // address order, no overlap
    last_end = r.bytes.data() + r.bytes.size();
    CHECK(last_end <= ram.data() + ram.size());
    const bool in_heap = r.bytes.data() >= heap_lo && r.bytes.data() < heap_lo + 0x10000;
    CHECK(in_heap == (r.data != nullptr));
    if (r.data == kept) {
      kept_seen = true;
      CHECK(r.data_size >= 100);
      CHECK(r.bytes.data() + (r.bytes.size() - r.data_size) == r.data);
    }
    const std::byte* f = static_cast<const std::byte*>(freed);
    CHECK_FALSE((f >= r.bytes.data() && f < last_end));  // the freed block's data
    covered += r.bytes.size();
  }
  CHECK(kept_seen);
  // Everything but the heap's free space: main RAM less the heap, plus the one allocated block.
  CHECK(covered == ram.size() - 0x10000 + (128 + 32));
  CHECK(sdk->aramInUse().size() == AR_RESERVED_BYTES + 0x1000);
  CHECK(sdk->aramInUse().data() == sdk->aram().data());
}

TEST_CASE("memoryDigest depends on contents and length, never on the address") {
  std::vector<std::byte> a(1000), b(1003);
  for (std::size_t i = 0; i < a.size(); ++i) a[i] = static_cast<std::byte>((i * 37 + 11) & 0xFF);
  std::memcpy(b.data() + 3, a.data(), a.size());  // same bytes, misaligned copy
  const std::uint64_t d = memoryDigest(a);
  CHECK(memoryDigest(std::span<const std::byte>(b).subspan(3)) == d);
  for (std::size_t at : {std::size_t{0}, std::size_t{31}, std::size_t{32}, std::size_t{995}, std::size_t{999}}) {
    a[at] ^= std::byte{1};
    CHECK(memoryDigest(a) != d);
    a[at] ^= std::byte{1};
  }
  CHECK(memoryDigest(std::span<const std::byte>(a).first(999)) != d);
  // Goldens: the digest is part of the hash manifest, so it must not change silently and must
  // match on every platform.
  CHECK(memoryDigest({}) == 0xEF46DB3751D8E999ull);
  CHECK(d == 0x128DA10CFBDC59D9ull);
}

TEST_CASE("memoryDigestWords transforms whole words only, and is memoryDigest for the identity") {
  std::vector<std::byte> a(203);
  for (std::size_t i = 0; i < a.size(); ++i) a[i] = static_cast<std::byte>((i * 53 + 7) & 0xFF);
  struct Identity {
    static bool special(std::uint64_t) { return false; }
    std::uint64_t operator()(std::uint64_t w) const { return w; }
  };
  CHECK(memoryDigestWords(a, Identity{}) == memoryDigest(a));
  // Zeroing every word: the same digest as all-zero words, and the 3 trailing bytes stay as they are.
  struct Zero {
    static bool special(std::uint64_t) { return true; }
    std::uint64_t operator()(std::uint64_t) const { return 0; }
  };
  std::vector<std::byte> z(a.size());
  std::memcpy(z.data() + 200, a.data() + 200, 3);
  CHECK(memoryDigestWords(a, Zero{}) == memoryDigest(z));
  // special() false for a block means word_fn is not applied to it (a superset test, never a subset).
  struct Never {
    static bool special(std::uint64_t) { return false; }
    std::uint64_t operator()(std::uint64_t) const { return 0; }
  };
  CHECK(memoryDigestWords(std::span<const std::byte>(a).first(64), Never{}) == memoryDigest(std::span<const std::byte>(a).first(64)));
}

namespace {
OSAlarm g_id_alarm;
void idAlarmHandler(OSAlarm*, OSContext*) {}
std::uint64_t idA(const void* p) { return p == reinterpret_cast<const void*>(&idAlarmHandler) ? 0x1234 : 0; }
std::uint64_t idB(const void* p) { return p == reinterpret_cast<const void*>(&idAlarmHandler) ? 0x5678 : 0; }
std::uint64_t idNone(const void*) { return 0; }

std::uint64_t alarmHashWith(std::uint64_t (*pointer_id)(const void*)) {
  Game g{[] {
    OSCreateAlarm(&g_id_alarm);
    OSSetAlarm(&g_id_alarm, OSMillisecondsToTicks(1000), &idAlarmHandler);
  }};
  SdkConfig cfg;
  cfg.pointer_id = pointer_id;
  auto sdk = boot(g, cfg);
  CHECK(step(*sdk) == StepStatus::halted);
  return sdk->stateHash();
}
}  // namespace

// ADR 0008, P1-13 amendment point 5: function pointers hash as the title's stable IDs, not a sentinel.
TEST_CASE("SdkConfig::pointer_id names functions in the SDK's manifest") {
  const std::uint64_t sentinel = alarmHashWith(nullptr);
  CHECK(alarmHashWith(&idNone) == sentinel);  // 0 means unknown: the sentinel stays
  const std::uint64_t a = alarmHashWith(&idA);
  CHECK(a != sentinel);
  CHECK(alarmHashWith(&idB) != a);
  CHECK(alarmHashWith(&idA) == a);
}
