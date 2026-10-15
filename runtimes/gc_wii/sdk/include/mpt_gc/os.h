/* OS: virtual time, cooperative threads, alarms, message queues, mutexes,
 * the arena and heaps, interrupts and fixed hardware answers (ADR 0007 §4,
 * ADR 0008 §1-3, §7). Every function here except the fast casts, OSReport
 * and the cache no-ops is an "SDK call boundary": due virtual interrupts run on
 * entry, and a higher-priority thread made ready by them may take over before
 * the call returns.
 */
#ifndef MPT_GC_OS_H
#define MPT_GC_OS_H

#include "mpt_gc/types.h"

MPT_GC_EXTERN_C_BEGIN

/* ---- Time ---------------------------------------------------------------- */

typedef s64 OSTime;
typedef u32 OSTick;

/* Console bus clock 162 MHz; the time base runs at a quarter of it. */
#define OS_BUS_CLOCK 162000000u
#define OS_TIMER_CLOCK (OS_BUS_CLOCK / 4u)
#define OSSecondsToTicks(s) ((s) * (OSTime)OS_TIMER_CLOCK)
#define OSMillisecondsToTicks(ms) ((ms) * ((OSTime)OS_TIMER_CLOCK / 1000))
#define OSMicrosecondsToTicks(us) (((us) * ((OSTime)OS_TIMER_CLOCK / 125000)) / 8)
#define OSTicksToSeconds(t) ((t) / (OSTime)OS_TIMER_CLOCK)
#define OSTicksToMilliseconds(t) ((t) / ((OSTime)OS_TIMER_CLOCK / 1000))

typedef struct OSCalendarTime {
  int sec;  /* 0-59 */
  int min;  /* 0-59 */
  int hour; /* 0-23 */
  int mday; /* 1-31 */
  int mon;  /* 0-11 */
  int year; /* e.g. 2001 */
  int wday; /* 0 = Sunday */
  int yday; /* 0-365 */
  int msec; /* 0-999 */
  int usec; /* 0-999 */
} OSCalendarTime;

/* Each call advances virtual time by the fixed per-query quantum first (ADR 0008 §2). */
OSTime OSGetTime(void);
OSTick OSGetTick(void);
/* Ticks since 2000-01-01 00:00:00 to a calendar date. Pure, no time query. */
void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* td);
OSTime OSCalendarTimeToTicks(const OSCalendarTime* td);

/* ---- Interrupts (no-ops under cooperative scheduling, ADR 0007 §7) -------- */

BOOL OSDisableInterrupts(void);
BOOL OSEnableInterrupts(void);
BOOL OSRestoreInterrupts(BOOL level);

/* ---- Threads -------------------------------------------------------------- */

typedef s32 OSPriority; /* 0 (highest) .. 31 (lowest) */
#define OS_PRIORITY_MIN 0
#define OS_PRIORITY_MAX 31
#define OS_PRIORITY_IDLE OS_PRIORITY_MAX
#define OS_THREAD_ATTR_DETACH 0x0001u

/* Opaque to the SDK: only the address is used, as a key. Sized like the
 * console's structures so code that embeds them keeps sensible layouts. */
typedef struct OSThread { u8 opaque[0x320]; } OSThread;
typedef struct OSThreadQueue { void* head; void* tail; } OSThreadQueue;
typedef struct OSContext { u8 opaque[0x2c8]; } OSContext;
typedef struct OSMutex { u8 opaque[0x18]; } OSMutex;
typedef struct OSCond { OSThreadQueue queue; } OSCond;

typedef void* (*OSThreadFunc)(void* param);
typedef void (*OSIdleFunction)(void* param);

void OSInitThreadQueue(OSThreadQueue* queue);
OSThread* OSGetCurrentThread(void);
/* Threads are created suspended. The coroutine runs on an SDK stack in the
 * arena; `stackBase`/`stackSize` are recorded but not used for execution. */
