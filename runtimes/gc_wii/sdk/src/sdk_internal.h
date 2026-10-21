// Internal state and services of the SDK layer. Not part of the public API.
//
// Every piece of game-visible SDK state is in SdkState, a plain struct placed at
// the start of the arena. Save states therefore capture it with the rest of the
// arena, and stateHash() walks it field by field (ADR 0008 §8, §10).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mpt/gc/sdk.h"
#include "mpt_gc/ai.h"
#include "mpt_gc/ar.h"
#include "mpt_gc/card.h"
#include "mpt_gc/dvd.h"
#include "mpt_gc/os.h"
#include "mpt_gc/pad.h"
#include "mpt_gc/vi.h"

#if !defined(__clang__)
#error "The GameCube SDK layer must be built with Clang (ADR 0008 §4)"
#endif

namespace mpt::gc::detail {

// ---- Fixed capacities --------------------------------------------------------
inline constexpr std::uint32_t kIrqSlot = kMaxThreads;  // the interrupt coroutine
inline constexpr std::uint32_t kSlotCount = kMaxThreads + 1;
inline constexpr std::uint32_t kMaxEvents = 256;
inline constexpr std::uint32_t kMaxMessageQueues = 64;
inline constexpr std::uint32_t kMaxAlarms = 128;
inline constexpr std::uint32_t kMaxMutexes = 64;
inline constexpr std::uint32_t kMaxHeaps = 16;
inline constexpr std::uint32_t kMaxArStack = 64;
inline constexpr std::uint32_t kCardSectorSize = 8192;
inline constexpr std::uint32_t kCardSystemBlocks = 5;
inline constexpr std::uint32_t kCardMaxBlocks = 2048;  // 16 Mbit + headroom

// ---- Arena layout --------------------------------------------------------------
inline constexpr std::size_t kStateRegionBytes = 1u << 20;
inline constexpr std::size_t kMainRamBytes = 24u << 20;  // the console's; the default SdkConfig::main_ram_bytes
inline constexpr std::size_t kAramBytes = 16u << 20;
inline constexpr std::size_t kCardRegionBytes = 2u << 20;  // 16 Mbit
inline constexpr std::size_t kLowMemoryBytes = 0x3100;     // console OS globals area, kept zero

// ---- Threads ------------------------------------------------------------------------
enum class ThreadState : std::uint8_t { free, ready, running, waiting, moribund };
enum class WaitKind : std::uint8_t { none, queue, retrace, join, mutex, mq_send, mq_recv, idle };

struct ThreadSlot {
  ThreadState state;
  WaitKind wait_kind;
  std::uint8_t detached;
  std::uint8_t started;
  std::int32_t priority;
  std::int32_t suspend;
  std::uint64_t ready_seq;
  std::uint64_t wait_seq;
  const void* wait_key;
  OSThread* user;
  OSThreadFunc func;
  void* param;
  void* exit_val;
  void* ctx_sp;  // saved context (part of the save state, never of the hash)
  std::uint32_t stack_offset;
  std::uint32_t stack_size;
};

// ---- Virtual interrupts -----------------------------------------------------------------
// Ties at one deadline are broken by this fixed source order, then by the order
// the events were scheduled in (ADR 0008 §2).
enum class Source : std::uint8_t { vi = 0, ai = 1, dsp = 2, aram = 3, dvd = 4, card = 5, alarm = 6 };
enum class EventKind : std::uint8_t { none, retrace, alarm, dvd_read, dvd_cancel, ar_dma, arq, card, host_tick, ai_dma };

struct Event {
  std::int64_t deadline;
  std::uint64_t seq;
  Source source;
  EventKind kind;
  std::uint8_t used;
  void* a;
  void* b;
  std::int64_t c;
};

struct MessageQueueSlot {
  const void* key;
  OSMessage* msgs;
  std::int32_t count;
  std::int32_t first;
  std::int32_t used;
};

struct AlarmSlot {
  const void* key;
  OSAlarmHandler handler;
  std::int64_t fire;
  std::int64_t period;  // 0 = one-shot
  std::uint32_t tag;
  std::uint8_t armed;
};

struct MutexSlot {
  const void* key;
  std::int32_t owner;  // thread slot, -1 = unlocked
  std::int32_t count;
};

struct HeapSlot {
  std::int32_t active;
  std::uint32_t start;  // arena offsets
  std::uint32_t end;
  std::uint32_t free_head;   // block offsets, 0 = none
  std::uint32_t alloc_head;
};

// ---- AI (audio DMA) ----------------------------------------------------------------
// One AI-DMA block completes per virtual AI tick. The attached sound engine
// (MusyX in pull mode) renders into one of two blocks in this state, exactly as
// MusyX's own AI callback refills the DMA buffer on the console (ADR 0007 §4).
inline constexpr std::uint32_t kAiEngineBlockSamples = 160 * 2;  // 5 ms of s16 stereo at 32 kHz

struct AiState {
  std::uint8_t initialized;
  std::uint8_t dma_enabled;
  std::uint8_t engine_attached;
  std::uint8_t engine_block;  // the block the engine renders next
  std::uint32_t dsp_rate;     // AI_SAMPLERATE_32KHZ / _48KHZ
  void* dma_addr;
  std::uint32_t dma_len;      // bytes
  AIDCallback callback;       // the registered DMA callback (the engine's when none is registered)
  std::int64_t next_deadline;  // of the pending AI tick, if dma_enabled
  std::int64_t period;         // of the pending tick, in timer ticks
  std::int64_t period_acc;     // fractional remainder accumulator (numerator over the rate)
  std::uint64_t ticks;         // AI ticks so far
  std::uint64_t frames_out;    // stereo frames DMA'd so far (identical with null or real output)
  // DVD audio streaming registers: stored, never played (no title streams disc audio yet).
  std::uint32_t stream_play_state;
  std::uint32_t stream_rate;
  std::uint8_t stream_vol_l;
  std::uint8_t stream_vol_r;
  std::uint32_t stream_trigger;
  AISCallback stream_callback;
  alignas(32) std::int16_t engine_buf[2][kAiEngineBlockSamples];
};

// ---- Card ---------------------------------------------------------------------------------
struct CardDirEntry {
  std::uint8_t used;
  std::uint8_t game_name[4];
  std::uint8_t company[2];
  std::uint8_t banner_format;
  std::uint8_t permission;
  char file_name[CARD_FILENAME_MAX];
  std::uint32_t time;
  std::uint32_t icon_addr;
  std::uint16_t icon_format;
  std::uint16_t icon_speed;
  std::uint32_t comment_addr;
  std::uint16_t start_block;  // within the user area
  std::uint16_t block_count;
};

struct CardState {
  std::uint8_t inserted;
  std::uint8_t mounted;
  std::uint16_t size_mbits;
  std::uint16_t encoding;
  std::uint16_t user_blocks;
  std::int32_t result;  // CARDGetResultCode
  std::uint64_t serial;
  std::uint64_t generation;
  CardDirEntry dir[CARD_MAX_FILE];
};

// ---- The whole SDK state ------------------------------------------------------------------
struct SdkState {
  std::uint32_t magic;
  std::uint32_t version;

