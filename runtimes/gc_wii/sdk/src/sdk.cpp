// The deterministic core of the SDK layer: the arena, virtual time, the
// cooperative scheduler, virtual interrupts and the frame step (ADR 0008 §1-3, §7).
//
// Threads never run on host threads. Each SDK thread (and the root coroutine,
// the game's main()) is a coroutine on an arena stack. The host's stepFrame()
// is the scheduler: it resumes the best ready thread until that thread blocks,
// yields or is preempted at an SDK call boundary, and returns once nothing is
// ready and no virtual interrupt is due before the next field.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

#include "context.h"
#include "mpt/core/rng.h"
#include "sdk_internal.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#include <stdexcept>

namespace mpt::gc {

namespace detail {
namespace {

constexpr std::uint32_t kStateMagic = 0x4D50'4753;  // "MPGS"
constexpr std::uint32_t kStateVersion = 1;
constexpr std::uint64_t kSaveMagic = 0x3153'5343'4754'504Dull;  // "MPTGCSS1"
constexpr std::uint64_t kEpochStream = 0x4743'5254'4345'504Full;  // domain tag for the epoch draw
constexpr std::uint64_t kMaxResumesPerFrame = 50'000'000;

static_assert(sizeof(SdkState) <= kStateRegionBytes, "SdkState must fit its arena region");

Sdk::Impl* g_impl = nullptr;
std::int64_t g_earliest = std::numeric_limits<std::int64_t>::max();  // cache of the earliest event deadline
std::uint64_t g_resumes = 0;

std::size_t alignUp(std::size_t v, std::size_t a) { return (v + a - 1) & ~(a - 1); }

// Stack canary: the lowest bytes of every coroutine stack hold a fixed pattern.
// Stacks grow down, so an overflow overwrites it before anything else, and the
// check after every switch turns the overflow into a deterministic fault.
constexpr std::size_t kCanaryBytes = 256;
constexpr std::uint64_t kCanaryWord = 0x4D50'5443'414E'4152ull;  // "MPTCANAR"

void writeStackCanary(Sdk::Impl& I, std::uint32_t slot) {
  std::byte* lo = I.arena + I.st->threads[slot].stack_offset;
  for (std::size_t i = 0; i < kCanaryBytes; i += 8) std::memcpy(lo + i, &kCanaryWord, 8);
}

bool stackCanaryIntact(const Sdk::Impl& I, std::uint32_t slot) {
  const std::byte* lo = I.arena + I.st->threads[slot].stack_offset;
  for (std::size_t i = 0; i < kCanaryBytes; i += 8) {
    std::uint64_t w;
    std::memcpy(&w, lo + i, 8);
    if (w != kCanaryWord) return false;
  }
  return true;
}

// The context switch returns into other stacks, which CET shadow stacks reject.
bool userShadowStacksEnabled() {
#if defined(_WIN32)
  PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY policy{};
  if (!GetProcessMitigationPolicy(GetCurrentProcess(), ProcessUserShadowStackPolicy, &policy, sizeof policy))
    return false;  // older Windows: no user shadow stacks
  return policy.EnableUserShadowStack != 0;
#elif defined(__linux__) && defined(SYS_arch_prctl)
  constexpr int kArchShstkStatus = 0x5005;  // ARCH_SHSTK_STATUS
  unsigned long features = 0;
  if (syscall(SYS_arch_prctl, kArchShstkStatus, &features) != 0) return false;  // kernel without shadow stacks
  return (features & 1ul) != 0;                                                  // ARCH_SHSTK_SHSTK
#else
  return false;
#endif
}

std::byte* allocateArena(std::size_t size, std::uintptr_t base) {
#if defined(_WIN32)
  void* p = VirtualAlloc(reinterpret_cast<void*>(base), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (p && base && reinterpret_cast<std::uintptr_t>(p) != base) {
    VirtualFree(p, 0, MEM_RELEASE);
    return nullptr;
  }
  return static_cast<std::byte*>(p);
#else
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(MAP_FIXED_NOREPLACE)
  if (base) flags |= MAP_FIXED_NOREPLACE;
#endif
  void* p = mmap(reinterpret_cast<void*>(base), size, PROT_READ | PROT_WRITE, flags, -1, 0);
  if (p == MAP_FAILED) return nullptr;
  if (base && reinterpret_cast<std::uintptr_t>(p) != base) {
    munmap(p, size);
    return nullptr;
  }
  return static_cast<std::byte*>(p);
#endif
}

void freeArena(std::byte* p, std::size_t size) {
  if (!p) return;
#if defined(_WIN32)
  (void)size;
  VirtualFree(p, 0, MEM_RELEASE);
#else
  munmap(p, size);
#endif
}

void recomputeEarliest() {
  std::int64_t e = std::numeric_limits<std::int64_t>::max();
  for (const Event& ev : st().events)
    if (ev.used && ev.deadline < e) e = ev.deadline;
  g_earliest = e;
}

bool before(const Event& x, const Event& y) {
  if (x.deadline != y.deadline) return x.deadline < y.deadline;
  if (x.source != y.source) return x.source < y.source;
  return x.seq < y.seq;
}

int earliestDue(std::int64_t now) {
  int best = -1;
  auto& s = st();
  for (int i = 0; i < static_cast<int>(kMaxEvents); ++i) {
    const Event& e = s.events[i];
    if (!e.used || e.deadline > now) continue;
    if (best < 0 || before(e, s.events[best])) best = i;
  }
  return best;
}

// Runs every event due at or before `now`, in deadline order, in interrupt context.
void runDue() {
  auto& s = st();
  ++s.in_interrupt;
  for (;;) {
    const int i = earliestDue(s.now);
    if (i < 0) break;
    const Event e = s.events[i];
    s.events[i] = Event{};
    recomputeEarliest();
    dispatch(e);
    if (s.faulted || s.halted || s.reset_requested) break;
  }
  --s.in_interrupt;
}

int pickReady() {
  auto& s = st();
  int best = -1;
  for (int i = 0; i < static_cast<int>(kMaxThreads); ++i) {
    const ThreadSlot& t = s.threads[i];
    if (t.state != ThreadState::ready || t.suspend > 0) continue;
    if (best < 0) {
      best = i;
      continue;
    }
    const ThreadSlot& b = s.threads[best];
    if (t.priority < b.priority || (t.priority == b.priority && t.ready_seq < b.ready_seq)) best = i;
  }
  return best;
}

void threadEntry(void* arg) {
  const auto slot = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(arg));
  void* result = nullptr;
  if (slot == 0) {
    impl().main(impl().main_arg);
  } else if (slot == st().idle_slot) {
    // The idle thread: runs the idle function once per frame step, at the
    // lowest priority, then waits to be armed again (idleArm).
    for (;;) {
      auto& s = st();
      if (s.idle_fn) s.idle_fn(s.idle_param);
      ++s.idle_runs;
      ThreadSlot& t = s.threads[slot];
      t.state = ThreadState::waiting;
      t.wait_kind = WaitKind::idle;
      t.wait_key = nullptr;
      t.wait_seq = ++s.seq;
      switchToHost();
    }
  } else {
    ThreadSlot& t = st().threads[slot];
    if (t.func) result = t.func(t.param);
  }
  exitCurrent(result);
}

void irqEntry(void*) {
  for (;;) {
    runDue();
    switchToHost();
  }
}

void resumeSlot(std::int32_t slot) {
  auto& s = st();
  auto& I = impl();
  ThreadSlot& t = s.threads[slot];
  s.current = slot;
  if (slot != static_cast<std::int32_t>(kIrqSlot)) {
    t.state = ThreadState::running;
    t.started = 1;
  }
  ++g_resumes;
  mpt_gc_switch_context(&I.host_sp, t.ctx_sp);
  s.current = -1;
  // Every switch comes back here, so this checks the stack that just ran.
  if (!stackCanaryIntact(I, static_cast<std::uint32_t>(slot)) && !s.faulted) {
    s.faulted = 1;
    I.fault = "stack overflow in SDK thread slot " + std::to_string(slot) + " (its " +
              std::to_string(I.config.thread_stack_bytes / 1024) +
              " KiB arena stack; raise SdkConfig::thread_stack_bytes)";
  }
}

void runUntilIdle() {
  auto& s = st();
  for (;;) {
    if (s.faulted || s.halted || s.reset_requested) return;
    if (g_resumes > kMaxResumesPerFrame) {
      s.faulted = 1;
      impl().fault = "scheduler: more than 50M context switches in one frame (threads yield to each other forever)";
      return;
    }
    if (s.now >= g_earliest) {
      resumeSlot(static_cast<std::int32_t>(kIrqSlot));
      continue;
    }
    const int next = pickReady();
    if (next >= 0) {
      resumeSlot(next);
      continue;
    }
    // Idle: jump to the next interrupt if it falls inside this field.
    if (g_earliest < s.next_field) {
      s.now = g_earliest;
      continue;
    }
    return;
  }
}

void initSlotContext(Sdk::Impl& I, std::uint32_t slot, ContextEntry entry) {
  ThreadSlot& t = I.st->threads[slot];
  t.stack_size = I.config.thread_stack_bytes;
  t.stack_offset = static_cast<std::uint32_t>(I.stacks_offset + std::size_t{slot} * I.config.thread_stack_bytes);
  writeStackCanary(I, slot);
  t.ctx_sp = make_context(I.arena + t.stack_offset, t.stack_size, entry,
                          reinterpret_cast<void*>(static_cast<std::uintptr_t>(slot)));
}

void hashEvent(StateHasher& h, const Event& e) {
  h.update_u64(static_cast<std::uint64_t>(e.deadline));
  h.update_u64(e.seq);
  h.update_u64(static_cast<std::uint64_t>(e.source));
  h.update_u64(static_cast<std::uint64_t>(e.kind));
  // a is a game object (alarm, file info, request); b is a callback.
  h.update_u64(hashPointer(e.a));
  h.update_u64(e.kind == EventKind::host_tick ? hashPointer(e.b) : hashFunction(e.b));
  h.update_u64(static_cast<std::uint64_t>(e.c));
}

}  // namespace

Sdk::Impl& impl() {
  if (!g_impl) throw std::logic_error("GameCube SDK call with no live Sdk");
  return *g_impl;
}

SdkState& st() { return *impl().st; }
bool active() { return g_impl != nullptr; }

std::uint64_t hashPointer(const void* p) {
  if (!p) return 0;
  const auto& I = impl();
  if (I.inArena(p)) return 0x8000'0000'0000'0000ull | I.offsetOf(p);
  return 0xFFFF'FFFF'FFFF'FFFFull;
}

void switchToHost() {
  auto& s = st();
  const std::int32_t slot = s.current;
  mpt_gc_switch_context(&s.threads[slot].ctx_sp, impl().host_sp);
}

void boundary() {
  if (!active()) return;
  auto& s = st();
  if (s.current < 0 || s.current == static_cast<std::int32_t>(kIrqSlot) || s.in_interrupt) return;
  if (s.now >= g_earliest) runDue();
  if (s.faulted || s.halted || s.reset_requested) switchToHost();
  maybePreempt();
}

std::int64_t timeQuery() {
  auto& s = st();
  s.now += kTimeQueryQuantumTicks;
  ++s.time_queries;
  if (s.now - s.field_start > kWatchdogTicks)
    fatal("busy-wait watchdog: a frame ran more than 60 virtual seconds of time queries without yielding");
  boundary();
  return s.now;
}

std::int32_t currentSlot() { return st().current; }

void maybePreempt() {
  auto& s = st();
  if (s.current < 0 || s.current == static_cast<std::int32_t>(kIrqSlot) || s.in_interrupt || s.scheduler_disabled)
    return;
  ThreadSlot& cur = s.threads[s.current];
  const int best = pickReady();
  if (best >= 0 && s.threads[best].priority < cur.priority) {
    makeReady(cur);
    switchToHost();
  }
}

void makeReady(ThreadSlot& t) {
  t.state = ThreadState::ready;
  t.ready_seq = ++st().seq;
}

static void updateQueueStruct(const void* key) {
  auto& s = st();
  auto* q = const_cast<OSThreadQueue*>(static_cast<const OSThreadQueue*>(key));
  int head = -1, tail = -1;
  for (int i = 0; i < static_cast<int>(kMaxThreads); ++i) {
    const ThreadSlot& t = s.threads[i];
    if (t.state != ThreadState::waiting || t.wait_kind != WaitKind::queue || t.wait_key != key) continue;
    if (head < 0 || t.wait_seq < s.threads[head].wait_seq) head = i;
    if (tail < 0 || t.wait_seq > s.threads[tail].wait_seq) tail = i;
  }
  q->head = head < 0 ? nullptr : threadOf(head);
  q->tail = tail < 0 ? nullptr : threadOf(tail);
}

void block(WaitKind kind, const void* key) {
  auto& s = st();
  if (s.current < 0 || s.current == static_cast<std::int32_t>(kIrqSlot) || s.in_interrupt)
    fatal("blocking SDK call outside a thread or inside an interrupt handler");
  ThreadSlot& t = s.threads[s.current];
  t.state = ThreadState::waiting;
  t.wait_kind = kind;
  t.wait_key = key;
  t.wait_seq = ++s.seq;
  if (kind == WaitKind::queue) updateQueueStruct(key);
  switchToHost();
}

int wakeAll(WaitKind kind, const void* key) {
  auto& s = st();
  std::int32_t order[kMaxThreads];
  int n = 0;
  for (int i = 0; i < static_cast<int>(kMaxThreads); ++i) {
    const ThreadSlot& t = s.threads[i];
    if (t.state == ThreadState::waiting && t.wait_kind == kind && t.wait_key == key) order[n++] = i;
  }
  std::sort(order, order + n, [&](std::int32_t a, std::int32_t b) {
    return s.threads[a].wait_seq < s.threads[b].wait_seq;
  });
  for (int k = 0; k < n; ++k) {
    ThreadSlot& t = s.threads[order[k]];
    t.wait_kind = WaitKind::none;
    t.wait_key = nullptr;
    makeReady(t);
  }
  if (kind == WaitKind::queue && n > 0) updateQueueStruct(key);
  return n;
}

ThreadSlot* slotFor(const OSThread* thread) {
  auto& s = st();
  if (!thread) return nullptr;
  for (std::uint32_t i = 0; i < kMaxThreads; ++i)
    if (s.threads[i].state != ThreadState::free && s.threads[i].user == thread) return &s.threads[i];
  return nullptr;
}

OSThread* threadOf(std::int32_t slot) {
  if (slot < 0) return nullptr;
  if (slot == static_cast<std::int32_t>(kIrqSlot)) return reinterpret_cast<OSThread*>(st().idle_thread_storage);
  return st().threads[slot].user;
}

void exitCurrent(void* val) {
  auto& s = st();
  if (s.current < 0 || s.current == static_cast<std::int32_t>(kIrqSlot)) fatal("OSExitThread outside a thread");
  ThreadSlot& t = s.threads[s.current];
  // Release mutexes the thread still holds, as the SDK does.
  for (MutexSlot& m : s.mutexes) {
    if (m.key && m.owner == s.current) {
      m.owner = -1;
      m.count = 0;
      wakeAll(WaitKind::mutex, m.key);
    }
  }
  t.exit_val = val;
  if (s.current == 0) {
    s.halted = 1;
    t.state = ThreadState::moribund;
  } else {
    t.state = t.detached ? ThreadState::free : ThreadState::moribund;
    wakeAll(WaitKind::join, t.user);
  }
  switchToHost();
  fatal("a terminated thread was resumed");
}

static bool insertEvent(std::int64_t deadline, Source source, EventKind kind, void* a, void* b, std::int64_t c) {
  auto& s = st();
  for (Event& e : s.events) {
    if (e.used) continue;
    e = Event{deadline, ++s.seq, source, kind, 1, a, b, c};
    if (deadline < g_earliest) g_earliest = deadline;
    return true;
  }
  return false;
}

void schedule(std::int64_t deadline, Source source, EventKind kind, void* a, void* b, std::int64_t c) {
  if (!insertEvent(deadline, source, kind, a, b, c)) fatal("virtual interrupt queue full (256 pending events)");
}

void cancelEvents(EventKind kind, const void* a) {
  for (Event& e : st().events)
    if (e.used && e.kind == kind && e.a == a) e = Event{};
  recomputeEarliest();
}

void completeIo(Source source, EventKind kind, void* a, void* b, std::int64_t c) {
  auto& s = st();
  if (impl().config.io_callbacks_before_return) {
    ++s.in_interrupt;
    dispatch(Event{s.now, ++s.seq, source, kind, 1, a, b, c});
    --s.in_interrupt;
  } else {
    schedule(s.now, source, kind, a, b, c);
  }
}

void initThreadContext(std::uint32_t slot) { initSlotContext(impl(), slot, &threadEntry); }

void idleArm() {
  auto& s = st();
  if (!s.idle_fn || s.idle_slot < 0) return;
  ThreadSlot& t = s.threads[s.idle_slot];
  if (t.state != ThreadState::waiting || t.wait_kind != WaitKind::idle) return;  // still busy from an earlier frame
  t.wait_kind = WaitKind::none;
  makeReady(t);
}

void dispatch(const Event& e) {
  switch (e.kind) {
    case EventKind::retrace:
      viRetrace();
      break;
    case EventKind::alarm:
      alarmFire(e);
      break;
    case EventKind::dvd_read:
    case EventKind::dvd_cancel:
      dvdDispatch(e);
      break;
    case EventKind::ar_dma:
    case EventKind::arq:
      arDispatch(e);
      break;
    case EventKind::card:
      cardDispatch(e);
      break;
    case EventKind::ai_dma:
      aiDispatch(e);
      break;
    case EventKind::host_tick:
      reinterpret_cast<void (*)(void*)>(e.b)(e.a);
      break;
    case EventKind::none:
      break;
  }
}

void fatal(const std::string& message) {
  if (active()) {
    auto& I = impl();
    auto& s = st();
    if (!s.faulted) I.fault = message;
    s.faulted = 1;
    if (s.current >= 0) {
      switchToHost();     // a faulted runtime never resumes its threads
      __builtin_trap();   // unreachable
    }
    // Host context: only reachable by calling the C API from host code instead
    // of game code, which is a programming error (the frame step itself never
    // calls fatal). The fault is recorded; the exception reports the misuse.
    throw std::logic_error("GameCube SDK C API called from host code: " + message);
  }
  throw std::logic_error("GameCube SDK fault with no live Sdk: " + message);
}

void report(const char* text) {
  auto& I = impl();
  if (I.report_sink) {
    I.report_sink(text);
  } else {
    std::fputs(text, stderr);
  }
}

}  // namespace detail

using namespace detail;

std::uint64_t hashGamePointer(const void* p) { return hashPointer(p); }
std::uint64_t hashGameFunction(const void* f) { return hashFunction(f); }

Sdk::Sdk(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Sdk::~Sdk() {
  if (impl_) {
    freeArena(impl_->arena, impl_->arena_size);
    if (g_impl == impl_.get()) g_impl = nullptr;
  }
}

Result Sdk::create(const SdkConfig& config, MainFn main, void* arg, std::unique_ptr<Sdk>& out) {
  if (g_impl) return Result::failure("only one GameCube SDK instance may exist at a time");
  if (!main) return Result::failure("no main function");
  if (userShadowStacksEnabled())
    return Result::failure(
        "CET user shadow stacks are enabled for this process; the SDK's coroutine switch is incompatible with them. "
        "Link the host executable with /CETCOMPAT:NO (Windows) or without shadow-stack marking (-fcf-protection=none).");
  if (config.thread_stack_bytes < 64 * 1024 || config.thread_stack_bytes % 4096 != 0)
    return Result::failure("thread_stack_bytes must be a multiple of 4096 and at least 64 KiB");
  for (const auto& region : config.state_regions)
    if (region.empty() || !region.data()) return Result::failure("empty registered state region");
  if (config.card.size_mbits != 4 && config.card.size_mbits != 8 && config.card.size_mbits != 16)
    return Result::failure("card size must be 4, 8 or 16 Mbit");

  auto I = std::make_unique<Impl>();
  I->config = config;
  I->main = main;
  I->main_arg = arg;

  constexpr std::size_t kAlign = 64 * 1024;
  std::size_t off = alignUp(kStateRegionBytes, kAlign);
  I->main_offset = off;
  off = alignUp(off + kMainRamBytes, kAlign);
  I->aram_offset = off;
  off = alignUp(off + kAramBytes, kAlign);
  I->card_offset = off;
  off = alignUp(off + kCardRegionBytes, kAlign);
  I->stacks_offset = off;
  off = alignUp(off + std::size_t{kSlotCount} * config.thread_stack_bytes, kAlign);
  I->arena_size = off;
  if (off > 0xFFFF'0000ull) return Result::failure("arena too large for 32-bit offsets");

  I->arena = allocateArena(I->arena_size, config.arena_base);
  if (!I->arena) return Result::failure("could not reserve the SDK arena at the requested address");
  std::memset(I->arena, 0, I->arena_size);  // fresh pages are zero; be explicit
  I->st = new (I->arena) SdkState{};

  g_impl = I.get();
  g_earliest = std::numeric_limits<std::int64_t>::max();
  auto& s = *I->st;
  s.magic = kStateMagic;
  s.version = kStateVersion;

  // Epoch: a valid date in [2001-01-01, 2031-01-01), drawn from the session seed (ADR 0008 §2).
  constexpr std::int64_t kTicksPerDay = 86400ll * kTimerClockHz;
  constexpr std::int64_t kLo = 366 * kTicksPerDay;    // 2001-01-01
  constexpr std::int64_t kHi = 11323 * kTicksPerDay;  // 2031-01-01
  DeterministicRng rng(config.seed ^ kEpochStream);
  s.epoch = kLo + static_cast<std::int64_t>(rng.below(static_cast<std::uint64_t>(kHi - kLo)));
  s.now = s.epoch;

  // Field period: kTimerClockHz * den / num ticks, as whole + num/den.
  const auto [rate_num, rate_den] = config.video == VideoStandard::ntsc ? std::pair<std::int64_t, std::int64_t>{60000, 1001}
                                                                        : std::pair<std::int64_t, std::int64_t>{50, 1};
  const std::int64_t period_num = std::int64_t{kTimerClockHz} * rate_den;
  s.field_whole = period_num / rate_num;
  s.field_num = period_num % rate_num;
  s.field_den = rate_num;
  s.field_acc = 0;
  s.field_start = s.epoch;
  s.next_field = s.epoch + s.field_whole;  // field_num is 0 for both standards today; kept general
  s.field_acc = s.field_num;
  if (s.field_acc >= s.field_den) {
    s.next_field += s.field_acc / s.field_den;
    s.field_acc %= s.field_den;
  }

  s.current = -1;
  s.interrupts_enabled = 1;
  s.sound_mode = OS_SOUND_MODE_STEREO;
  s.progressive = 0;
  s.current_heap = -1;
  s.arena_lo = static_cast<std::uint32_t>(I->main_offset + kLowMemoryBytes);
  s.arena_hi = static_cast<std::uint32_t>(I->main_offset + kMainRamBytes);
  s.tv_format = config.video == VideoStandard::ntsc ? VI_NTSC : VI_PAL;
  s.dvd_auto_invalidation = 1;
  s.idle_slot = -1;
  s.arq_chunk_size = 4096;
  for (auto& m : s.mutexes) m.owner = -1;

  // Root coroutine (the default thread, priority 16 as on the console) and the
  // interrupt coroutine.
  ThreadSlot& root = s.threads[0];
  root.user = reinterpret_cast<OSThread*>(s.default_thread_storage);
  root.priority = 16;
  initSlotContext(*I, 0, &threadEntry);
  makeReady(root);
  initSlotContext(*I, kIrqSlot, &irqEntry);
  s.threads[kIrqSlot].state = ThreadState::ready;
  s.threads[kIrqSlot].priority = -1;

  dvdBuildIndex(*I);
  if (Result r = cardBoot(*I); !r) {
    g_impl = nullptr;
    freeArena(I->arena, I->arena_size);
    return r;
  }
  osSyncGlobalsFromState();
  aiBoot(*I);

  out.reset(new Sdk(std::move(I)));
  return Result::success();
}

StepStatus Sdk::stepFrame(const InputFrame& input) {
  auto& s = *impl_->st;
  auto status = [&] {
    if (s.faulted) return StepStatus::faulted;
    if (s.reset_requested) return StepStatus::reset_requested;
    if (s.halted) return StepStatus::halted;
    return StepStatus::running;
  };
  if (status() != StepStatus::running) return status();
  if (input.frame != s.frame) {
    s.faulted = 1;
    impl_->fault = "stepFrame: input.frame does not match the frame counter";
    return StepStatus::faulted;
  }

  // Nothing on this host-side path calls fatal(): faults here are recorded and
  // reported through the returned status, never by aborting.
  g_resumes = 0;
  padStage(input);
  if (s.frame == 0) {
    padLatch();
  } else {
    s.field_start = s.next_field;
    s.field_acc += s.field_num;
    s.next_field = s.field_start + s.field_whole + s.field_acc / s.field_den;
    s.field_acc %= s.field_den;
    if (s.now < s.field_start) s.now = s.field_start;  // the field clock is a floor
    if (!insertEvent(s.field_start, Source::vi, EventKind::retrace, nullptr, nullptr, 0)) {
      s.faulted = 1;
      impl_->fault = "virtual interrupt queue full (256 pending events) at the retrace";
    }
  }
  idleArm();
  runUntilIdle();
  ++s.frame;
  return status();
}

std::uint64_t Sdk::frame() const { return impl_->st->frame; }
const std::string& Sdk::fault() const { return impl_->fault; }
std::int64_t Sdk::now() const { return impl_->st->now; }
std::int64_t Sdk::epoch() const { return impl_->st->epoch; }
std::byte* Sdk::arenaBase() const { return impl_->arena; }
std::size_t Sdk::arenaSize() const { return impl_->arena_size; }
std::span<std::byte> Sdk::mainRam() const { return {impl_->mainRam(), kMainRamBytes}; }
std::span<std::byte> Sdk::aram() const { return {impl_->aramBase(), kAramBytes}; }
std::vector<std::int16_t> Sdk::takeAudio() { return std::exchange(impl_->pcm, {}); }
std::uint32_t Sdk::audioRate() const { return impl_->st->ai.dsp_rate == AI_SAMPLERATE_48KHZ ? kAiRate48k : kAiRate32k; }
std::uint64_t Sdk::audioFrames() const { return impl_->st->ai.frames_out; }
void Sdk::setReportSink(std::function<void(const char*)> sink) { impl_->report_sink = std::move(sink); }
std::uint64_t Sdk::saveMediaGeneration() const { return impl_->st->card.generation; }
SaveMedia Sdk::saveMedia() const { return SaveMedia{card_image_format_version(), cardSerialize()}; }

FrameRate Sdk::frameRate() const {
  return impl_->config.video == VideoStandard::ntsc ? FrameRate{60000, 1001} : FrameRate{50, 1};
}

std::array<std::uint32_t, 4> Sdk::rumble() const {
  const auto& m = impl_->st->motor;
  return {m[0], m[1], m[2], m[3]};
}

void Sdk::hashState(StateHasher& h) const {
  const SdkState& s = *impl_->st;
  h.update_u64(0x4D50'5447'4353'444Bull);  // manifest tag "MPTGCSDK", version 1
  h.update_u64(static_cast<std::uint64_t>(s.epoch));
  h.update_u64(static_cast<std::uint64_t>(s.now));
  h.update_u64(static_cast<std::uint64_t>(s.field_start));
  h.update_u64(static_cast<std::uint64_t>(s.next_field));
  h.update_u64(static_cast<std::uint64_t>(s.field_acc));
  h.update_u64(s.frame);
  h.update_u64(s.time_queries);
  h.update_u64(s.retrace_count);
  h.update_u64(s.halted | (s.faulted << 8) | (s.reset_requested << 16));
  h.update_u64(s.seq);

  for (std::uint32_t i = 0; i < kSlotCount; ++i) {
    const ThreadSlot& t = s.threads[i];
    h.update_u64(static_cast<std::uint64_t>(t.state) | (static_cast<std::uint64_t>(t.wait_kind) << 8) |
                 (std::uint64_t{t.detached} << 16) | (std::uint64_t{t.started} << 24));
    if (t.state == ThreadState::free) continue;
    h.update_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(t.priority)));
    h.update_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(t.suspend)));
    h.update_u64(t.ready_seq);
    h.update_u64(t.wait_seq);
    h.update_u64(hashPointer(t.wait_key));
    h.update_u64(hashPointer(t.user));
    h.update_u64(hashFunction(reinterpret_cast<const void*>(t.func)));
    h.update_u64(hashPointer(t.param));
    h.update_u64(hashPointer(t.exit_val));
  }
  h.update_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(s.current)));
  h.update_u64(static_cast<std::uint64_t>(s.scheduler_disabled));
  h.update_u64(static_cast<std::uint64_t>(s.interrupts_enabled));

  // Pending interrupts in dispatch order, independent of slot positions.
  std::vector<const Event*> pending;
  for (const Event& e : s.events)
    if (e.used) pending.push_back(&e);
  std::sort(pending.begin(), pending.end(), [](const Event* a, const Event* b) { return before(*a, *b); });
  h.update_u64(pending.size());
  for (const Event* e : pending) hashEvent(h, *e);

  for (const MessageQueueSlot& q : s.mqs) {
    if (!q.key) continue;
    h.update_u64(hashPointer(q.key));
    h.update_u64(hashPointer(q.msgs));
    h.update_u64(static_cast<std::uint64_t>(q.count) | (static_cast<std::uint64_t>(q.first) << 20) |
                 (static_cast<std::uint64_t>(q.used) << 40));
    for (std::int32_t k = 0; k < q.used; ++k) h.update_u64(hashPointer(q.msgs[(q.first + k) % q.count]));
  }
  for (const AlarmSlot& a : s.alarms) {
    if (!a.key) continue;
    h.update_u64(hashPointer(a.key));
    h.update_u64(hashFunction(reinterpret_cast<const void*>(a.handler)));
    h.update_u64(static_cast<std::uint64_t>(a.fire));
    h.update_u64(static_cast<std::uint64_t>(a.period));
    h.update_u64(a.tag | (std::uint64_t{a.armed} << 32));
  }
  for (const MutexSlot& m : s.mutexes) {
    if (!m.key) continue;
    h.update_u64(hashPointer(m.key));
    h.update_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(m.owner)));
    h.update_u64(static_cast<std::uint64_t>(m.count));
  }

  h.update_u64(s.arena_lo);
  h.update_u64(s.arena_hi);
  h.update_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(s.current_heap)));
  heapHash(h);
  h.update_u64(s.sound_mode | (std::uint64_t{s.progressive} << 32));

  h.update_u64(hashFunction(reinterpret_cast<const void*>(s.pre_retrace)));
  h.update_u64(hashFunction(reinterpret_cast<const void*>(s.post_retrace)));
  h.update_u64(hashPointer(s.next_fb));
  h.update_u64(hashPointer(s.current_fb));
  h.update_u64(hashPointer(s.flushed_fb));
  h.update_u64(s.tv_format | (std::uint64_t{s.black} << 32) | (std::uint64_t{s.black_flushed} << 40));

  h.update_u64(s.pad_initialized);
  for (const PADStatus& p : s.latched) {
    h.update_u64(p.button | (std::uint64_t{static_cast<std::uint8_t>(p.stickX)} << 16) |
                 (std::uint64_t{static_cast<std::uint8_t>(p.stickY)} << 24) |
                 (std::uint64_t{static_cast<std::uint8_t>(p.substickX)} << 32) |
                 (std::uint64_t{static_cast<std::uint8_t>(p.substickY)} << 40) | (std::uint64_t{p.triggerL} << 48) |
                 (std::uint64_t{p.triggerR} << 56));
    h.update_u64(p.analogA | (std::uint64_t{p.analogB} << 8) | (std::uint64_t{static_cast<std::uint8_t>(p.err)} << 16));
  }
  for (std::uint32_t m : s.motor) h.update_u64(m);
  h.update_u64(s.pad_spec | (std::uint64_t{s.pad_analog_mode} << 32));

  h.update_u64(s.dvd_cwd | (std::uint64_t{s.dvd_initialized} << 32) | (std::uint64_t{s.dvd_auto_invalidation} << 40));

  h.update_u64(s.ar_initialized | (std::uint64_t{s.arq_initialized} << 8));
  h.update_u64(s.ar_top);
  h.update_u64(s.ar_stack_depth);
  for (std::uint32_t k = 0; k < s.ar_stack_depth; ++k) h.update_u64(s.ar_stack[k]);
  h.update_u64(hashFunction(reinterpret_cast<const void*>(s.ar_dma_callback)));
  h.update_u64(s.arq_chunk_size);

  cardHash(h);

  aiHash(h);
  h.update_u64(s.thp_initialized);
  h.update_u64(hashFunction(reinterpret_cast<const void*>(s.idle_fn)));
  h.update_u64(hashPointer(s.idle_param));
  h.update_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(s.idle_slot)));
  h.update_u64(s.idle_runs);
}

