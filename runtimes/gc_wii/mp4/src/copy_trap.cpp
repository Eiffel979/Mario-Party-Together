// The copy-destination debug trap (ADR 0007 section 5, mp4.md section 8). See copy_trap.h.
#include "copy_trap.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mpt/gx/renderer.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#endif

extern "C" const char* mpt_mp4_current_overlay_name(void);  // game_reads.c

namespace mpt::mp4 {
namespace {

// Overlays that read a copy destination on purpose. m415 (Stamp Out!) memcpys its shadow-map copy
// into a canvas texture; the bytes are presentation only and stay zero here (found by spike S1).
const char* const kAllowed[] = {"m415Dll"};

struct Range {
  std::uintptr_t lo;  // whole pages inside the destination: only these are protected
  std::uintptr_t hi;
  std::uintptr_t dest;
};

bool g_enabled = false;
bool g_armed = false;
std::vector<Range> g_ranges;
std::string g_violation;
std::uintptr_t g_page = 0;

#if defined(_WIN32)
PVOID g_handler = nullptr;
#else
struct sigaction g_previous {};
#endif

void protect(const Range& r, bool on) {
#if defined(_WIN32)
  DWORD old = 0;
  VirtualProtect(reinterpret_cast<void*>(r.lo), r.hi - r.lo, on ? PAGE_NOACCESS : PAGE_READWRITE, &old);
#else
  mprotect(reinterpret_cast<void*>(r.lo), r.hi - r.lo, on ? PROT_NONE : (PROT_READ | PROT_WRITE));
#endif
}

bool allowedOverlay() {
  const char* name = mpt_mp4_current_overlay_name();
  if (name == nullptr) return false;
  for (const char* a : kAllowed)
    if (std::string(name) == a) return true;
  return false;
}

// Returns true if the fault was on a trapped range (now released and recorded if it was a read).
bool onFault(std::uintptr_t addr, bool write) {
  for (std::size_t i = 0; i < g_ranges.size(); ++i) {
    const Range r = g_ranges[i];
    if (addr < r.lo || addr >= r.hi) continue;
    protect(r, false);
    g_ranges.erase(g_ranges.begin() + static_cast<std::ptrdiff_t>(i));
    // A CPU write means the memory is in other use now (the heap reused it, or the game cleared it).
    if (!write && !allowedOverlay() && g_violation.empty()) {
      char buf[192];
      const char* ovl = mpt_mp4_current_overlay_name();
      std::snprintf(buf, sizeof buf, "CPU read of a GXCopyTex destination (+0x%llx) in overlay %s",
                    static_cast<unsigned long long>(addr - r.dest), ovl ? ovl : "?");
      g_violation = buf;
    }
    return true;
  }
  return false;
}

#if defined(_WIN32)
LONG CALLBACK handler(EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD* e = info->ExceptionRecord;
  if (e->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || e->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
  const bool write = e->ExceptionInformation[0] == 1;
  return onFault(static_cast<std::uintptr_t>(e->ExceptionInformation[1]), write) ? EXCEPTION_CONTINUE_EXECUTION
                                                                                 : EXCEPTION_CONTINUE_SEARCH;
}
#else
void handler(int sig, siginfo_t* info, void* ctx) {
  const auto* uc = static_cast<const ucontext_t*>(ctx);
  const bool write = (uc->uc_mcontext.gregs[REG_ERR] & 2) != 0;  // x86-64 page-fault error code
  if (onFault(reinterpret_cast<std::uintptr_t>(info->si_addr), write)) return;
  // Not ours: hand it to the previous handler, or crash as usual.
  if (g_previous.sa_flags & SA_SIGINFO) {
    g_previous.sa_sigaction(sig, info, ctx);
  } else if (g_previous.sa_handler != SIG_DFL && g_previous.sa_handler != SIG_IGN) {
    g_previous.sa_handler(sig);
  } else {
    signal(sig, SIG_DFL);
    raise(sig);
  }
}
#endif

void observer(void* dest, std::uint32_t size) {
  const auto d = reinterpret_cast<std::uintptr_t>(dest);
  const std::uintptr_t lo = (d + g_page - 1) & ~(g_page - 1);
  const std::uintptr_t hi = (d + size) & ~(g_page - 1);
  if (hi <= lo) return;  // smaller than a page: not trapped
  for (const Range& r : g_ranges)
    if (r.lo == lo && r.hi == hi) return;
  // A new copy over part of an old range replaces it.
  for (std::size_t i = g_ranges.size(); i-- > 0;) {
    if (g_ranges[i].lo < hi && lo < g_ranges[i].hi) {
      protect(g_ranges[i], false);
      g_ranges.erase(g_ranges.begin() + static_cast<std::ptrdiff_t>(i));
    }
  }
  g_ranges.push_back({lo, hi, d});
  if (g_armed) protect(g_ranges.back(), true);
}

}  // namespace

void copy_trap_enable() {
  if (g_enabled) return;
#if defined(_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  g_page = si.dwPageSize;
  g_handler = AddVectoredExceptionHandler(1, &handler);
#else
  g_page = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
  struct sigaction sa {};
  sa.sa_sigaction = &handler;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, &g_previous);
#endif
  g_enabled = true;
  mpt::gx::set_copy_tex_observer(&observer);
}

void copy_trap_disable() {
  if (!g_enabled) return;
  copy_trap_disarm();
  mpt::gx::set_copy_tex_observer(nullptr);
#if defined(_WIN32)
  RemoveVectoredExceptionHandler(g_handler);
  g_handler = nullptr;
#else
  sigaction(SIGSEGV, &g_previous, nullptr);
#endif
  g_ranges.clear();
  g_violation.clear();
  g_enabled = false;
}

void copy_trap_arm() {
  if (!g_enabled || g_armed) return;
  for (const Range& r : g_ranges) protect(r, true);
  g_armed = true;
}

void copy_trap_disarm() {
  if (!g_armed) return;
  for (const Range& r : g_ranges) protect(r, false);
  g_armed = false;
}

void copy_trap_forget() { g_ranges.clear(); }

std::string copy_trap_take_violation() {
  std::string v;
  v.swap(g_violation);
  return v;
}

}  // namespace mpt::mp4
