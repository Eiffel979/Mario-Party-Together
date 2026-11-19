// Stack guard values in game memory, for the E5 round trip (M4-16; ADR 0008, "Stack guard values in
// save states"). Test-only: nothing here is in the shell or the game module, and nothing changes state.
//
// A /GS function (Windows) writes `__security_cookie XOR <its stack or frame pointer>` into its frame on
// entry and checks it on return. The game's coroutine stacks are in the arena, so a game function that
// is suspended at a save has that word in the saved arena. find_guards() finds such words: an aligned
// word whose value XOR the cookie is an address close to the word itself. With a random 48-bit cookie
// a word that is not a guard passes that test only by chance (the cookie's top 16 bits are zero, so the
// word must hold the cookie's bits above the reach, about one in 2^32 for any other value).
//
// On Linux the stack protector's canary belongs to the process and is stored as it is (ADR 0008, same
// section, point 6), so a module reload does not change it; the lane does not scan for it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mpt::dump_lane::stack_guard {

struct Guard {
  std::uintptr_t slot = 0;   // where the guard word is
  std::uintptr_t keyed = 0;  // the stack or frame pointer it was mixed with (value XOR cookie)
};

// Every 8-byte aligned word of [data, data + bytes), which lies at address `base` in the game's memory,
// whose value XOR `cookie` is within `reach` bytes of the word's own address. A pure function, so the
// synthetic test can feed it made-up memory.
inline std::vector<Guard> find_guards(const std::byte* data, std::uintptr_t base, std::size_t bytes,
                                      std::uint64_t cookie, std::uint64_t reach = 0x10000) {
  std::vector<Guard> out;
  if (cookie == 0) return out;
  for (std::size_t off = 0; off + 8 <= bytes; off += 8) {
    std::uint64_t w;
    std::memcpy(&w, data + off, sizeof w);
    const std::uint64_t keyed = w ^ cookie;
    const std::uint64_t slot = base + off;
    const std::uint64_t distance = keyed > slot ? keyed - slot : slot - keyed;
    if (distance < reach) out.push_back({static_cast<std::uintptr_t>(slot), static_cast<std::uintptr_t>(keyed)});
  }
  return out;
}

#if defined(_WIN32) && defined(_M_X64)
// The /GS cookie of a loaded image, read through its load config directory (SecurityCookie is the
// cookie's address). nullopt when the image has no load config or no cookie.
inline std::optional<std::uint64_t> module_cookie(HMODULE module) {
  if (module == nullptr) return std::nullopt;
  const auto base = reinterpret_cast<std::uintptr_t>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return std::nullopt;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + static_cast<std::uintptr_t>(dos->e_lfanew));
  if (nt->Signature != IMAGE_NT_SIGNATURE) return std::nullopt;
  const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
  if (dir.VirtualAddress == 0 || dir.Size < offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, SecurityCookie) + 8)
    return std::nullopt;
  const auto* config = reinterpret_cast<const IMAGE_LOAD_CONFIG_DIRECTORY64*>(base + dir.VirtualAddress);
  if (config->SecurityCookie == 0) return std::nullopt;
  std::uint64_t cookie;
  std::memcpy(&cookie, reinterpret_cast<const void*>(config->SecurityCookie), sizeof cookie);
  return cookie;
}

// find_guards() over every committed, readable page of the reservation that starts at `arena_base`.
inline std::vector<Guard> arena_guards(std::uintptr_t arena_base, std::uint64_t cookie) {
  std::vector<Guard> out;
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(reinterpret_cast<const void*>(arena_base), &mbi, sizeof mbi) == 0) return out;
  const void* allocation = mbi.AllocationBase;
  for (std::uintptr_t at = arena_base;
       VirtualQuery(reinterpret_cast<const void*>(at), &mbi, sizeof mbi) != 0 && mbi.AllocationBase == allocation;
       at = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & readable) == 0 || (mbi.Protect & PAGE_GUARD) != 0) continue;
    const auto region = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    const std::vector<Guard> found =
        find_guards(reinterpret_cast<const std::byte*>(region), region, mbi.RegionSize, cookie);
    out.insert(out.end(), found.begin(), found.end());
  }
  return out;
}
#endif

