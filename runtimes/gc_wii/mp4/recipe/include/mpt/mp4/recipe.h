// asset-import's build recipe for MP4 (P1-10 interface, P1-15 build step, ADR 0009 section 3).
//
// Host side: built with the tree's compiler and linked into the shell. For a verified GMPE01_00 dump
// it extracts the disc (sys/ and files/, read by the game module at run time) and compiles the game
// module with Clang from the pinned sources in runtimes/gc_wii/mp4/decomp.lock:
//
//   <build>/disc/...                the extracted disc
//   <build>/module/mp4_game.dll     (mp4_game.so elsewhere) the game module, C ABI only
//
// The module is built by configuring runtimes/gc_wii as its own Clang project with
// MPT_BUILD_MP4_GAME_MODULE=ON and the resolved source checkouts. Nothing is extracted from the dump
// into the build: the game sources hold only code, and the module reads its data from the disc.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "mpt/asset_import/build.h"

namespace mpt::mp4 {

#if defined(_WIN32)
inline constexpr const char* kModuleFileName = "mp4_game.dll";
#else
inline constexpr const char* kModuleFileName = "mp4_game.so";
#endif

// Phase 1 supports one revision (plan D3).
inline constexpr const char* kGameVersion = "GMPE01_00";

struct RecipeConfig {
  // runtimes/gc_wii of this build's source tree (its CMake project, SDK, GX, MusyX and mp4 code).
  std::filesystem::path gc_wii_source_dir;
  // core/include of the same tree.
  std::filesystem::path core_include_dir;
  // Programs; empty means found on PATH (clang-cl on Windows, clang/clang++ elsewhere; cmake; ninja).
  std::filesystem::path clang_c;
  std::filesystem::path clang_cxx;
  std::filesystem::path cmake;
  std::filesystem::path ninja;
  // Extra -D arguments for the module's configure: the GX backend, the Dawn prefix, CMAKE_PREFIX_PATH
  // for the GX layer's packages. Never a path into the user's dump.
  std::vector<std::string> cmake_args;
  // Reported by runtime_build_version(); a change rebuilds every cached module.
  std::string runtime_build_version = "mp4-module-1";
};

// Fails if the source tree has no runtimes/gc_wii/mp4/decomp.lock.
std::unique_ptr<asset_import::BuildRecipe> make_recipe(const RecipeConfig& config, std::string& error);

// Parses `clang --version` output ("clang version 22.1.3 ...", "Target: x86_64-pc-windows-msvc").
bool parse_clang_version(const std::string& output, asset_import::ToolchainInfo& info);

}  // namespace mpt::mp4
