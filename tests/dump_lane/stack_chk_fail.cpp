// The dump lane's own __stack_chk_fail (Linux, M4-04): the stack protector's failure path, called by a
// function whose canary was overwritten. glibc's version prints "stack smashing detected" and aborts,
// with no word of where. This one writes a crash report first: the breadcrumbs (crash_report.h) and
// the failing function as module+offset and its nearest dynamic symbol (dladdr), to stderr and to
// --crash-report FILE. The lane exports the symbol (tests/CMakeLists.txt), so the game module, which
// is a shared object, binds its calls here rather than to glibc.
//
// Compile-only for now: the Linux runtime work, and with it a run of this path, waits on phase-1.md
// M4-U2. Nothing here may itself be stack-protected, allocate, or rely on the corrupted frame: the
// report is formatted into static storage and written with write(2).
#if defined(__linux__)

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "crash_report.h"
#include "supervise.h"

namespace {

char g_report[4096];

__attribute__((no_stack_protector)) void put(std::size_t& n, const char* text) {
  const std::size_t len = std::strlen(text);
  if (n + len >= sizeof g_report) return;
  std::memcpy(g_report + n, text, len);
  n += len;
  g_report[n] = 0;
}

__attribute__((no_stack_protector)) void write_all(int fd, const char* p, std::size_t len) {
  while (len > 0) {
    const ssize_t w = write(fd, p, len);
    if (w <= 0) return;
    p += w;
    len -= static_cast<std::size_t>(w);
  }
}

}  // namespace

extern "C" __attribute__((noreturn, no_stack_protector, visibility("default"))) void __stack_chk_fail(void) {
  void* caller = __builtin_return_address(0);
  static char line[512];
  std::size_t n = 0;
  g_report[0] = 0;
  put(n, "dump lane crash report v1\nstatus __stack_chk_fail (stack protector: a stack buffer overrun)\n");
  static char crumbs[512];
  mpt::dump_lane::format_crumbs(mpt::dump_lane::crash_crumbs(), crumbs, sizeof crumbs);
  put(n, crumbs);
  Dl_info info{};
  if (dladdr(caller, &info) != 0 && info.dli_fname != nullptr) {
    const char* base = info.dli_fname;
    for (const char* c = info.dli_fname; *c != 0; ++c)
      if (*c == '/') base = c + 1;
    const auto off = static_cast<unsigned long long>(static_cast<const char*>(caller) -
                                                     static_cast<const char*>(info.dli_fbase));
    if (info.dli_sname != nullptr && info.dli_saddr != nullptr)
      std::snprintf(line, sizeof line, "culprit %s+0x%llx %s+0x%llx\n", base, off, info.dli_sname,
                    static_cast<unsigned long long>(static_cast<const char*>(caller) -
                                                    static_cast<const char*>(info.dli_saddr)));
    else
      std::snprintf(line, sizeof line, "culprit %s+0x%llx\n", base, off);
  } else {
    std::snprintf(line, sizeof line, "culprit %p (no module)\n", caller);
  }
  put(n, line);
  put(n, "end of crash report\n");
  write_all(STDERR_FILENO, g_report, n);
  if (mpt::dump_lane::g_crash_report_path[0] != 0) {
    const int fd = open(mpt::dump_lane::g_crash_report_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) {
      write_all(fd, g_report, n);
      close(fd);
    }
  }
  std::abort();
}

#endif  // __linux__
