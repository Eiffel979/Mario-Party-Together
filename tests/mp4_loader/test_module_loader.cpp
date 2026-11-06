// mpt::mp4::load_module (runtimes/gc_wii/mp4/host/src/module_loader.cpp) on stub game modules
// (stub_module.cpp): the fixed image base on this OS, one module per process with shared holds, and
// every rejection unloading what it loaded (#49 review). No game code and no dump, so public CI runs it.
// It is its own executable: the loader's one-module state is per process.
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

#include "../../runtimes/gc_wii/mp4/host/src/module_loader.h"

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

namespace {

namespace fs = std::filesystem;
using mpt::mp4::load_module;
using mpt::mp4::release_module;

constexpr std::uintptr_t kBase = static_cast<std::uintptr_t>(MPT_MP4_MODULE_BASE);

fs::path utf8Path(const char* s) { return fs::path(reinterpret_cast<const char8_t*>(s)); }
const fs::path kV11 = utf8Path(MPT_STUB_V11);
const fs::path kV10 = utf8Path(MPT_STUB_V10);
const fs::path kNoExport = utf8Path(MPT_STUB_NO_EXPORT);

// Whether the image is mapped in this process right now (without loading it).
bool mapped(const fs::path& path) {
#if defined(_WIN32)
  return GetModuleHandleW(path.wstring().c_str()) != nullptr;
#else
  void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
  if (handle != nullptr) dlclose(handle);
  return handle != nullptr;
#endif
}

std::string hex(std::uintptr_t v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
  return buf;
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

}  // namespace

TEST_CASE("load_module maps a v1.1 module at its fixed base and shares it until the last hold") {
  std::string error;
  const MptMp4Api* api = load_module(kV11, kBase, error);
  REQUIRE_MESSAGE(api != nullptr, error);
  CHECK(mpt_mp4_api_has_v1_1(api));
  CHECK(std::string(api->build_id) == "stub v1.1");
  CHECK(api->status(nullptr) == MPT_MP4_STEP_RUNNING);
  // The table is the module's own data, so it sits just above the fixed base.
  const auto table = reinterpret_cast<std::uintptr_t>(api);
  CHECK(table > kBase);
  CHECK(table < kBase + 0x1000000);
  CHECK(mapped(kV11));

  // The same module again: the same table and one more hold.
  CHECK(load_module(kV11, kBase, error) == api);
  // Another module while one is loaded: refused before it is ever mapped.
  std::string other;
  CHECK(load_module(kV10, kBase, other) == nullptr);
  CHECK(has(other, "already loaded"));
  CHECK_FALSE(mapped(kV10));

  release_module();
  CHECK(mapped(kV11));  // one hold left
  release_module();
  CHECK_FALSE(mapped(kV11));
}

TEST_CASE("a module away from its fixed base is refused and unloaded") {
  std::string error;
  CHECK(load_module(kV11, kBase + 0x10000000, error) == nullptr);
  CHECK(has(error, "not at its fixed base"));
  CHECK(has(error, "loaded at " + hex(kBase)));  // where the OS really put it: the base it was linked at
  CHECK_FALSE(mapped(kV11));
  // Nothing is left held: loading it at its own base works afterwards.
  const MptMp4Api* api = load_module(kV11, kBase, error);
  REQUIRE_MESSAGE(api != nullptr, error);
  release_module();
  CHECK_FALSE(mapped(kV11));
}

TEST_CASE("a v1.0 module is refused and unloaded") {
  std::string error;
  CHECK(load_module(kV10, kBase, error) == nullptr);
  CHECK(has(error, "ABI version 1.1"));
  CHECK_FALSE(mapped(kV10));
}

TEST_CASE("a module without mpt_mp4_get_api is refused and unloaded") {
  std::string error;
  CHECK(load_module(kNoExport, kBase, error) == nullptr);
  CHECK(has(error, "does not export mpt_mp4_get_api"));
  CHECK_FALSE(mapped(kNoExport));
}

TEST_CASE("a missing module file is reported without loading anything") {
  std::string error;
  CHECK(load_module(kV11.parent_path() / "no_such_module.bin", kBase, error) == nullptr);
  CHECK(has(error, "no game module at"));
  // After every refusal above, the real module still loads.
  const MptMp4Api* api = load_module(kV11, kBase, error);
  REQUIRE_MESSAGE(api != nullptr, error);
  release_module();
}
