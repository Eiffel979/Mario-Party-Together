// Cooperative scheduling determinism and virtual-interrupt ordering (E10,
// ADR 0008 Â§1-2).
#include <cstdio>

#include "harness.h"
#include "mpt/gc/file_source.h"
#include "mpt_gc/dvd.h"
#include "mpt_gc/os.h"
#include "mpt_gc/vi.h"

using namespace mpt;
using namespace mpt::gc;
using namespace mpt::gc::test;

namespace {

Log* g_log = nullptr;
void logLine(const char* s) { g_log->add(s); }

struct Worker {
  OSThread thread;
  const char* name;
  OSThreadQueue* queue;
  int rounds;
};

void* workerMain(void* arg) {
  auto* w = static_cast<Worker*>(arg);
  char buf[32];
  for (int i = 0; i < w->rounds; ++i) {
    std::snprintf(buf, sizeof buf, "%s%d", w->name, i);
    logLine(buf);
    OSSleepThread(w->queue);
  }
  std::snprintf(buf, sizeof buf, "%s-end", w->name);
  logLine(buf);
  return w;
}

// Main (priority 16) starts A (20), B (10) and C (10), then wakes them each frame.
std::string runPriorityScenario(std::vector<std::uint64_t>* hashes) {
  Log log;
  g_log = &log;
  static OSThreadQueue queue;
  static Worker a{{}, "A", &queue, 2}, b{{}, "B", &queue, 2}, c{{}, "C", &queue, 2};
  Game g{[&] {
    OSInitThreadQueue(&queue);
    for (Worker* w : {&a, &b, &c})
      OSCreateThread(&w->thread, &workerMain, w, nullptr, 0, w == &a ? 20 : 10, 0);
    logLine("m-create");
    OSResumeThread(&a.thread);  // lower priority than main: does not run yet
    logLine("m-resA");
    OSResumeThread(&b.thread);  // higher: preempts main at once
    logLine("m-resB");
    OSResumeThread(&c.thread);
    logLine("m-resC");
    for (int frame = 0; frame < 3; ++frame) {
      VIWaitForRetrace();
      logLine("m-wake");
      OSWakeupThread(&queue);
      logLine("m-woke");
    }
    void* ret = nullptr;
    CHECK(OSJoinThread(&a.thread, &ret));
    CHECK(ret == &a);
    logLine("m-joined");
    for (;;) VIWaitForRetrace();
  }};
  auto sdk = boot(g);
  for (int i = 0; i < 6; ++i) {
    REQUIRE(step(*sdk) == StepStatus::running);
    if (hashes) hashes->push_back(sdk->stateHash());
  }
  g_log = nullptr;
  return log.joined();
}

}  // namespace

TEST_CASE("threads run in priority order, FIFO within a priority, preempting at SDK calls") {
  const std::string expected =
      "m-create m-resA B0 m-resB C0 m-resC A0 "  // A runs once main and B/C wait
      "m-wake B1 C1 m-woke A1 "
      "m-wake B-end C-end m-woke A-end "
      "m-wake m-woke m-joined";
  CHECK(runPriorityScenario(nullptr) == expected);
}

TEST_CASE("scheduling is deterministic: identical logs and hash trails across runs") {
  std::vector<std::uint64_t> h1, h2;
  const std::string l1 = runPriorityScenario(&h1);
  const std::string l2 = runPriorityScenario(&h2);
  CHECK(l1 == l2);
  CHECK(h1 == h2);
}

namespace {
struct PingPong {
  OSThread thread;
  OSMessageQueue to_worker, to_main;
  OSMessage worker_buf[1], main_buf[1];
};

void* ponger(void* arg) {
  auto* p = static_cast<PingPong*>(arg);
  for (;;) {
    OSMessage m = nullptr;
    OSReceiveMessage(&p->to_worker, &m, OS_MESSAGE_BLOCK);
    const auto v = reinterpret_cast<std::uintptr_t>(m);
    if (v == 0) return nullptr;
    OSSendMessage(&p->to_main, reinterpret_cast<OSMessage>(v * 2), OS_MESSAGE_BLOCK);
  }
}
}  // namespace

