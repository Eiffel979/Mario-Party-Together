// OS C API: time, interrupts, threads, message queues, mutexes, alarms,
// settings, reporting, fast casts and cache no-ops.
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "sdk_internal.h"

using namespace mpt::gc;
using namespace mpt::gc::detail;

namespace mpt::gc::detail {

namespace {

AlarmSlot* alarmFor(const void* key, bool create) {
  auto& s = st();
  AlarmSlot* free_slot = nullptr;
  for (AlarmSlot& a : s.alarms) {
    if (a.key == key) return &a;
    if (!a.key && !free_slot) free_slot = &a;
  }
  if (!create) return nullptr;
  if (!free_slot) fatal("alarm table full (128 alarms)");
  *free_slot = AlarmSlot{};
  free_slot->key = key;
  return free_slot;
}

MessageQueueSlot* mqFor(const void* key) {
  for (MessageQueueSlot& q : st().mqs)
    if (q.key == key) return &q;
  fatal("message queue used before OSInitMessageQueue");
}

MutexSlot* mutexFor(const void* key, bool create) {
  MutexSlot* free_slot = nullptr;
  for (MutexSlot& m : st().mutexes) {
    if (m.key == key) return &m;
    if (!m.key && !free_slot) free_slot = &m;
  }
  if (!create) fatal("mutex used before OSInitMutex");
  if (!free_slot) fatal("mutex table full (64 mutexes)");
  *free_slot = MutexSlot{key, -1, 0};
  return free_slot;
}

ThreadSlot& currentThreadSlot() {
  auto& s = st();
  if (s.current < 0 || s.current == static_cast<std::int32_t>(kIrqSlot)) fatal("thread call outside a thread");
  return s.threads[s.current];
}

void armAlarm(OSAlarm* alarm, std::int64_t fire, std::int64_t period, OSAlarmHandler handler) {
  if (!handler) fatal("OSSetAlarm with no handler");
  AlarmSlot* a = alarmFor(alarm, true);
  cancelEvents(EventKind::alarm, alarm);
  a->handler = handler;
  a->fire = fire;
  a->period = period;
  a->armed = 1;
  schedule(fire, Source::alarm, EventKind::alarm, alarm, nullptr, 0);
}

// Days from 2000-01-01 to year-01-01.
std::int64_t daysBeforeYear(int year) {
  std::int64_t days = 0;
  for (int y = 2000; y < year; ++y) days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
  for (int y = year; y < 2000; ++y) days -= (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
  return days;
}

bool isLeap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
constexpr int kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

}  // namespace

void alarmFire(const Event& e) {
  AlarmSlot* a = alarmFor(e.a, false);
  if (!a || !a->armed) return;
  OSAlarmHandler handler = a->handler;
  if (a->period > 0) {
    a->fire += a->period;
    schedule(a->fire, Source::alarm, EventKind::alarm, e.a, nullptr, 0);
  } else {
    a->armed = 0;
  }
  handler(static_cast<OSAlarm*>(e.a), reinterpret_cast<OSContext*>(st().dummy_context));
}

}  // namespace mpt::gc::detail

namespace {
template <typename T>
T saturate(f32 v, f32 lo, f32 hi) {
  if (std::isnan(v)) return 0;
  const f32 t = std::trunc(v);
  if (t <= lo) return static_cast<T>(lo);
  if (t >= hi) return static_cast<T>(hi);
  return static_cast<T>(t);
}
}  // namespace

extern "C" {

// ---- Time ---------------------------------------------------------------------------

OSTime OSGetTime(void) { return timeQuery(); }

OSTick OSGetTick(void) { return static_cast<OSTick>(static_cast<std::uint64_t>(timeQuery())); }

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* td) {
  std::int64_t total_us = 0;
  std::int64_t secs = ticks / OS_TIMER_CLOCK;
  std::int64_t rem = ticks % OS_TIMER_CLOCK;
  if (rem < 0) {
    rem += OS_TIMER_CLOCK;
    --secs;
  }
  total_us = rem * 8 / (OS_TIMER_CLOCK / 125000);
  td->msec = static_cast<int>(total_us / 1000);
  td->usec = static_cast<int>(total_us % 1000);
  std::int64_t days = secs / 86400;
  std::int64_t sod = secs % 86400;
  if (sod < 0) {
    sod += 86400;
    --days;
  }
  td->hour = static_cast<int>(sod / 3600);
  td->min = static_cast<int>(sod / 60 % 60);
  td->sec = static_cast<int>(sod % 60);
  td->wday = static_cast<int>(((days % 7) + 7 + 6) % 7);  // 2000-01-01 was a Saturday
  int year = 2000;
  while (days < 0) {
    --year;
    days += isLeap(year) ? 366 : 365;
  }
  while (days >= (isLeap(year) ? 366 : 365)) {
    days -= isLeap(year) ? 366 : 365;
    ++year;
  }
  td->year = year;
  td->yday = static_cast<int>(days);
  int mon = 0;
  for (; mon < 12; ++mon) {
    const int md = kMonthDays[mon] + (mon == 1 && isLeap(year) ? 1 : 0);
    if (days < md) break;
    days -= md;
  }
  td->mon = mon;
  td->mday = static_cast<int>(days) + 1;
}

OSTime OSCalendarTimeToTicks(const OSCalendarTime* td) {
  std::int64_t year = td->year + td->mon / 12;
  int mon = td->mon % 12;
  if (mon < 0) {
    mon += 12;
    --year;
  }
  std::int64_t days = daysBeforeYear(static_cast<int>(year));
  for (int m = 0; m < mon; ++m) days += kMonthDays[m] + (m == 1 && isLeap(static_cast<int>(year)) ? 1 : 0);
  days += td->mday - 1;
  const std::int64_t secs = days * 86400 + td->hour * 3600ll + td->min * 60ll + td->sec;
  return secs * OS_TIMER_CLOCK + OSMillisecondsToTicks(static_cast<OSTime>(td->msec)) +
         OSMicrosecondsToTicks(static_cast<OSTime>(td->usec));
}

// ---- Interrupts -----------------------------------------------------------------------

BOOL OSDisableInterrupts(void) {
  boundary();
  auto& s = st();
  const BOOL old = s.interrupts_enabled;
  s.interrupts_enabled = 0;
  return old;
}

BOOL OSEnableInterrupts(void) {
  boundary();
  auto& s = st();
  const BOOL old = s.interrupts_enabled;
  s.interrupts_enabled = 1;
  return old;
}

BOOL OSRestoreInterrupts(BOOL level) {
  boundary();
  auto& s = st();
  const BOOL old = s.interrupts_enabled;
  s.interrupts_enabled = level ? 1 : 0;
  return old;
}

// ---- Threads --------------------------------------------------------------------------

void OSInitThreadQueue(OSThreadQueue* queue) {
  boundary();
  queue->head = nullptr;
  queue->tail = nullptr;
}

OSThread* OSGetCurrentThread(void) {
  boundary();
  return threadOf(currentSlot());
}

BOOL OSCreateThread(OSThread* thread, OSThreadFunc func, void* param, void* stackBase, u32 stackSize,
                    OSPriority priority, u16 attr) {
  (void)stackBase;
  (void)stackSize;
  boundary();
  if (!thread || priority < OS_PRIORITY_MIN || priority > OS_PRIORITY_MAX) return FALSE;
  auto& s = st();
  if (ThreadSlot* existing = slotFor(thread)) {
    if (existing->state != ThreadState::moribund) return FALSE;
    existing->state = ThreadState::free;
  }
  for (std::uint32_t i = 1; i < kMaxThreads; ++i) {
    ThreadSlot& t = s.threads[i];
    if (t.state != ThreadState::free) continue;
    t = ThreadSlot{};
    t.user = thread;
    t.func = func;
    t.param = param;
    t.priority = priority;
    t.suspend = 1;  // created suspended
    t.detached = (attr & OS_THREAD_ATTR_DETACH) ? 1 : 0;
    initThreadContext(i);
    makeReady(t);
    return TRUE;
  }
  fatal("thread table full (32 SDK threads)");
}

void OSExitThread(void* val) {
  boundary();
  exitCurrent(val);
}

void OSCancelThread(OSThread* thread) {
  boundary();
  auto& s = st();
  ThreadSlot* t = slotFor(thread);
  if (!t || t->state == ThreadState::moribund) return;
  if (t == &s.threads[0]) fatal("OSCancelThread on the default thread");
  if (s.current >= 0 && t == &s.threads[s.current]) exitCurrent(nullptr);
  for (MutexSlot& m : s.mutexes) {
    if (m.key && m.owner == static_cast<std::int32_t>(t - s.threads)) {
      m.owner = -1;
      m.count = 0;
      wakeAll(WaitKind::mutex, m.key);
    }
  }
  t->exit_val = nullptr;
  t->wait_kind = WaitKind::none;
  t->wait_key = nullptr;
  t->state = t->detached ? ThreadState::free : ThreadState::moribund;
  if (!t->detached) wakeAll(WaitKind::join, thread);
  maybePreempt();
}

BOOL OSJoinThread(OSThread* thread, void** val) {
  boundary();
  ThreadSlot* t = slotFor(thread);
  if (!t || t->detached) return FALSE;
  while (t->state != ThreadState::moribund) {
    block(WaitKind::join, thread);
    t = slotFor(thread);
    if (!t) return FALSE;
  }
  if (val) *val = t->exit_val;
  t->state = ThreadState::free;
  return TRUE;
}

void OSDetachThread(OSThread* thread) {
  boundary();
  ThreadSlot* t = slotFor(thread);
  if (!t) return;
  t->detached = 1;
  if (t->state == ThreadState::moribund) t->state = ThreadState::free;
}

s32 OSResumeThread(OSThread* thread) {
  boundary();
  ThreadSlot* t = slotFor(thread);
  if (!t) return 0;
  const s32 old = t->suspend;
  if (t->suspend > 0 && --t->suspend == 0 && t->state == ThreadState::ready) t->ready_seq = ++st().seq;
  maybePreempt();
  return old;
}

s32 OSSuspendThread(OSThread* thread) {
  boundary();
  auto& s = st();
  ThreadSlot* t = slotFor(thread);
  if (!t) return 0;
  const s32 old = t->suspend++;
  if (s.current >= 0 && t == &s.threads[s.current]) {
    makeReady(*t);  // ready but suspended: not picked until resumed
    switchToHost();
  }
  return old;
}

BOOL OSIsThreadSuspended(OSThread* thread) {
  boundary();
  ThreadSlot* t = slotFor(thread);
  return t && t->suspend > 0;
}

BOOL OSIsThreadTerminated(OSThread* thread) {
  boundary();
  ThreadSlot* t = slotFor(thread);
  return !t || t->state == ThreadState::moribund;
}

void OSSleepThread(OSThreadQueue* queue) {
  boundary();
  block(WaitKind::queue, queue);
}

void OSWakeupThread(OSThreadQueue* queue) {
  boundary();
  wakeAll(WaitKind::queue, queue);
  maybePreempt();
}

void OSYieldThread(void) {
  boundary();
  auto& s = st();
  if (s.current < 0 || s.current == static_cast<std::int32_t>(kIrqSlot) || s.in_interrupt) return;
  ThreadSlot& cur = s.threads[s.current];
  for (std::uint32_t i = 0; i < kMaxThreads; ++i) {
    const ThreadSlot& t = s.threads[i];
    if (static_cast<std::int32_t>(i) == s.current || t.state != ThreadState::ready || t.suspend > 0) continue;
    if (t.priority <= cur.priority) {
      makeReady(cur);
      switchToHost();
      return;
    }
  }
}

BOOL OSSetThreadPriority(OSThread* thread, OSPriority priority) {
  boundary();
  if (priority < OS_PRIORITY_MIN || priority > OS_PRIORITY_MAX) return FALSE;
  ThreadSlot* t = slotFor(thread);
  if (!t) return FALSE;
  t->priority = priority;
  maybePreempt();
  return TRUE;
}

OSPriority OSGetThreadPriority(OSThread* thread) {
  boundary();
  ThreadSlot* t = slotFor(thread);
  return t ? t->priority : OS_PRIORITY_IDLE;
}

s32 OSCheckActiveThreads(void) {
  boundary();
  s32 n = 0;
  for (std::uint32_t i = 0; i < kMaxThreads; ++i)
    if (st().threads[i].state != ThreadState::free && st().threads[i].state != ThreadState::moribund) ++n;
  return n;
}

// The idle function runs on an SDK thread at the lowest priority, once per
// frame step: it is made ready at the start of each step and runs when every
// other thread waits, as the console's idle thread would, but it never runs
// more than once per step, so a function that returns quickly cannot spin the
// scheduler. THP decoding (MP4's thpmain) uses it (ADR 0007 §4). The given
// stack is not used: the thread runs on its arena stack.
OSThread* OSSetIdleFunction(OSIdleFunction idleFunction, void* param, void* stack, u32 stackSize) {
  (void)stack;
  (void)stackSize;
  boundary();
  auto& s = st();
  s.idle_fn = idleFunction;
  s.idle_param = param;
  if (!idleFunction) return nullptr;
  if (s.idle_slot < 0) {
    for (std::uint32_t i = 1; i < kMaxThreads; ++i) {
      ThreadSlot& t = s.threads[i];
      if (t.state != ThreadState::free) continue;
      t = ThreadSlot{};
      t.user = reinterpret_cast<OSThread*>(s.idle_thread_storage);
      t.priority = OS_PRIORITY_IDLE;
      t.detached = 1;
      t.state = ThreadState::waiting;
      t.wait_kind = WaitKind::idle;
      t.wait_seq = ++s.seq;
      s.idle_slot = static_cast<std::int32_t>(i);
      initThreadContext(i);
      break;
    }
    if (s.idle_slot < 0) fatal("thread table full (32 SDK threads) for the idle thread");
  }
  return reinterpret_cast<OSThread*>(s.idle_thread_storage);
}

s32 OSDisableScheduler(void) {
  boundary();
  return st().scheduler_disabled++;
}

s32 OSEnableScheduler(void) {
  boundary();
  auto& s = st();
  const s32 old = s.scheduler_disabled;
  if (s.scheduler_disabled > 0) --s.scheduler_disabled;
  maybePreempt();
  return old;
}

// ---- Message queues ----------------------------------------------------------------------

void OSInitMessageQueue(OSMessageQueue* mq, OSMessage* msgArray, s32 msgCount) {
  boundary();
  if (msgCount <= 0) fatal("OSInitMessageQueue with no slots");
  MessageQueueSlot* slot = nullptr;
  for (MessageQueueSlot& q : st().mqs) {
    if (q.key == mq) {
      slot = &q;
      break;
    }
    if (!q.key && !slot) slot = &q;
  }
  if (!slot) fatal("message queue table full (64 queues)");
  *slot = MessageQueueSlot{mq, msgArray, msgCount, 0, 0};
}

BOOL OSSendMessage(OSMessageQueue* mq, OSMessage msg, s32 flags) {
  boundary();
  MessageQueueSlot* q = mqFor(mq);
  while (q->used >= q->count) {
    if (!(flags & OS_MESSAGE_BLOCK)) return FALSE;
    block(WaitKind::mq_send, mq);
    q = mqFor(mq);
  }
  q->msgs[(q->first + q->used) % q->count] = msg;
  ++q->used;
  wakeAll(WaitKind::mq_recv, mq);
  maybePreempt();
  return TRUE;
}

BOOL OSJamMessage(OSMessageQueue* mq, OSMessage msg, s32 flags) {
  boundary();
  MessageQueueSlot* q = mqFor(mq);
  while (q->used >= q->count) {
    if (!(flags & OS_MESSAGE_BLOCK)) return FALSE;
    block(WaitKind::mq_send, mq);
    q = mqFor(mq);
  }
  q->first = (q->first + q->count - 1) % q->count;
  q->msgs[q->first] = msg;
  ++q->used;
  wakeAll(WaitKind::mq_recv, mq);
  maybePreempt();
  return TRUE;
}

BOOL OSReceiveMessage(OSMessageQueue* mq, OSMessage* msg, s32 flags) {
  boundary();
  MessageQueueSlot* q = mqFor(mq);
  while (q->used == 0) {
    if (!(flags & OS_MESSAGE_BLOCK)) return FALSE;
    block(WaitKind::mq_recv, mq);
    q = mqFor(mq);
  }
  if (msg) *msg = q->msgs[q->first];
  q->first = (q->first + 1) % q->count;
  --q->used;
  wakeAll(WaitKind::mq_send, mq);
  maybePreempt();
  return TRUE;
}

// ---- Mutexes and condition variables ------------------------------------------------------

void OSInitMutex(OSMutex* mutex) {
  boundary();
  MutexSlot* m = mutexFor(mutex, true);
  m->owner = -1;
  m->count = 0;
}

void OSLockMutex(OSMutex* mutex) {
  boundary();
  const std::int32_t me = currentSlot();
  currentThreadSlot();
  MutexSlot* m = mutexFor(mutex, false);
  while (m->owner != -1 && m->owner != me) {
    block(WaitKind::mutex, mutex);
    m = mutexFor(mutex, false);
  }
  m->owner = me;
  ++m->count;
}

void OSUnlockMutex(OSMutex* mutex) {
  boundary();
  MutexSlot* m = mutexFor(mutex, false);
  if (m->owner != currentSlot()) return;
  if (--m->count == 0) {
    m->owner = -1;
    wakeAll(WaitKind::mutex, mutex);
    maybePreempt();
  }
}

BOOL OSTryLockMutex(OSMutex* mutex) {
  boundary();
  const std::int32_t me = currentSlot();
  MutexSlot* m = mutexFor(mutex, false);
  if (m->owner != -1 && m->owner != me) return FALSE;
  m->owner = me;
  ++m->count;
  return TRUE;
}

void OSInitCond(OSCond* cond) { OSInitThreadQueue(&cond->queue); }

void OSWaitCond(OSCond* cond, OSMutex* mutex) {
  boundary();
  MutexSlot* m = mutexFor(mutex, false);
  if (m->owner != currentSlot()) return;
  const std::int32_t count = m->count;
  m->owner = -1;
  m->count = 0;
  wakeAll(WaitKind::mutex, mutex);
  block(WaitKind::queue, &cond->queue);
  m = mutexFor(mutex, false);
  while (m->owner != -1) {
    block(WaitKind::mutex, mutex);
    m = mutexFor(mutex, false);
  }
  m->owner = currentSlot();
  m->count = count;
}

void OSSignalCond(OSCond* cond) { OSWakeupThread(&cond->queue); }

// ---- Alarms ---------------------------------------------------------------------------------

void OSCreateAlarm(OSAlarm* alarm) {
  boundary();
  cancelEvents(EventKind::alarm, alarm);
  AlarmSlot* a = alarmFor(alarm, true);
  *a = AlarmSlot{};
  a->key = alarm;
}

void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler) {
  boundary();
  armAlarm(alarm, st().now + tick, 0, handler);
}

