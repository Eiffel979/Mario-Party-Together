// Test-only memory watches for the dump lane (ADR 0008, "Gate status after the implementation",
// decision point 1: find the writer of a layout-dependent word).
//
// Nothing here changes game state. The SDK arena sits at a fixed address (runtimes/gc_wii/mp4/layout.cmake),
// so an arena offset names the same bytes in every run of one build, and the lane reads them in place:
//  - `--watch-word OFF[,OFF...]` prints the 64-bit word at each arena offset whenever it differs after a step;
//  - `--find-word VALUE@FRAME` lists every aligned arena word equal to VALUE after FRAME steps;
//  - `--watch-write OFF[,OFF...]@FRAME` (Windows x64) arms a hardware data breakpoint on each offset (at most
//    four, 8-byte aligned) from FRAME on. A write traps after it completes; a vectored handler records the
//    instruction, the new value and an unwound call stack, clears the trap and resumes. The debug registers
//    belong to the stepping thread, which is the only thread that runs game code (coroutines included).
// The lane is a test tool: none of this is in the shell or the game module.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32) && defined(_M_X64)
#include <windows.h>
#include <thread>
#endif

#include "crash_report.h"

namespace mpt::dump_lane::watch {

constexpr std::uintptr_t kArenaBase = 0x4000'0000'0000ull;  // runtimes/gc_wii/mp4/layout.cmake

struct Options {
  std::vector<std::uint64_t> words;     // --watch-word
  std::uint64_t find_value = 0, find_frame = 0;  // --find-word (find_frame 0: off)
  std::vector<std::uint64_t> writes;    // --watch-write
  std::uint64_t write_frame = 0;
  std::uint64_t arena_bytes = 128ull << 20;  // main RAM plus ARAM, scanned by --find-word
};

// "0x1234,0x5678" (hex, with or without 0x).
inline std::vector<std::uint64_t> parse_list(const std::string& s) {
  std::vector<std::uint64_t> out;
  for (std::size_t from = 0; from < s.size();) {
    const std::size_t comma = std::min(s.find(',', from), s.size());
    out.push_back(std::strtoull(s.substr(from, comma - from).c_str(), nullptr, 16));
    from = comma + 1;
  }
  return out;
}

inline std::uint64_t read_word(std::uint64_t off) {
  std::uint64_t v;
  std::memcpy(&v, reinterpret_cast<const void*>(kArenaBase + off), sizeof v);
  return v;
}

#if defined(_WIN32) && defined(_M_X64)
namespace detail {
struct Hit {
  std::uint64_t frame, slot, value;
  std::uint64_t pcs[24];
  int depth;
};
inline Hit g_hits[256];
inline volatile long g_count = 0;
inline std::uint64_t g_frame = 0;
inline std::uint64_t g_addr[4] = {};

inline LONG WINAPI on_trap(EXCEPTION_POINTERS* info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT* c = info->ContextRecord;
  const DWORD64 bits = c->Dr6 & 0xF;
  if (bits == 0) return EXCEPTION_CONTINUE_SEARCH;
  for (unsigned slot = 0; slot < 4; ++slot) {
    if ((bits & (1ull << slot)) == 0) continue;
    const long n = g_count;
    if (n >= static_cast<long>(std::size(g_hits))) break;
    Hit& h = g_hits[n];
    h.frame = g_frame;
    h.slot = slot;
    std::memcpy(&h.value, reinterpret_cast<const void*>(g_addr[slot]), 8);
    CONTEXT ctx = *c;
    h.depth = 0;
    h.pcs[h.depth++] = ctx.Rip;
    while (h.depth < static_cast<int>(std::size(h.pcs))) {
      DWORD64 image = 0;
      PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &image, nullptr);
      if (fn == nullptr) {
        if (!crash_detail::readable_u64(ctx.Rsp)) break;
        ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
        ctx.Rsp += 8;
      } else {
        void* handler_data = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx.Rip, fn, &ctx, &handler_data, &frame, nullptr);
      }
      if (ctx.Rip == 0) break;
      h.pcs[h.depth++] = ctx.Rip;
    }
    g_count = n + 1;
  }
  c->Dr6 = 0;
  return EXCEPTION_CONTINUE_EXECUTION;
}
}  // namespace detail

