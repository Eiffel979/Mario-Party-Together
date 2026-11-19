// mpt::mp4::load_module (runtimes/gc_wii/mp4/host/src/module_loader.cpp) on stub game modules
// (stub_module.cpp): the fixed image base on this OS, one module per process with shared holds, every
// rejection unloading what it loaded (#49 review), and the module kept loaded for the process until
// shutdown_module (ADR 0008, "Game module kept loaded for the process"). No game code and no dump, so
// public CI runs it. It is its own executable: the loader's one-module state is per process. Every
// case ends with the module shut down, so the next one starts with nothing loaded.
#include <doctest/doctest.h>

#include <chrono>
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
using mpt::mp4::shutdown_module;

constexpr std::uintptr_t kBase = static_cast<std::uintptr_t>(MPT_MP4_MODULE_BASE);

fs::path utf8Path(const char* s) { return fs::path(reinterpret_cast<const char8_t*>(s)); }
const fs::path kV11 = utf8Path(MPT_STUB_V11);
const fs::path kV10 = utf8Path(MPT_STUB_V10);
const fs::path kNoExport = utf8Path(MPT_STUB_NO_EXPORT);
const fs::path kOldBase = utf8Path(MPT_STUB_OLD_BASE);
const fs::path kCross = utf8Path(MPT_STUB_CROSS);
constexpr std::uintptr_t kCrossBase = static_cast<std::uintptr_t>(MPT_STUB_CROSS_BASE);

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

// Releases `holds` holds and shuts the module down, as the shell does at exit.
void shut(int holds = 1) {
  for (int i = 0; i < holds; ++i) release_module();
  std::string error;
  CHECK_MESSAGE(shutdown_module(error), error);
}

}  // namespace

TEST_CASE("load_module maps a v1.1 module at its fixed base and shares it") {
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
  shut();
  CHECK_FALSE(mapped(kV11));
}

TEST_CASE("the last release keeps the module loaded, and a second load reuses it without a reload") {
  std::string error;
  const MptMp4Api* api = load_module(kV11, kBase, error);
  REQUIRE_MESSAGE(api != nullptr, error);
  // A mark in the module's own data: a reload (or a second mpt_mp4_get_api) would reset it.
  static const char kMark[] = "kept loaded";
  const_cast<MptMp4Api*>(api)->build_id = kMark;
  release_module();  // the last hold
  CHECK(mapped(kV11));
  CHECK(mpt::mp4::loaded_module_build_id() == kMark);
  const MptMp4Api* again = load_module(kV11, kBase, error);
  CHECK(again == api);
  CHECK(api->build_id == kMark);  // the same image, not loaded again
  const_cast<MptMp4Api*>(api)->build_id = "stub v1.1";
  shut();
  CHECK(mpt::mp4::loaded_module_build_id().empty());
}

TEST_CASE("while a module is kept loaded, another path or build ID is refused with a restart message") {
  std::string error;
  const MptMp4Api* api = load_module(kV11, kBase, error, "stub v1.1");
  REQUIRE_MESSAGE(api != nullptr, error);
  release_module();  // no hold left: still loaded
  std::string other;
  CHECK(load_module(kV10, kBase, other) == nullptr);
  CHECK(has(other, "already loaded"));
  CHECK(has(other, "restart"));
  CHECK_FALSE(mapped(kV10));
  std::string build;
  CHECK(load_module(kV11, kBase, build, "stub v2") == nullptr);
  CHECK(has(build, "is build stub v1.1, not stub v2"));
  CHECK(has(build, "restart"));
  CHECK(mapped(kV11));  // the refusals leave the kept module as it was
  CHECK(load_module(kV11, kBase, error, "stub v1.1") == api);
  shut();
}

TEST_CASE("a module whose build ID is not the expected one is refused and unloaded") {
  std::string error;
  CHECK(load_module(kV11, kBase, error, "mp4-other") == nullptr);
  CHECK(has(error, "is build stub v1.1, not mp4-other"));
  CHECK_FALSE(mapped(kV11));
}

