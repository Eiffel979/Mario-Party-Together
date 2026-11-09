// Real linkers keep the function type (ADR 0008, "Module words: a safe rule must not depend on
// layout", point 6): loads the function-start stub module (function_starts_module.cpp), which the
// Clang build links with lld-link on Windows and ld.lld on Linux, and checks its report. No game code
// and no dump, so public CI runs it on both platforms.
#include <cstddef>
#include <cstdio>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using StartsFn = int (*)(char*, std::size_t);

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: mpt_mp4_function_starts_tests <stub module>\n");
    return 2;
  }
#if defined(_WIN32)
  HMODULE module = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (module == nullptr) {
    std::fprintf(stderr, "cannot load %s (error %lu)\n", argv[1], GetLastError());
    return 1;
  }
  auto fn = reinterpret_cast<StartsFn>(reinterpret_cast<void*>(GetProcAddress(module, "mpt_mp4_stub_function_starts")));
#else
  void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (module == nullptr) {
    std::fprintf(stderr, "cannot load %s: %s\n", argv[1], dlerror());
    return 1;
  }
  auto fn = reinterpret_cast<StartsFn>(dlsym(module, "mpt_mp4_stub_function_starts"));
#endif
  if (fn == nullptr) {
    std::fprintf(stderr, "the stub does not export mpt_mp4_stub_function_starts\n");
    return 1;
  }
  static char report[4096];
  const int failures = fn(report, sizeof report);
  std::fputs(report, stdout);
  std::printf("mp4_function_starts: %d failure(s)\n", failures);
#if defined(_WIN32)
  FreeLibrary(module);
#else
  dlclose(module);
#endif
  return failures == 0 ? 0 : 1;
}