  // Clock (ticks of the 40.5 MHz time base).
  std::int64_t epoch;
  std::int64_t now;
  std::int64_t field_start;  // R_n, the current field's retrace time
  std::int64_t next_field;   // R_{n+1}
  std::int64_t field_whole;  // field period = field_whole + field_num / field_den
  std::int64_t field_num;
  std::int64_t field_den;
  std::int64_t field_acc;
  std::uint64_t frame;
  std::uint64_t time_queries;
  std::uint32_t retrace_count;
  std::uint8_t halted;
  std::uint8_t faulted;
  std::uint8_t reset_requested;
  std::uint8_t pad0;
  std::uint64_t seq;

  // Scheduler.
  ThreadSlot threads[kSlotCount];
  std::int32_t current;  // slot index, -1 = host
  std::int32_t in_interrupt;
  std::int32_t scheduler_disabled;
  std::int32_t interrupts_enabled;
  std::uint8_t default_thread_storage[sizeof(OSThread)];
  std::uint8_t idle_thread_storage[sizeof(OSThread)];
  std::uint8_t dummy_context[sizeof(OSContext)];

  Event events[kMaxEvents];
  MessageQueueSlot mqs[kMaxMessageQueues];
  AlarmSlot alarms[kMaxAlarms];
  MutexSlot mutexes[kMaxMutexes];

  // OS arena and heaps.
  std::uint32_t arena_lo;  // arena offsets
  std::uint32_t arena_hi;
  std::int32_t current_heap;
  std::int32_t heap_count;
  HeapSlot heaps[kMaxHeaps];
  std::uint32_t sound_mode;
  std::uint32_t progressive;

  // VI.
  VIRetraceCallback pre_retrace;
  VIRetraceCallback post_retrace;
  void* next_fb;
  void* current_fb;
  void* flushed_fb;
  std::uint32_t tv_format;
  std::uint8_t black;
  std::uint8_t black_flushed;

  // PAD.
  std::uint8_t pad_initialized;
  PadState staged[kMaxPlayers];
  PADStatus latched[PAD_MAX_CONTROLLERS];
  std::uint32_t motor[PAD_MAX_CONTROLLERS];
  std::uint32_t pad_spec;
  std::uint32_t pad_analog_mode;

  // DVD.
  std::uint32_t dvd_cwd;
  std::uint8_t dvd_initialized;
  std::uint8_t dvd_auto_invalidation;

  // AR / ARQ.
  std::uint8_t ar_initialized;
  std::uint8_t arq_initialized;
  std::uint32_t ar_top;
  std::uint32_t ar_stack_depth;
  std::uint32_t ar_stack[kMaxArStack];
  ARCallback ar_dma_callback;
  std::uint32_t arq_chunk_size;

  // CARD.
  CardState card;

  // AI and the sound engine's DMA blocks.
  AiState ai;

