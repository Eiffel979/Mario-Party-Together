#pragma once

// A crash report for the dump lane (Windows x64): on a fatal exception (first chance), print the exception
// code, the faulting address and a stack walk, each frame as module+offset, then let the process die as it
// would have. The MP4 module carries a COFF symbol table (/DEBUG:SYMTAB), so `llvm-nm -n` on the
// installed mp4_game.dll names every frame inside it, even though the recipe keeps no PDB. Game code
// runs on coroutine stacks; the walk follows the unwind data, which every Clang-built function has.
//
// A fail-fast (0xC0000409, __fastfail) never reaches an exception handler, so the CRT's ways into one are
// hooked too: abort() (SIGABRT, which std::terminate also ends in), std::terminate and the CRT's invalid-
// parameter handler. Each prints the same walk from where it was called. The host and the module share
// the dynamic UCRT and VCRUNTIME, so the hooks cover both. A /GS stack-cookie failure fails fast without
// any hook; it still ends the run with no report.
// A test-harness aid only: nothing here touches game state, and other platforms get a no-op.

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

#include <cstdio>

namespace mpt::dump_lane {

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

inline void walk(CONTEXT ctx) {
  print_address("at", ctx.Rip);
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
  std::fflush(stderr);
}

// The walk from the caller of the hook that called this (abort, terminate, invalid parameter).
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
inline void install_crash_report() {
  AddVectoredExceptionHandler(1, &crash_detail::on_crash);
  std::signal(SIGABRT, &crash_detail::on_abort);
  std::set_terminate(&crash_detail::on_terminate);
  _set_invalid_parameter_handler(&crash_detail::on_invalid_parameter);
}
#else
inline void install_crash_report() {}
#endif

}  // namespace mpt::dump_lane
