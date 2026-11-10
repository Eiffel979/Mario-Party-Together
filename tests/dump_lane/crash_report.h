#pragma once

// A crash report for the dump lane. Two parts:
//
// 1. Breadcrumbs (every platform): the run's mode, seed and board, the frame being stepped, the party
//    phase, turn and running overlay, and the last checkpoint's frame and state hash. main.cpp notes
//    them as it goes (crash_note_*), and every report prints them, so a crash in the M4-03 sweep can be
//    told apart and replayed from its seed. They hold numbers and module (overlay) names only: no game
//    text and no paths. Under `--supervise` (supervise.h) they live in memory the supervisor shares.
//
// 2. In-process hooks (Windows x64): on a fatal exception (first chance), print the exception code, the
//    faulting address and a stack walk, each frame as module+offset, then let the process die as it
//    would have. The MP4 module carries a COFF symbol table (/DEBUG:SYMTAB), so `llvm-nm -n` on the
//    installed mp4_game.dll names every frame inside it, even though the recipe keeps no PDB (the
//    supervisor's report names them itself). Game code runs on coroutine stacks; the walk follows the
//    unwind data, which every Clang-built function has.
//
//    A fail-fast (0xC0000409, __fastfail) never reaches an exception handler, so the CRT's ways into one
//    are hooked too: abort() (SIGABRT, which std::terminate also ends in), std::terminate and the CRT's
//    invalid-parameter handler. Each prints the same walk from where it was called. The host and the
//    module share the dynamic UCRT and VCRUNTIME, so the hooks cover both. std::set_terminate is per
//    thread in the MSVC CRT, so the terminate hook covers only the thread that installed it; a
//    terminate on another thread still reports, because the default terminate handler calls abort()
//    and the SIGABRT handler is process-wide. A /GS stack-cookie failure fails fast without any hook,
//    so only `--supervise` reports it: the supervisor debugs the lane and sees the fail-fast.
//
//    On Linux the stack protector's failure path is __stack_chk_fail, which the lane defines itself
//    (stack_chk_fail.cpp) to print the breadcrumbs and the failing function before it aborts.
//
// A test-harness aid only: nothing here touches game state.

#include <cstdint>
#include <cstring>
#include <initializer_list>

#if defined(_WIN32) && defined(_M_X64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <csignal>
#include <cstdlib>
#include <exception>
#endif

#include <algorithm>
#include <cstdio>