void OSSetAbsAlarm(OSAlarm* alarm, OSTime time, OSAlarmHandler handler) {
  boundary();
  armAlarm(alarm, time, 0, handler);
}

void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period, OSAlarmHandler handler) {
  boundary();
  if (period <= 0) fatal("OSSetPeriodicAlarm with a period <= 0");
  const std::int64_t now = st().now;
  std::int64_t fire = start;
  if (fire <= now) fire += ((now - fire) / period + 1) * period;
  armAlarm(alarm, fire, period, handler);
}

void OSCancelAlarm(OSAlarm* alarm) {
  boundary();
  cancelEvents(EventKind::alarm, alarm);
  if (AlarmSlot* a = alarmFor(alarm, false)) a->armed = 0;
}

void OSSetAlarmTag(OSAlarm* alarm, u32 tag) {
  boundary();
  alarmFor(alarm, true)->tag = tag;
}

void OSCancelAlarms(u32 tag) {
  boundary();
  if (tag == 0) return;
  for (AlarmSlot& a : st().alarms) {
    if (a.key && a.tag == tag && a.armed) {
      cancelEvents(EventKind::alarm, a.key);
      a.armed = 0;
    }
  }
}

// ---- System and settings -----------------------------------------------------------------------

void OSInit(void) { boundary(); }
u32 OSGetConsoleType(void) {
  boundary();
  return OS_CONSOLE_RETAIL3;
}
u32 OSGetPhysicalMemSize(void) {
  boundary();
  return static_cast<u32>(kMainRamBytes);
}
u32 OSGetConsoleSimulatedMemSize(void) {
  boundary();
  return static_cast<u32>(kMainRamBytes);
}
u32 OSGetSoundMode(void) {
  boundary();
  return st().sound_mode;
}
void OSSetSoundMode(u32 mode) {
  boundary();
  st().sound_mode = mode ? OS_SOUND_MODE_STEREO : OS_SOUND_MODE_MONO;
}
u32 OSGetProgressiveMode(void) {
  boundary();
  return st().progressive;
}
void OSSetProgressiveMode(u32 on) {
  boundary();
  st().progressive = on ? 1 : 0;
}
u8 OSGetLanguage(void) {
  boundary();
  return OS_LANG_ENGLISH;
}
u32 OSGetResetCode(void) {
  boundary();
  return 0;
}
BOOL OSGetResetSwitchState(void) {
  boundary();
  return FALSE;
}
BOOL OSGetResetButtonState(void) {
  boundary();
  return FALSE;
}