BOOL OSCreateThread(OSThread* thread, OSThreadFunc func, void* param, void* stackBase, u32 stackSize,
                    OSPriority priority, u16 attr);
void OSExitThread(void* val);
void OSCancelThread(OSThread* thread);
BOOL OSJoinThread(OSThread* thread, void** val);
void OSDetachThread(OSThread* thread);
s32 OSResumeThread(OSThread* thread);
s32 OSSuspendThread(OSThread* thread);
BOOL OSIsThreadSuspended(OSThread* thread);
BOOL OSIsThreadTerminated(OSThread* thread);
void OSSleepThread(OSThreadQueue* queue);
void OSWakeupThread(OSThreadQueue* queue);
void OSYieldThread(void);
BOOL OSSetThreadPriority(OSThread* thread, OSPriority priority);
OSPriority OSGetThreadPriority(OSThread* thread);
s32 OSCheckActiveThreads(void);
/* Runs idleFunction on the lowest-priority SDK thread once per frame step, when
 * every other thread waits (THP decoding, ADR 0007 §4). NULL stops it. */
OSThread* OSSetIdleFunction(OSIdleFunction idleFunction, void* param, void* stack, u32 stackSize);
s32 OSDisableScheduler(void);
s32 OSEnableScheduler(void);

/* ---- Message queues, mutexes, condition variables ------------------------- */

typedef void* OSMessage;
#define OS_MESSAGE_NOBLOCK 0
#define OS_MESSAGE_BLOCK 1

typedef struct OSMessageQueue { u8 opaque[0x20]; } OSMessageQueue;

void OSInitMessageQueue(OSMessageQueue* mq, OSMessage* msgArray, s32 msgCount);
BOOL OSSendMessage(OSMessageQueue* mq, OSMessage msg, s32 flags);
BOOL OSJamMessage(OSMessageQueue* mq, OSMessage msg, s32 flags);
BOOL OSReceiveMessage(OSMessageQueue* mq, OSMessage* msg, s32 flags);

void OSInitMutex(OSMutex* mutex);
void OSLockMutex(OSMutex* mutex);
void OSUnlockMutex(OSMutex* mutex);
BOOL OSTryLockMutex(OSMutex* mutex);
void OSInitCond(OSCond* cond);
void OSWaitCond(OSCond* cond, OSMutex* mutex);
void OSSignalCond(OSCond* cond);

/* ---- Alarms ---------------------------------------------------------------- */

typedef struct OSAlarm { u8 opaque[0x28]; } OSAlarm;
typedef void (*OSAlarmHandler)(OSAlarm* alarm, OSContext* context);

void OSCreateAlarm(OSAlarm* alarm);
void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler);
void OSSetAbsAlarm(OSAlarm* alarm, OSTime time, OSAlarmHandler handler);
void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period, OSAlarmHandler handler);
void OSCancelAlarm(OSAlarm* alarm);
void OSSetAlarmTag(OSAlarm* alarm, u32 tag);
void OSCancelAlarms(u32 tag);

/* ---- Arena and heaps --------------------------------------------------------- */

typedef int OSHeapHandle;

void* OSGetArenaLo(void);
void* OSGetArenaHi(void);
void OSSetArenaLo(void* addr);
void OSSetArenaHi(void* addr);
/* Allocates from the low or high end of the OS arena and moves that end. */
void* OSAllocFromArenaLo(u32 size, u32 align);
void* OSAllocFromArenaHi(u32 size, u32 align);

void* OSInitAlloc(void* arenaStart, void* arenaEnd, int maxHeaps);
OSHeapHandle OSCreateHeap(void* start, void* end);
void OSDestroyHeap(OSHeapHandle heap);
OSHeapHandle OSSetCurrentHeap(OSHeapHandle heap);
void* OSAllocFromHeap(OSHeapHandle heap, u32 size);
void* OSAllocFixed(void** rstart, void** rend);
void OSFreeToHeap(OSHeapHandle heap, void* ptr);
s32 OSCheckHeap(OSHeapHandle heap);
/* Debug output on the console; here it only checks the handle. */
void OSDumpHeap(OSHeapHandle heap);
#define OSAlloc(size) OSAllocFromHeap(__OSCurrHeap, (size))
#define OSFree(ptr) OSFreeToHeap(__OSCurrHeap, (ptr))
extern volatile OSHeapHandle __OSCurrHeap;