namespace mpt::dump_lane {

// What a crash report says about the run. Plain data with fixed-size strings, so the supervisor can
// read it from shared memory in a dead or stopped process. Bump kCrashCrumbsVersion on any change.
struct CrashCrumbs {
  std::uint32_t magic;    // kCrashCrumbsMagic once the lane has written it
  std::uint32_t version;  // kCrashCrumbsVersion
  char mode[16];          // "boot", "party", "selftest", ...; "" before main.cpp names one
  std::uint64_t seed;
  std::uint64_t frame;  // the frame being stepped (its index, before the step)
  std::int32_t has_frame;
  std::int32_t has_party;  // phase, turn, board and overlay below are set
  std::int32_t board;
  std::int32_t turn;
  std::int32_t max_turns;
  char phase[16];
  char overlay[32];  // the running overlay's module name, e.g. "w01Dll" or a minigame's "m4xxDll"
  std::int32_t has_checkpoint;
  std::uint64_t checkpoint_frame;
  std::uint64_t checkpoint_hash;
};

inline constexpr std::uint32_t kCrashCrumbsMagic = 0x4D505443u;  // "MPTC"
inline constexpr std::uint32_t kCrashCrumbsVersion = 1;
// The environment variable through which `--supervise` hands its child the shared breadcrumbs.
inline constexpr const char* kCrashCrumbsEnv = "MPT_DUMP_LANE_CRASH_CRUMBS";

namespace crash_detail {
inline CrashCrumbs g_local_crumbs{};
inline CrashCrumbs* g_crumbs = &g_local_crumbs;  // the supervisor's shared copy under --supervise

inline void copy_name(char* to, std::size_t size, const char* from) {
  std::size_t n = 0;
  if (from != nullptr)
    for (; n + 1 < size && from[n] != 0; ++n) to[n] = from[n];
  std::memset(to + n, 0, size - n);
}
}  // namespace crash_detail

inline CrashCrumbs& crash_crumbs() { return *crash_detail::g_crumbs; }

inline void crash_note_run(const char* mode, std::uint64_t seed) {
  CrashCrumbs& c = crash_crumbs();
  crash_detail::copy_name(c.mode, sizeof c.mode, mode);
  c.seed = seed;
  c.version = kCrashCrumbsVersion;
  c.magic = kCrashCrumbsMagic;
}

inline void crash_note_frame(std::uint64_t frame) {
  CrashCrumbs& c = crash_crumbs();
  c.frame = frame;
  c.has_frame = 1;
}

inline void crash_note_party(const char* phase, const char* overlay, std::int32_t board, std::int32_t turn,
                             std::int32_t max_turns) {
  CrashCrumbs& c = crash_crumbs();
  crash_detail::copy_name(c.phase, sizeof c.phase, phase);
  crash_detail::copy_name(c.overlay, sizeof c.overlay, overlay != nullptr && overlay[0] != 0 ? overlay : "-");
  c.board = board;
  c.turn = turn;
  c.max_turns = max_turns;
  c.has_party = 1;
}

inline void crash_note_checkpoint(std::uint64_t frame, std::uint64_t hash) {
  CrashCrumbs& c = crash_crumbs();
  c.checkpoint_frame = frame;
  c.checkpoint_hash = hash;
  c.has_checkpoint = 1;
}

// The breadcrumb lines of a report, into `out` (always terminated). Returns the length written.
inline std::size_t format_crumbs(const CrashCrumbs& c, char* out, std::size_t size) {
  if (size == 0) return 0;
  out[0] = 0;
  if (c.magic != kCrashCrumbsMagic || c.version != kCrashCrumbsVersion) {
    std::snprintf(out, size, "run (no breadcrumbs: the lane crashed before it noted its run)\n");
    return std::strlen(out);
  }
  std::size_t n = 0;
  auto add = [&](const char* fmt, auto... args) {
    if (n + 1 >= size) return;
    int w = 0;
    if constexpr (sizeof...(args) == 0)
      w = std::snprintf(out + n, size - n, "%s", fmt);
    else
      w = std::snprintf(out + n, size - n, fmt, args...);
    if (w > 0) n = std::min(size - 1, n + static_cast<std::size_t>(w));
  };
  char mode[sizeof c.mode + 1] = {}, phase[sizeof c.phase + 1] = {}, overlay[sizeof c.overlay + 1] = {};
  std::memcpy(mode, c.mode, sizeof c.mode);
  std::memcpy(phase, c.phase, sizeof c.phase);
  std::memcpy(overlay, c.overlay, sizeof c.overlay);
  // Names only: anything outside [A-Za-z0-9_-] becomes '?', so a stray or corrupted name cannot put
  // a path, a newline or other text into the report.
  for (char* name : {mode, phase, overlay})
    for (char* ch = name; *ch != 0; ++ch) {
      const char v = *ch;
      if (!((v >= 'A' && v <= 'Z') || (v >= 'a' && v <= 'z') || (v >= '0' && v <= '9') || v == '_' || v == '-'))
        *ch = '?';
    }
  add("run mode %s seed %llu", mode[0] != 0 ? mode : "-", static_cast<unsigned long long>(c.seed));
  if (c.has_party) add(" board %d", static_cast<int>(c.board));
  add("\n");
  if (c.has_frame) add("frame %llu", static_cast<unsigned long long>(c.frame));
  else add("frame - (before the first step)");
  if (c.has_party)
    add(" phase %s turn %d/%d overlay %s", phase, static_cast<int>(c.turn), static_cast<int>(c.max_turns), overlay);
  add("\n");
  if (c.has_checkpoint)
    add("last checkpoint frame %llu hash %016llx\n", static_cast<unsigned long long>(c.checkpoint_frame),
        static_cast<unsigned long long>(c.checkpoint_hash));
  else
    add("last checkpoint - (none yet)\n");
  return n;
}

#if defined(_WIN32) && defined(_M_X64)
namespace crash_detail {

inline void print_address(const char* what, DWORD64 pc) {
  HMODULE module = nullptr;
  char name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(pc), &module) &&
      module != nullptr) {
    char path[MAX_PATH];
    if (GetModuleFileNameA(module, path, MAX_PATH) != 0) {
      const char* base = path;
      for (const char* c = path; *c != 0; ++c)
        if (*c == '\\' || *c == '/') base = c + 1;
      std::snprintf(name, sizeof name, "%s", base);
    }
    std::fprintf(stderr, "  %s 0x%016llx %s+0x%llx\n", what, static_cast<unsigned long long>(pc), name,
                 static_cast<unsigned long long>(pc - reinterpret_cast<DWORD64>(module)));
  } else {
    std::fprintf(stderr, "  %s 0x%016llx (no module)\n", what, static_cast<unsigned long long>(pc));
  }
}

// Whether the 8 bytes at `p` are committed, readable memory: the walk runs inside a crash, so a corrupt
// stack pointer must end it instead of faulting a second time.
inline bool readable_u64(DWORD64 p) {
  if (p == 0 || (p & 7) != 0) return false;
  MEMORY_BASIC_INFORMATION mbi;
  if (VirtualQuery(reinterpret_cast<LPCVOID>(p), &mbi, sizeof mbi) == 0) return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
  const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  if ((mbi.Protect & readable) == 0) return false;
  return p + 8 <= reinterpret_cast<DWORD64>(mbi.BaseAddress) + mbi.RegionSize;
}