void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu) {
  (void)reset;
  (void)resetCode;
  (void)forceMenu;
  boundary();
  auto& s = st();
  s.reset_requested = 1;
  if (s.current >= 0) switchToHost();
  fatal("OSResetSystem returned");
}

void OSReport(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  report(buf);
}

void OSPanic(const char* file, int line, const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  char msg[1300];
  std::snprintf(msg, sizeof msg, "OSPanic at %s:%d: %s", file ? file : "?", line, buf);
  fatal(msg);
}

// ---- Stopwatches -----------------------------------------------------------------------------------
// The public SDK's behaviour: every start, stop and check reads OSGetTime, so each costs one
// time-query quantum of virtual time, as it costs real time on the console.

void OSInitStopwatch(OSStopwatch* sw, char* name) {
  boundary();
  sw->name = name;
  sw->total = 0;
  sw->hits = 0;
  sw->min = 0x00000000FFFFFFFFll;
  sw->max = 0;
  sw->last = 0;
  sw->running = FALSE;
}

void OSStartStopwatch(OSStopwatch* sw) {
  sw->running = TRUE;
  sw->last = OSGetTime();
}

void OSStopStopwatch(OSStopwatch* sw) {
  if (!sw->running) {
    boundary();
    return;
  }
  const OSTime interval = OSGetTime() - sw->last;
  sw->total += interval;
  sw->running = FALSE;
  sw->hits++;
  if (sw->max < interval) sw->max = interval;
  if (interval < sw->min) sw->min = interval;
}