// Arms the data breakpoints on the calling thread (through a helper thread, since a thread cannot set
// its own debug registers while it runs).
inline bool arm(const std::vector<std::uint64_t>& offsets) {
  static bool handler = false;
  if (!handler) {
    AddVectoredExceptionHandler(1, &detail::on_trap);
    handler = true;
  }
  HANDLE self = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self,
                       THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, 0))
    return false;
  bool ok = false;
  std::thread([&] {
    if (SuspendThread(self) == static_cast<DWORD>(-1)) return;
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(self, &ctx)) {
      DWORD64 dr7 = 0;
      DWORD64* regs[4] = {&ctx.Dr0, &ctx.Dr1, &ctx.Dr2, &ctx.Dr3};
      for (std::size_t i = 0; i < offsets.size() && i < 4; ++i) {
        detail::g_addr[i] = kArenaBase + offsets[i];
        *regs[i] = detail::g_addr[i];
        dr7 |= 1ull << (2 * i);              // local enable
        dr7 |= 0b01ull << (16 + 4 * i);      // break on data writes
        dr7 |= 0b10ull << (18 + 4 * i);      // 8 bytes
      }
      ctx.Dr7 = dr7;
      ok = SetThreadContext(self, &ctx) != 0;
    }
    ResumeThread(self);
  }).join();
  CloseHandle(self);
  return ok;
}

inline void set_frame(std::uint64_t f) { detail::g_frame = f; }

// Prints and clears the hits recorded so far.
inline void drain(const std::vector<std::uint64_t>& offsets) {
  const long n = detail::g_count;
  for (long i = 0; i < n; ++i) {
    const detail::Hit& h = detail::g_hits[i];
    std::printf("watch-write frame %llu offset 0x%llx value %016llx\n", static_cast<unsigned long long>(h.frame),
                static_cast<unsigned long long>(offsets[h.slot]), static_cast<unsigned long long>(h.value));
    for (int d = 0; d < h.depth; ++d) {
      HMODULE module = nullptr;
      char path[MAX_PATH] = "?";
      const char* base = path;
      if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCSTR>(h.pcs[d]), &module) &&
          module != nullptr && GetModuleFileNameA(module, path, MAX_PATH) != 0) {
        for (const char* c = path; *c != 0; ++c)
          if (*c == '\\' || *c == '/') base = c + 1;
      }
      std::printf("  %s 0x%016llx %s+0x%llx\n", d == 0 ? "at" : "by", static_cast<unsigned long long>(h.pcs[d]),
                  base, static_cast<unsigned long long>(h.pcs[d] - reinterpret_cast<std::uint64_t>(module)));
    }
  }
  detail::g_count = 0;
}
#else
inline bool arm(const std::vector<std::uint64_t>&) { return false; }
inline void set_frame(std::uint64_t) {}
inline void drain(const std::vector<std::uint64_t>&) {}
#endif

// The per-step driver: call before() ahead of stepping frame f (0-based), after() once it is done.
class Watcher {
 public:
  explicit Watcher(Options o) : o_(std::move(o)), last_(o_.words.size(), 0) {}
  bool active() const { return !o_.words.empty() || o_.find_frame != 0 || !o_.writes.empty(); }

  void before(std::uint64_t f) {
    if (!o_.writes.empty() && !armed_ && f + 1 >= o_.write_frame) {
      armed_ = true;
      std::printf("watch-write: %s from frame %llu\n", arm(o_.writes) ? "armed" : "COULD NOT ARM",
                  static_cast<unsigned long long>(f + 1));
    }
    set_frame(f + 1);
  }

  void after(std::uint64_t frame) {
    if (armed_) drain(o_.writes);
    for (std::size_t i = 0; i < o_.words.size(); ++i) {
      const std::uint64_t v = read_word(o_.words[i]);
      if (!seen_ || v != last_[i])
        std::printf("watch-word frame %llu offset 0x%llx %016llx\n", static_cast<unsigned long long>(frame),
                    static_cast<unsigned long long>(o_.words[i]), static_cast<unsigned long long>(v));
      last_[i] = v;
    }
    seen_ = true;
    if (o_.find_frame != 0 && frame == o_.find_frame) {
      for (std::uint64_t off = 0; off + 8 <= o_.arena_bytes; off += 8)
        if (read_word(off) == o_.find_value)
          std::printf("find-word frame %llu offset 0x%llx\n", static_cast<unsigned long long>(frame),
                      static_cast<unsigned long long>(off));
    }
  }

 private:
  Options o_;
  std::vector<std::uint64_t> last_;
  bool seen_ = false;
  bool armed_ = false;
};

}  // namespace mpt::dump_lane::watch
