#pragma once

// A crash report for the dump lane (Windows): on a fatal exception (first chance), print the exception code,
// the faulting address and a stack walk, each frame as module+offset, then let the process die as it
// would have. The MP4 module carries a COFF symbol table (/DEBUG:SYMTAB), so `llvm-nm -n` on the
// installed mp4_game.dll names every frame inside it, even though the recipe keeps no PDB. Game code
// runs on coroutine stacks; the walk follows the unwind data, which every Clang-built function has.
// A test-harness aid only: nothing here touches game state, and other platforms get a no-op.

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstdio>

namespace mpt::dump_lane {

#if defined(_WIN32)
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

inline LONG WINAPI on_crash(EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD* r = info->ExceptionRecord;
  // First chance: only the faults that end the process (C++ exceptions and the like pass by).
  switch (r->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_PRIV_INSTRUCTION:
      break;
    default:
      return EXCEPTION_CONTINUE_SEARCH;
  }
  std::fprintf(stderr, "dump lane: crash, exception 0x%08lx", static_cast<unsigned long>(r->ExceptionCode));
  if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
    std::fprintf(stderr, " (%s 0x%016llx)", r->ExceptionInformation[0] == 0 ? "read" : "write",
                 static_cast<unsigned long long>(r->ExceptionInformation[1]));
  std::fputc('\n', stderr);
  CONTEXT ctx = *info->ContextRecord;
  print_address("at", ctx.Rip);
  for (int depth = 0; depth < 48; ++depth) {
    DWORD64 image = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &image, nullptr);
    if (fn == nullptr) {  // a leaf function: the return address is at the stack pointer
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
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace crash_detail

// A first-chance vectored handler: it runs before any frame-based handler, even when the fault is on
// a game coroutine's stack, and whatever filter a library installs later.
inline void install_crash_report() { AddVectoredExceptionHandler(1, &crash_detail::on_crash); }
#else
inline void install_crash_report() {}
#endif

}  // namespace mpt::dump_lane