std::uint64_t Sdk::stateHash() const {
  StateHasher h;
  hashState(h);
  return h.digest();
}

namespace {
struct SaveHeader {
  std::uint64_t magic;
  std::uint64_t state_size;
  std::uint64_t arena_size;
  std::uint64_t arena_base;
  std::uint64_t code_anchor;  // detects a different build or module base
  std::uint64_t stack_bytes;
  std::uint64_t region_count;  // registered globals, appended after the arena
  std::uint64_t region_bytes;
};

std::size_t regionBytes(const SdkConfig& c) {
  std::size_t n = 0;
  for (const auto& r : c.state_regions) n += r.size();
  return n;
}
}  // namespace

std::vector<std::byte> Sdk::saveState() const {
  SaveHeader h{kSaveMagic,
               sizeof(SdkState),
               impl_->arena_size,
               reinterpret_cast<std::uintptr_t>(impl_->arena),
               reinterpret_cast<std::uintptr_t>(&threadEntry),
               impl_->config.thread_stack_bytes,
               impl_->config.state_regions.size(),
               regionBytes(impl_->config)};
  std::vector<std::byte> out(sizeof h + impl_->arena_size + h.region_bytes);
  std::memcpy(out.data(), &h, sizeof h);
  std::byte* at = out.data() + sizeof h;
  std::memcpy(at, impl_->arena, impl_->arena_size);
  at += impl_->arena_size;
  for (const auto& r : impl_->config.state_regions) {
    std::memcpy(at, r.data(), r.size());
    at += r.size();
  }
  return out;
}

