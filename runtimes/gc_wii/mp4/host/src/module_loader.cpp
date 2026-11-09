// See module_loader.h.
#include "module_loader.h"

#include <cstdint>
#include <cstdio>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#include <link.h>
#endif

namespace mpt::mp4 {
namespace {

struct Loaded {
  std::filesystem::path path;
  void* handle = nullptr;
  const MptMp4Api* api = nullptr;
  int holds = 0;
};

Loaded g_loaded;  // one module per process at a time

// A path for a message: UTF-8 on every platform (path::string() is the ANSI code page on Windows).
std::string utf8(const std::filesystem::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

// Unloads a module that failed the checks below, so a failed boot leaves nothing mapped.
void unload(void* handle) {
#if defined(_WIN32)
  FreeLibrary(static_cast<HMODULE>(handle));
#else
  dlclose(handle);
#endif
}

std::string hex(std::uintptr_t v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
  return buf;
}

// The end of the loaded image, from its headers in memory: SizeOfImage on Windows, the highest PT_LOAD
// segment on ELF. 0 when it cannot be found.
std::uintptr_t imageEnd([[maybe_unused]] std::uintptr_t base, [[maybe_unused]] const void* inside) {
#if defined(_WIN32)
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + static_cast<std::uintptr_t>(dos->e_lfanew));
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
  return base + nt->OptionalHeader.SizeOfImage;
#else
  struct Find {
    std::uintptr_t inside, end;
  } find{reinterpret_cast<std::uintptr_t>(inside), 0};
  dl_iterate_phdr(
      [](dl_phdr_info* info, std::size_t, void* user) -> int {
        auto* f = static_cast<Find*>(user);
        std::uintptr_t lo = ~std::uintptr_t{0}, hi = 0;
        for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
          const ElfW(Phdr)& ph = info->dlpi_phdr[i];
          if (ph.p_type != PT_LOAD) continue;
          const std::uintptr_t a = info->dlpi_addr + ph.p_vaddr;
          if (a < lo) lo = a;
          if (a + ph.p_memsz > hi) hi = a + ph.p_memsz;
        }
        if (f->inside < lo || f->inside >= hi) return 0;
        f->end = hi;
        return 1;
      },
      &find);
  return find.end;
#endif
}

}  // namespace

const MptMp4Api* load_module(const std::filesystem::path& path, std::uintptr_t expected_base, std::string& error) {
  std::error_code ec;
  const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
  if (g_loaded.api != nullptr) {
    if (canonical == g_loaded.path) {
      ++g_loaded.holds;
      return g_loaded.api;
    }
    error = "another MP4 game module is already loaded in this process (" + utf8(g_loaded.path) + ")";
    return nullptr;
  }
  if (!std::filesystem::is_regular_file(canonical, ec)) {
    error = "no game module at " + utf8(path) + "; build the title first";
    return nullptr;
  }
  void* handle = nullptr;
  void* get_api = nullptr;
  std::uintptr_t base = 0;
#if defined(_WIN32)
  HMODULE module = LoadLibraryExW(canonical.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (module == nullptr) {
    const DWORD code = GetLastError();
    error = "cannot load " + utf8(canonical) + " (error " + std::to_string(code) + ")";
    // The module has no relocations (/DYNAMICBASE:NO /FIXED, ADR 0008 section 11), so it loads only at
    // its own base: when that range is taken, or when Windows forces ASLR on images without it
    // (Exploit protection, "Force randomization for images (Mandatory ASLR)", system-wide or for this
    // executable), the load fails. Mandatory ASLR must be off for the shell and the dump lane.
    if (code == ERROR_INVALID_ADDRESS || code == ERROR_ILLEGAL_DLL_RELOCATION || code == 487)
      error += ": its fixed base " + hex(expected_base) +
               " is in use in this process, or Mandatory ASLR (Exploit protection) is forced for it";
    return nullptr;
  }
  handle = module;
  base = reinterpret_cast<std::uintptr_t>(module);
  get_api = reinterpret_cast<void*>(GetProcAddress(module, "mpt_mp4_get_api"));
#else
  handle = dlopen(canonical.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    const char* why = dlerror();
    error = "cannot load " + utf8(canonical) + ": " + (why ? why : "unknown error");
    return nullptr;
  }
  get_api = dlsym(handle, "mpt_mp4_get_api");
  Dl_info info{};
  if (get_api != nullptr && dladdr(get_api, &info) != 0) base = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
#endif
  if (get_api == nullptr) {
    error = utf8(canonical) + " does not export mpt_mp4_get_api";
    unload(handle);
    return nullptr;
  }
  if (base != expected_base) {
    // A module elsewhere would make every function pointer in a save state wrong (ADR 0008 section 11).
    error = "the game module loaded at " + hex(base) + ", not at its fixed base " + hex(expected_base);
    unload(handle);
    return nullptr;
  }
  // Every module address must share the base's upper 32 bits (ADR 0008, "Module words: a safe rule
  // must not depend on layout", point 1): the image ends at or below the next 4 GiB boundary, which
  // for MPT_MP4_MODULE_BASE is 0x5B98'0000'0000.
  const std::uintptr_t end = imageEnd(base, get_api);
  const std::uintptr_t limit = (expected_base | std::uintptr_t{0xFFFF'FFFF}) + 1;
  if (end == 0 || end > limit) {
    error = end == 0 ? "cannot read the size of the game module's image"
                     : "the game module's image ends at " + hex(end) + ", above " + hex(limit) +
                           ": it must not cross a 4 GiB boundary";
    unload(handle);
    return nullptr;
  }
  const auto fn = reinterpret_cast<MptMp4GetApiFn>(get_api);
  const MptMp4Api* api = fn(MPT_MP4_ABI_VERSION);
  // The host calls status() (v1.1), so it needs a v1.1 table.
  if (!mpt_mp4_api_has_v1_1(api)) {
    error = "the game module does not implement ABI version " + std::to_string(MPT_MP4_ABI_VERSION) + ".1";
    unload(handle);
    return nullptr;
  }
  g_loaded = {canonical, handle, api, 1};
  return api;
}

void release_module() {
  if (g_loaded.api == nullptr || --g_loaded.holds > 0) return;
  unload(g_loaded.handle);
  g_loaded = {};
}

}  // namespace mpt::mp4