/* ---- System, settings and fixed hardware answers (ADR 0007 §4) --------------- */

#define OS_CONSOLE_RETAIL3 0x00000003u
#define OS_SOUND_MODE_MONO 0u
#define OS_SOUND_MODE_STEREO 1u
#define OS_LANG_ENGLISH 0u

void OSInit(void);
u32 OSGetConsoleType(void);
u32 OSGetPhysicalMemSize(void);
u32 OSGetConsoleSimulatedMemSize(void);
u32 OSGetSoundMode(void);
void OSSetSoundMode(u32 mode);
u32 OSGetProgressiveMode(void);
void OSSetProgressiveMode(u32 on);
u8 OSGetLanguage(void);
u32 OSGetResetCode(void);
BOOL OSGetResetSwitchState(void);
BOOL OSGetResetButtonState(void); /* the same answer as OSGetResetSwitchState: never pressed */
/* Ends the session: the frame step reports a reset request to the host. */
void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu);

void OSReport(const char* fmt, ...);
/* Fatal: stops the runtime with an error naming file and line. Never returns. */
void OSPanic(const char* file, int line, const char* fmt, ...);

/* ---- Fast float <-> int casts with the console's saturating semantics -------- *
 * Float to int: NaN gives 0, the value is truncated toward zero, then clamped to
 * the target range. These replace the paired-single quantized stores (ADR 0008 §5). */
void OSf32tos8(const f32* in, s8* out);
void OSf32tou8(const f32* in, u8* out);
void OSf32tos16(const f32* in, s16* out);
void OSf32tou16(const f32* in, u16* out);
void OSs8tof32(const s8* in, f32* out);
void OSu8tof32(const u8* in, f32* out);
void OSs16tof32(const s16* in, f32* out);
void OSu16tof32(const u16* in, f32* out);

/* ---- Stopwatches (debug timing) ---------------------------------------------- *
 * Read virtual time through OSGetTime, as the console's do, so each start, stop and
 * check costs one time-query quantum. The layout is the public SDK's (asserted
 * against the pinned aurora headers by the MP4 module). */
typedef struct OSStopwatch {
  char* name;
  u32 hits;
  OSTime total;
  OSTime min;
  OSTime max;
  OSTime last;
  BOOL running;
} OSStopwatch;

void OSInitStopwatch(OSStopwatch* sw, char* name);
void OSStartStopwatch(OSStopwatch* sw);
void OSStopStopwatch(OSStopwatch* sw);
OSTime OSCheckStopwatch(OSStopwatch* sw);
void OSResetStopwatch(OSStopwatch* sw);
void OSDumpStopwatch(OSStopwatch* sw);

/* ---- Address spaces ------------------------------------------------------------ *
 * The host has one address space: cached and uncached views are the same pointer. */
void* OSCachedToUncached(void* caddr);
void* OSUncachedToCached(void* ucaddr);

/* ---- Caches and CPU (no-ops) -------------------------------------------------- */

void DCFlushRange(void* addr, u32 nbytes);
void DCFlushRangeNoSync(void* addr, u32 nbytes);
void DCStoreRange(void* addr, u32 nbytes);
void DCStoreRangeNoSync(void* addr, u32 nbytes);
void DCInvalidateRange(void* addr, u32 nbytes);
void DCTouchRange(void* addr, u32 nbytes);
void ICInvalidateRange(void* addr, u32 nbytes);
void ICFlashInvalidate(void);
void PPCSync(void);
void LCEnable(void);
void LCDisable(void);

MPT_GC_EXTERN_C_END

#endif /* MPT_GC_OS_H */