TEST_CASE("message queues block and wake in order") {
  std::vector<std::uintptr_t> got;
  static PingPong p;
  Game g{[&] {
    OSInitMessageQueue(&p.to_worker, p.worker_buf, 1);
    OSInitMessageQueue(&p.to_main, p.main_buf, 1);
    OSCreateThread(&p.thread, &ponger, &p, nullptr, 0, 20, 0);
    OSResumeThread(&p.thread);
    for (std::uintptr_t i = 1; i <= 5; ++i) {
      CHECK(OSSendMessage(&p.to_worker, reinterpret_cast<OSMessage>(i), OS_MESSAGE_BLOCK));
      OSMessage m = nullptr;
      CHECK(OSReceiveMessage(&p.to_main, &m, OS_MESSAGE_BLOCK));
      got.push_back(reinterpret_cast<std::uintptr_t>(m));
    }
    OSMessage none = nullptr;
    CHECK_FALSE(OSReceiveMessage(&p.to_main, &none, OS_MESSAGE_NOBLOCK));
    OSSendMessage(&p.to_worker, nullptr, OS_MESSAGE_BLOCK);
    OSJoinThread(&p.thread, nullptr);
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(got == std::vector<std::uintptr_t>{2, 4, 6, 8, 10});
}

namespace {
OSAlarm g_alarms[3];
void alarmNamed(OSAlarm* alarm, OSContext*) {
  static const char* names[] = {"X", "Y", "Z"};
  g_log->add(names[alarm - g_alarms]);
}
void dvdDone(s32, DVDFileInfo*) { g_log->add("dvd"); }
}  // namespace

TEST_CASE("virtual interrupts run in deadline order, then fixed source order, then scheduling order") {
  Log log;
  g_log = &log;
  MemoryFileSource files({{"f.bin", std::vector<std::byte>(64, std::byte{7})}});
  Game g{[&] {
    static DVDFileInfo fi;
    static std::byte buf[64];
    const OSTime t = OSGetTime();
    for (auto& a : g_alarms) OSCreateAlarm(&a);
    OSSetAbsAlarm(&g_alarms[0], t + 1000, &alarmNamed);  // X
    OSSetAbsAlarm(&g_alarms[1], t + 500, &alarmNamed);   // Y, earlier deadline
    OSSetAbsAlarm(&g_alarms[2], t + 1000, &alarmNamed);  // Z, same deadline as X, scheduled after it
    CHECK(DVDOpen("f.bin", &fi));
    CHECK(DVDReadAsync(&fi, buf, 64, 0, &dvdDone));  // completes now, callback at the next boundary
    log.add("main");
    VIWaitForRetrace();  // idle: the scheduler runs interrupts up to the next field
  }};
  SdkConfig cfg;
  cfg.files = &files;
  auto sdk = boot(g, cfg);
  REQUIRE(step(*sdk) == StepStatus::running);
  g_log = nullptr;
  CHECK(log.joined() == "main dvd Y X Z");
}

TEST_CASE("at one deadline the retrace (VI) runs before an alarm scheduled earlier") {
  Log log;
  g_log = &log;
  Game g{[&] {
    OSCreateAlarm(&g_alarms[0]);
    // Absolute alarm exactly at the first retrace (R_1 = epoch + one field).
    OSSetAbsAlarm(&g_alarms[0], OSGetTime() - kTimeQueryQuantumTicks + 675675, &alarmNamed);
    VISetPreRetraceCallback([](u32) { g_log->add("vi"); });
    VIWaitForRetrace();
    log.add("main");
  }};
  auto sdk = boot(g);
  REQUIRE(step(*sdk) == StepStatus::running);
  CHECK(log.joined().empty());  // the alarm is not before the next field: it waits for frame 1
  CHECK(step(*sdk) == StepStatus::halted);
  g_log = nullptr;
  CHECK(log.joined() == "vi X main");
}

TEST_CASE("interrupts never run between SDK calls") {
  int flag_seen_before = -1, flag_seen_after = -1;
  Game g{[&] {
    static OSAlarm alarm;
    OSCreateAlarm(&alarm);
    static volatile int flag = 0;
    flag = 0;
    OSSetAlarm(&alarm, 1, [](OSAlarm*, OSContext*) { flag = 1; });
    // Due as soon as time moves, but nothing advances time or reaches a boundary here.
    volatile int spin = 0;
    for (int i = 0; i < 100000; ++i) spin = spin + 1;
    flag_seen_before = flag;
    OSGetTick();
    flag_seen_after = flag;
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(flag_seen_before == 0);
  CHECK(flag_seen_after == 1);
}

namespace {
OSThreadQueue g_hiq;
void wakeHi(OSAlarm*, OSContext*) { OSWakeupThread(&g_hiq); }
void* hiThread(void*) {
  OSSleepThread(&g_hiq);
  g_log->add("hi");
  return nullptr;
}
}  // namespace

TEST_CASE("a thread woken by an interrupt preempts a lower-priority busy-wait at the boundary") {
  Log log;
  g_log = &log;
  Game g{[&] {
    static OSThread hi;
    static OSAlarm alarm;
    OSInitThreadQueue(&g_hiq);
    OSCreateThread(&hi, &hiThread, nullptr, nullptr, 0, 4, 0);
    OSResumeThread(&hi);  // runs at once and sleeps
    log.add("main");
    OSCreateAlarm(&alarm);
    OSSetAlarm(&alarm, 3 * kTimeQueryQuantumTicks, &wakeHi);
    for (int i = 1; i <= 5; ++i) {
      OSGetTick();
      log.add("q" + std::to_string(i));
    }
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::halted);
  g_log = nullptr;
  CHECK(log.joined() == "main q1 q2 hi q3 q4 q5");
}

namespace {
void* yielder(void* arg) {
  const char* name = static_cast<const char*>(arg);
  for (int i = 0; i < 3; ++i) {
    g_log->add(std::string(name) + std::to_string(i));
    OSYieldThread();
  }
  return nullptr;
}
}  // namespace

TEST_CASE("OSYieldThread round-robins equal priorities") {
  Log log;
  g_log = &log;
  Game g{[&] {
    static OSThread t1, t2;
    static char n1[] = "a", n2[] = "b";
    OSCreateThread(&t1, &yielder, n1, nullptr, 0, 16, 0);
    OSCreateThread(&t2, &yielder, n2, nullptr, 0, 16, 0);
    OSResumeThread(&t1);
    OSResumeThread(&t2);
    for (int i = 0; i < 4; ++i) {
      log.add("m" + std::to_string(i));
      OSYieldThread();
    }
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::halted);
  g_log = nullptr;
  CHECK(log.joined() == "m0 a0 b0 m1 a1 b1 m2 a2 b2 m3");
}

namespace {
OSMutex g_mutex;
void* locker(void*) {
  OSLockMutex(&g_mutex);
  g_log->add("t-locked");
  OSUnlockMutex(&g_mutex);
  return nullptr;
}
}  // namespace

TEST_CASE("mutexes block, hand over and support suspend/resume") {
  Log log;
  g_log = &log;
  Game g{[&] {
    static OSThread t;
    OSInitMutex(&g_mutex);
    OSLockMutex(&g_mutex);
    OSCreateThread(&t, &locker, nullptr, nullptr, 0, 8, 0);
    CHECK(OSIsThreadSuspended(&t));
    OSResumeThread(&t);  // runs, blocks on the mutex
    log.add("m-unlock");
    OSUnlockMutex(&g_mutex);  // hands over: t preempts main
    log.add("m-after");
    CHECK(OSIsThreadTerminated(&t));
    CHECK(OSJoinThread(&t, nullptr));
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::halted);
  g_log = nullptr;
  CHECK(log.joined() == "m-unlock t-locked m-after");
}

TEST_CASE("blocking in an interrupt handler is a fault, not a hang") {
  Game g{[] {
    static OSAlarm alarm;
    OSCreateAlarm(&alarm);
    OSSetAlarm(&alarm, 1, [](OSAlarm*, OSContext*) { VIWaitForRetrace(); });
    for (;;) VIWaitForRetrace();
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::faulted);
  CHECK(sdk->fault().find("interrupt") != std::string::npos);
}

TEST_CASE("OSPanic stops the runtime with the message") {
  Game g{[] { OSPanic("game.c", 42, "bad %s", "thing"); }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::faulted);
  CHECK(sdk->fault() == "OSPanic at game.c:42: bad thing");
  CHECK(step(*sdk) == StepStatus::faulted);  // stays stopped
}

TEST_CASE("OSResetSystem is reported to the host") {
  Game g{[] { OSResetSystem(0, 0, FALSE); }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::reset_requested);
}

TEST_CASE("only one SDK instance at a time") {
  Game g{[] {}};
  auto first = boot(g);
  std::unique_ptr<Sdk> second;
  CHECK_FALSE(Sdk::create({}, &gameMain, &g, second).ok());
  first.reset();
  CHECK(Sdk::create({}, &gameMain, &g, second).ok());
}

namespace {
// Recursion with large frames, each written in full (a volatile buffer), so the
// overflow runs through the canary rather than skipping over it.
int deep(int n) {
  volatile char frame[4096];
  for (int i = 0; i < 4096; ++i) frame[i] = static_cast<char>(n + i);
  if (n == 0) return frame[0];
  return deep(n - 1) + frame[4095];
}
int g_sink = 0;
void* overflowingThread(void*) {
  g_sink = deep(24);  // about 100 KiB on a 64 KiB stack
  return nullptr;
}
}  // namespace

TEST_CASE("a coroutine stack overflow is a deterministic fault") {
  Game g{[] {
    static OSThread t;
    OSCreateThread(&t, &overflowingThread, nullptr, nullptr, 0, 8, 0);
    OSResumeThread(&t);
    for (;;) VIWaitForRetrace();
  }};
  SdkConfig cfg;
  cfg.thread_stack_bytes = 64 * 1024;
  auto sdk = boot(g, cfg);
  CHECK(step(*sdk) == StepStatus::faulted);
  CHECK(sdk->fault().find("stack overflow in SDK thread slot 1") != std::string::npos);
}