Result Sdk::loadState(std::span<const std::byte> state) {
  if (impl_->st->current >= 0) return Result::failure("loadState inside a frame step");
  SaveHeader h{};
  if (state.size() < sizeof h) return Result::failure("save state too small");
  std::memcpy(&h, state.data(), sizeof h);
  if (h.magic != kSaveMagic) return Result::failure("not a GameCube SDK save state");
  if (h.state_size != sizeof(SdkState) || h.arena_size != impl_->arena_size ||
      h.stack_bytes != impl_->config.thread_stack_bytes || h.region_count != impl_->config.state_regions.size() ||
      h.region_bytes != regionBytes(impl_->config))
    return Result::failure("save state from a different SDK build or configuration");
  if (h.arena_base != reinterpret_cast<std::uintptr_t>(impl_->arena) ||
      h.code_anchor != reinterpret_cast<std::uintptr_t>(&threadEntry))
    return Result::failure(
        "save state needs the same arena and code addresses (same build, same process or fixed bases; ADR 0008 §10-11)");
  if (state.size() != sizeof h + impl_->arena_size + h.region_bytes)
    return Result::failure("save state has the wrong size");
  const std::byte* at = state.data() + sizeof h;
  std::memcpy(impl_->arena, at, impl_->arena_size);
  at += impl_->arena_size;
  for (const auto& r : impl_->config.state_regions) {
    std::memcpy(r.data(), at, r.size());
    at += r.size();
  }
  impl_->pcm.clear();
  impl_->fault.clear();
  recomputeEarliest();
  osSyncGlobalsFromState();
  return Result::success();
}

}  // namespace mpt::gc