OSTime OSCheckStopwatch(OSStopwatch* sw) {
  OSTime total = sw->total;
  if (sw->running) {
    total += OSGetTime() - sw->last;
  } else {
    boundary();
  }
  return total;
}

void OSResetStopwatch(OSStopwatch* sw) { OSInitStopwatch(sw, sw->name); }

void OSDumpStopwatch(OSStopwatch* sw) {
  boundary();
  char buf[256];
  std::snprintf(buf, sizeof buf, "Stopwatch [%s]: hits %u, total %lld ticks, min %lld, max %lld\n",
                sw->name ? sw->name : "", sw->hits, static_cast<long long>(sw->total),
                static_cast<long long>(sw->min), static_cast<long long>(sw->max));
  report(buf);
}

// ---- Address spaces -------------------------------------------------------------------------------

void* OSCachedToUncached(void* caddr) { return caddr; }
void* OSUncachedToCached(void* ucaddr) { return ucaddr; }

// ---- Fast casts -------------------------------------------------------------------------------------

void OSf32tos8(const f32* in, s8* out) { *out = saturate<s8>(*in, -128.0f, 127.0f); }
void OSf32tou8(const f32* in, u8* out) { *out = saturate<u8>(*in, 0.0f, 255.0f); }
void OSf32tos16(const f32* in, s16* out) { *out = saturate<s16>(*in, -32768.0f, 32767.0f); }
void OSf32tou16(const f32* in, u16* out) { *out = saturate<u16>(*in, 0.0f, 65535.0f); }
void OSs8tof32(const s8* in, f32* out) { *out = static_cast<f32>(*in); }
void OSu8tof32(const u8* in, f32* out) { *out = static_cast<f32>(*in); }
void OSs16tof32(const s16* in, f32* out) { *out = static_cast<f32>(*in); }
void OSu16tof32(const u16* in, f32* out) { *out = static_cast<f32>(*in); }

// ---- Caches and CPU ------------------------------------------------------------------------------------

void DCFlushRange(void*, u32) {}
void DCFlushRangeNoSync(void*, u32) {}
void DCStoreRange(void*, u32) {}
void DCStoreRangeNoSync(void*, u32) {}
void DCInvalidateRange(void*, u32) {}
void DCTouchRange(void*, u32) {}
void ICInvalidateRange(void*, u32) {}
void ICFlashInvalidate(void) {}
void PPCSync(void) {}
void LCEnable(void) {}
void LCDisable(void) {}

}  // extern "C"