TEST_CASE("shutdown_module unloads only when no hold is left, and a load after it works") {
  std::string error;
  // Before any load: nothing to do.
  CHECK(shutdown_module(error));
  CHECK(error.empty());
  const MptMp4Api* api = load_module(kV11, kBase, error);
  REQUIRE_MESSAGE(api != nullptr, error);
  // A hold is still taken: an error, and the module stays mapped.
  std::string held;
  CHECK_FALSE(shutdown_module(held));
  CHECK(has(held, "still held by 1 runtime"));
  CHECK(mapped(kV11));
  release_module();
  CHECK(shutdown_module(error));
  CHECK_FALSE(mapped(kV11));
  // A second shutdown does nothing, and the module loads again afterwards.
  CHECK(shutdown_module(error));
  api = load_module(kV11, kBase, error);
  REQUIRE_MESSAGE(api != nullptr, error);
  CHECK(mapped(kV11));
  shut();
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
  shut();
  CHECK_FALSE(mapped(kV11));
}

TEST_CASE("the module base keeps the whole image below the next 4 GiB boundary") {
  // ADR 0008, "Module words: a safe rule must not depend on layout", point 1.
  CHECK(kBase == 0x5B97'9E3C'0000ull);
  CHECK((kBase & 0xFFFF) == 0);  // 64 KiB aligned (Windows), so also page aligned (ELF)
  std::string error;
  const MptMp4Api* api = load_module(kV11, kBase, error);
  REQUIRE_MESSAGE(api != nullptr, error);
  shut();
}

TEST_CASE("a module linked at the old base is refused and unloaded") {
  std::string error;
  CHECK(load_module(kOldBase, kBase, error) == nullptr);
  CHECK(has(error, "not at its fixed base"));
  CHECK(has(error, "loaded at 0x500000000000"));
  CHECK_FALSE(mapped(kOldBase));
}

TEST_CASE("a module whose image crosses a 4 GiB boundary is refused and unloaded") {
  std::string error;
  // At its own base, so only the end check can refuse it.
  CHECK(load_module(kCross, kCrossBase, error) == nullptr);
  CHECK(has(error, "must not cross a 4 GiB boundary"));
  CHECK(has(error, "above 0x5b9800000000"));
  CHECK_FALSE(mapped(kCross));
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
  shut();
}

TEST_CASE("a kept module whose file changed on disk is refused with a restart message") {
  // A copy of the stub, so its time stamp can change without touching the build's file.
  const fs::path dir = fs::temp_directory_path() / ("mpt_loader_changed_" + std::to_string(kBase & 0xFFFF));
  std::error_code ec;
  fs::create_directories(dir, ec);
  const fs::path copy = dir / ("stub_changed" + kV11.extension().string());
  fs::remove(copy, ec);
  REQUIRE(fs::copy_file(kV11, copy, ec));
  std::string error;
  const MptMp4Api* api = load_module(copy, kBase, error);
  REQUIRE_MESSAGE(api != nullptr, error);
  release_module();
  // A rebuild replaced the file (Linux allows that; Windows keeps a loaded image locked, so the time
  // stamp change below may be refused there, and then there is nothing to check).
  const fs::file_time_type before = fs::last_write_time(copy, ec);
  fs::last_write_time(copy, before + std::chrono::hours(1), ec);
  if (!ec) {
    std::string changed;
    CHECK(load_module(copy, kBase, changed) == nullptr);
    CHECK(has(changed, "changed on disk"));
    CHECK(has(changed, "restart"));
  } else {
    MESSAGE("the loaded file's time stamp cannot be changed here: " << ec.message());
  }
  shut(0);
  CHECK_FALSE(mapped(copy));
  fs::remove_all(dir, ec);
}