  // THP (the decoder itself is stateless) and the idle function, which runs
  // as the lowest-priority thread once per frame step (THP decoding, ADR 0007 §4).
  std::uint8_t thp_initialized;
  std::int32_t idle_slot;      // thread slot of the idle thread, -1 = none
  OSIdleFunction idle_fn;
  void* idle_param;
  std::uint64_t idle_runs;
};

// ---- Host-side implementation ----------------------------------------------------------------
struct DvdEntry {
  std::string name;   // as on the disc
  std::string lower;  // ASCII lower case, for case-insensitive lookup
  bool is_dir = false;
  std::uint32_t parent = 0;
  std::uint32_t end = 0;  // directories: one past the last entry inside it
  std::uint64_t size = 0;
};

}  // namespace mpt::gc::detail

namespace mpt::gc {

struct Sdk::Impl {
  SdkConfig config;
  MainFn main = nullptr;
  void* main_arg = nullptr;

  std::byte* arena = nullptr;
  std::size_t arena_size = 0;
  std::size_t main_offset = 0;
  std::size_t aram_offset = 0;
  std::size_t card_offset = 0;
  std::size_t stacks_offset = 0;
  detail::SdkState* st = nullptr;

  void* host_sp = nullptr;
  std::vector<detail::DvdEntry> dvd;
  std::string fault;
  std::function<void(const char*)> report_sink;
  // DMA'd PCM (s16 stereo, AI rate) waiting for the host. Presentation only:
  // not in the arena, never hashed, empty with null audio output.
  std::vector<std::int16_t> pcm;

  std::byte* mainRam() const { return arena + main_offset; }
  std::byte* aramBase() const { return arena + aram_offset; }
  std::byte* cardData() const { return arena + card_offset; }
  bool inArena(const void* p) const {
    auto* b = static_cast<const std::byte*>(p);
    return b >= arena && b < arena + arena_size;
  }
  std::uint32_t offsetOf(const void* p) const {
    return static_cast<std::uint32_t>(static_cast<const std::byte*>(p) - arena);
  }
};

}  // namespace mpt::gc

namespace mpt::gc::detail {

// The live SDK. The C API requires one (it aborts with a message otherwise).
Sdk::Impl& impl();
SdkState& st();
bool active();

// ---- Services used by the C API ------------------------------------------------------
// Runs due virtual interrupts and lets a higher-priority ready thread take over.
// Every C API entry point calls it first.
void boundary();
// Advances virtual time by the fixed quantum (a game time query) and returns now.
std::int64_t timeQuery();
// Blocks the current thread until woken. Fatal outside a thread.
void block(WaitKind kind, const void* key);
// Makes every thread waiting on (kind, key) ready, in wait order. Returns the count.
int wakeAll(WaitKind kind, const void* key);
// Lets a higher-priority ready thread run if the caller is a thread (not an interrupt).
void maybePreempt();
std::int32_t currentSlot();
ThreadSlot* slotFor(const OSThread* thread);
OSThread* threadOf(std::int32_t slot);
void makeReady(ThreadSlot& t);
void exitCurrent(void* val);
void switchToHost();
// Builds a fresh coroutine context on the slot's arena stack (SDK threads).
void initThreadContext(std::uint32_t slot);
void alarmFire(const Event& e);

// Schedules a virtual interrupt. Fatal if the queue is full.
void schedule(std::int64_t deadline, Source source, EventKind kind, void* a, void* b, std::int64_t c);
void cancelEvents(EventKind kind, const void* a);
// Delivers an I/O completion: before return, or as an interrupt at `now`.
void completeIo(Source source, EventKind kind, void* a, void* b, std::int64_t c);
void dispatch(const Event& e);

// A fatal SDK error. In a game thread it records the fault and stops the runtime
// (the thread is never resumed; stepFrame returns StepStatus::faulted). The frame
// step's own host-side code never calls it. Reaching it from host code means the
// C API was called outside game code, a programming error reported by throwing
// std::logic_error. It never aborts the process.
[[noreturn]] void fatal(const std::string& message);
void report(const char* text);

// Subsystem hooks called by the core.
void viRetrace();  // the retrace interrupt
void padLatch();   // copies the staged InputFrame into the PAD registers
void padStage(const InputFrame& input);
PADStatus padStatusFrom(const PadState& pad);
void dvdBuildIndex(Sdk::Impl& impl);
void dvdDispatch(const Event& e);
void arDispatch(const Event& e);
void cardDispatch(const Event& e);
void aiBoot(Sdk::Impl& impl);
void aiDispatch(const Event& e);
void aiHash(StateHasher& h);
void idleArm();  // makes the idle thread ready at the start of a frame step
Result cardBoot(Sdk::Impl& impl);
void cardHash(StateHasher& h);
std::vector<std::byte> cardSerialize();
void heapHash(StateHasher& h);
void osSyncGlobalsFromState();

// Pointers hashed by value: null, an arena offset, or "outside the arena" (module
// or host memory; stable IDs for those come with the P1-16 registration table).
std::uint64_t hashPointer(const void* p);
inline std::uint64_t hashFunction(const void* f) { return f ? 0xF0F0'F0F0'F0F0'F0F1ull : 0; }

}  // namespace mpt::gc::detail