// `--guard-reload-selftest STUB keep|reload` (Windows x64, no dump): the mechanism of the E5 failure on
// a synthetic stub (tests/mp4_loader/guard_stub.cpp). A fiber calls the stub's guarded function, which
// yields back with its frame suspended on the fiber's stack. The lane finds the guard word on that
// stack with find_guards(), then either keeps the module loaded while it drops its own reference
// (`keep`) or unloads and loads it again (`reload`), prints the cookie before and after, and resumes the
// fiber. With `keep` the function returns and the lane exits 0; with `reload` the cookie is new and the
// function's /GS check fails fast on return (0xC0000409), which `--supervise` reports. nullopt when the
// option is not given.
#if defined(_WIN32) && defined(_M_X64)
namespace detail {
struct Fiber {
  void* main = nullptr;
  int (*suspend)(void (*)(void*), void*, int) = nullptr;
  std::uintptr_t stack_probe = 0;
  int result = -1;
  bool done = false;
};
inline void yield(void* user) {
  auto* f = static_cast<Fiber*>(user);
  volatile int here = 0;
  f->stack_probe = reinterpret_cast<std::uintptr_t>(&here);
  SwitchToFiber(f->main);
}
inline void __stdcall run(void* user) {
  auto* f = static_cast<Fiber*>(user);
  f->result = f->suspend(&yield, f, 64);
  f->done = true;
  SwitchToFiber(f->main);
}
}  // namespace detail

inline std::optional<int> guard_reload_selftest_if_asked(int argc, char** argv) {
  int at = -1;
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--guard-reload-selftest") == 0) at = i;
  if (at < 0) return std::nullopt;
  if (at + 2 >= argc || (std::strcmp(argv[at + 2], "keep") != 0 && std::strcmp(argv[at + 2], "reload") != 0)) {
    std::puts("usage: mpt_dump_lane [--supervise] --guard-reload-selftest STUB keep|reload");
    return 2;
  }
  const char* stub = argv[at + 1];
  const bool reload = std::strcmp(argv[at + 2], "reload") == 0;
  HMODULE module = LoadLibraryA(stub);
  if (module == nullptr) {
    std::printf("guard selftest: cannot load the stub (error %lu)\n", GetLastError());
    return 1;
  }
  const auto base = reinterpret_cast<std::uintptr_t>(module);
  detail::Fiber f;
  f.suspend = reinterpret_cast<int (*)(void (*)(void*), void*, int)>(GetProcAddress(module, "mpt_guard_stub_suspend"));
  const std::optional<std::uint64_t> before = module_cookie(module);
  f.main = ConvertThreadToFiber(nullptr);
  void* fiber = f.suspend != nullptr && f.main != nullptr ? CreateFiber(256 * 1024, &detail::run, &f) : nullptr;
  if (!before || fiber == nullptr) {
    std::puts("guard selftest: no stub export, no /GS cookie or no fiber");
    return 1;
  }
  SwitchToFiber(fiber);  // runs until the stub's function yields
  // The guarded frame lies above the yield callback's frame, below the top of the fiber's stack.
  const std::uintptr_t from = f.stack_probe & ~std::uintptr_t{7};
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(reinterpret_cast<const void*>(from), &mbi, sizeof mbi) == 0) return 1;
  const std::uintptr_t top = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
  const std::vector<Guard> guards =
      find_guards(reinterpret_cast<const std::byte*>(from), from, static_cast<std::size_t>(top - from), *before);
  std::printf("guard selftest: suspended, %zu guard word(s) on the fiber's stack under cookie %012llx\n",
              guards.size(), static_cast<unsigned long long>(*before));
  HMODULE extra = nullptr;
  if (!reload) GetModuleHandleExA(0, stub, &extra);  // `keep`: one more reference, so the free below keeps it
  FreeLibrary(module);
  const bool unloaded = GetModuleHandleA(stub) == nullptr;
  if (reload) module = LoadLibraryA(stub);
  const std::optional<std::uint64_t> after = module_cookie(module);
  std::printf("guard selftest: module %s, at the same base %s, cookie %012llx (%s)\n",
              unloaded ? "unloaded and loaded again" : "kept loaded",
              reinterpret_cast<std::uintptr_t>(module) == base ? "yes" : "NO",
              static_cast<unsigned long long>(after.value_or(0)),
              after && *after == *before ? "same as before" : "CHANGED");
  if (guards.empty() || reinterpret_cast<std::uintptr_t>(module) != base || unloaded != reload) {
    std::puts("guard selftest: FAILED to set up the case");
    return 1;
  }
  SwitchToFiber(fiber);  // the stub's function returns: its /GS check runs against the current cookie
  std::printf("guard selftest: the guarded function returned %d\n", f.result);
  DeleteFiber(fiber);
  FreeLibrary(module);
  return f.done && f.result == 63 ? 0 : 1;
}
#else
inline std::optional<int> guard_reload_selftest_if_asked(int, char**) { return std::nullopt; }
#endif

}  // namespace mpt::dump_lane::stack_guard
