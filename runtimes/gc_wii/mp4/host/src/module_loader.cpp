// See module_loader.h.
#include "module_loader.h"

#include <cstdint>
#include <cstdio>
#include <mutex>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mpt::mp4 {
namespace {

struct Loaded {
  std::filesystem::path path;
  void* handle = nullptr;
  const MptMp4Api* api = nullptr;
};

Loaded g_loaded;  // one module per process, for the rest of the process

std::string hex(std::uintptr_t v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
  return buf;
}

}  // namespace

const MptMp4Api* load_module(const std::filesystem::path& path, std::uintptr_t expected_base, std::string& error) {
  std::error_code ec;
  const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
  if (g_loaded.api != nullptr) {
    if (canonical == g_loaded.path) return g_loaded.api;
    error = "another MP4 game module is already loaded in this process (" + g_loaded.path.string() + ")";
    return nullptr;
  }
  if (!std::filesystem::is_regular_file(canonical, ec)) {
    error = "no game module at " + path.string() + "; build the title first";
    return nullptr;
  }
  void* handle = nullptr;
  void* get_api = nullptr;
  std::uintptr_t base = 0;
#if defined(_WIN32)
  HMODULE module = LoadLibraryExW(canonical.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (module == nullptr) {
    const DWORD code = GetLastError();
    error = "cannot load " + canonical.string() + " (error " + std::to_string(code) + ")";
    if (code == ERROR_INVALID_ADDRESS || code == ERROR_ILLEGAL_DLL_RELOCATION || code == 487)
      error += ": its fixed base " + hex(expected_base) + " is in use in this process";
    return nullptr;
  }
  handle = module;
  base = reinterpret_cast<std::uintptr_t>(module);
  get_api = reinterpret_cast<void*>(GetProcAddress(module, "mpt_mp4_get_api"));
#else
  handle = dlopen(canonical.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    const char* why = dlerror();
    error = "cannot load " + canonical.string() + ": " + (why ? why : "unknown error");
    return nullptr;
  }
  get_api = dlsym(handle, "mpt_mp4_get_api");
  Dl_info info{};
  if (get_api != nullptr && dladdr(get_api, &info) != 0) base = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
#endif
  if (get_api == nullptr) {
    error = canonical.string() + " does not export mpt_mp4_get_api";
    return nullptr;
  }
  if (base != expected_base) {
    // A module elsewhere would make every function pointer in a save state wrong (ADR 0008 section 11).
    error = "the game module loaded at " + hex(base) + ", not at its fixed base " + hex(expected_base);
    return nullptr;
  }
  const auto fn = reinterpret_cast<MptMp4GetApiFn>(get_api);
  const MptMp4Api* api = fn(MPT_MP4_ABI_VERSION);
  // The host calls status() (v1.1), so it needs a v1.1 table.
  if (api == nullptr || api->struct_size < MPT_MP4_API_SIZE_V1_1 || api->abi_version != MPT_MP4_ABI_VERSION ||
      api->status == nullptr) {
    error = "the game module does not implement ABI version " + std::to_string(MPT_MP4_ABI_VERSION) + ".1";
    return nullptr;
  }
  g_loaded = {canonical, handle, api};
  return api;
}

}  // namespace mpt::mp4