inline void walk_frames(CONTEXT& ctx) {
  for (int depth = 0; depth < 48; ++depth) {
    DWORD64 image = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &image, nullptr);
    if (fn == nullptr) {  // a leaf function: the return address is at the stack pointer
      if (!readable_u64(ctx.Rsp)) {
        std::fprintf(stderr, "  (stack pointer 0x%016llx unreadable, walk ends)\n",
                     static_cast<unsigned long long>(ctx.Rsp));
        break;
      }
      ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
      ctx.Rsp += 8;
    } else {
      void* handler_data = nullptr;
      DWORD64 frame = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx.Rip, fn, &ctx, &handler_data, &frame, nullptr);
    }
    if (ctx.Rip == 0) break;
    print_address("by", ctx.Rip);
  }
}

// The breadcrumbs, then the walk. Only the leaf case is checked with VirtualQuery: RtlVirtualUnwind
// reads the stack itself and can still fault on a corrupt one, and a second fault inside the handler
// would hide the report, so the walk runs under __try (no C++ objects in this frame, as SEH needs).
inline void walk(CONTEXT ctx) {
  char crumbs[512];
  format_crumbs(crash_crumbs(), crumbs, sizeof crumbs);
  std::fputs(crumbs, stderr);
  print_address("at", ctx.Rip);
  __try {
    walk_frames(ctx);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    std::fputs("  (the walk faulted on a corrupt stack, walk ends)\n", stderr);
  }
  std::fflush(stderr);
}

// The walk from here: the first frames printed are walk_here itself, the hook that called it and the
// CRT's way into the hook (raise and abort, terminate, or the invalid-parameter dispatch); the code
// that called abort(), std::terminate or the failing CRT function follows them.
inline void walk_here(const char* why) {
  std::fprintf(stderr, "dump lane: crash, %s\n", why);
  CONTEXT ctx{};
  RtlCaptureContext(&ctx);
  walk(ctx);
}

inline LONG WINAPI on_crash(EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD* r = info->ExceptionRecord;
  // First chance: only the faults that end the process (C++ exceptions and the like pass by).
  switch (r->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_PRIV_INSTRUCTION:
    case STATUS_STACK_BUFFER_OVERRUN:  // only when raised as an exception; a real __fastfail skips handlers
      break;
    default:
      return EXCEPTION_CONTINUE_SEARCH;
  }
  std::fprintf(stderr, "dump lane: crash, exception 0x%08lx", static_cast<unsigned long>(r->ExceptionCode));
  if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
    std::fprintf(stderr, " (%s 0x%016llx)", r->ExceptionInformation[0] == 0 ? "read" : "write",
                 static_cast<unsigned long long>(r->ExceptionInformation[1]));
  std::fputc('\n', stderr);
  walk(*info->ContextRecord);
  return EXCEPTION_CONTINUE_SEARCH;
}

inline void on_abort(int) {
  walk_here("abort() (SIGABRT)");
  std::_Exit(3);  // the exit code abort() gives, without its fail-fast
}

inline void on_terminate() {
  walk_here("std::terminate");
  std::_Exit(3);
}

inline void on_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
  walk_here("CRT invalid parameter");
  std::_Exit(3);
}

}  // namespace crash_detail

// A first-chance vectored handler: it runs before any frame-based handler, even when the fault is on
// a game coroutine's stack, and whatever filter a library installs later. MP4's copy trap
// (runtimes/gc_wii/mp4/src/copy_trap.cpp) is also a first-in-line vectored handler. That works only
// because the trap is installed later (when a game is created, after this runs at startup), so it is
// called first and resumes its own faults before this handler sees them. Install this one first.
// Under --supervise the breadcrumbs move into the supervisor's shared memory (supervise.h), named by
// kCrashCrumbsEnv.
inline void install_crash_report() {
  char name[128];
  const DWORD length = GetEnvironmentVariableA(kCrashCrumbsEnv, name, sizeof name);
  if (length != 0 && length < sizeof name) {
    if (HANDLE mapping = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name); mapping != nullptr) {
      // The view stays mapped for the life of the process; the handle can go.
      void* view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(CrashCrumbs));
      CloseHandle(mapping);
      if (view != nullptr) crash_detail::g_crumbs = static_cast<CrashCrumbs*>(view);
    }
  }
  AddVectoredExceptionHandler(1, &crash_detail::on_crash);
  std::signal(SIGABRT, &crash_detail::on_abort);
  std::set_terminate(&crash_detail::on_terminate);
  _set_invalid_parameter_handler(&crash_detail::on_invalid_parameter);
}
#else
inline void install_crash_report() {}
#endif

}  // namespace mpt::dump_lane
