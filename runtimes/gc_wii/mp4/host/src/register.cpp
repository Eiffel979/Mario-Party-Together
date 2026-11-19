// The shell's registration of Mario Party 4 (mpt_register_title in CMakeLists.txt).
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "mpt/mp4/adapter.h"
#include "mpt/mp4/recipe.h"
#include "module_loader.h"
#include "mpt/mp4/runtime.h"
#include "recipe_config.h"
#include "titles.h"

namespace {

std::filesystem::path utf8_path(const char* s) { return std::filesystem::path(reinterpret_cast<const char8_t*>(s)); }

std::unique_ptr<mpt::asset_import::BuildRecipe> make_mp4_recipe() {
  mpt::mp4::RecipeConfig config;
  config.gc_wii_source_dir = utf8_path(MPT_MP4_RECIPE_GC_WII_SOURCE_DIR);
  config.core_include_dir = utf8_path(MPT_MP4_RECIPE_CORE_INCLUDE_DIR);
  config.clang_c = utf8_path(MPT_MP4_RECIPE_CLANG_C);
  config.clang_cxx = utf8_path(MPT_MP4_RECIPE_CLANG_CXX);
  config.cmake = utf8_path(MPT_MP4_RECIPE_CMAKE);
  config.ninja = utf8_path(MPT_MP4_RECIPE_NINJA);
  for (const char* arg : kMp4RecipeCmakeArgs) config.cmake_args.emplace_back(arg);
  config.runtime_build_version = MPT_MP4_RECIPE_RUNTIME_VERSION;
  // A build of MP4 in a process that has the module loaded is refused before it writes anything
  // (ADR 0008, "Game module kept loaded for the process", point 2).
  config.module_in_use = [] { return mpt::mp4::loaded_module_build_id(); };
  config.build_type = MPT_MP4_RECIPE_BUILD_TYPE;
  for (const auto& kv : kMp4RecipeEnv) {
    if (kv[0] != nullptr) config.env.emplace(kv[0], kv[1]);
  }
  std::string error;
  return mpt::mp4::make_recipe(config, error);  // null if the source tree has no lock: --dump explains
}

}  // namespace

void mpt_register_mp4(mpt::shell::ShellTitles& titles) {
  mpt::shell::ShellTitle title;
  title.id = mpt::TitleId::mp4;
  title.runtime = [] { return std::make_unique<mpt::mp4::Mp4Runtime>(); };
  title.adapter = [](mpt::ITitleRuntime& runtime) -> std::unique_ptr<mpt::ITitleAdapter> {
    auto* mp4 = dynamic_cast<mpt::mp4::Mp4Runtime*>(&runtime);
    if (mp4 == nullptr) return nullptr;
    return std::make_unique<mpt::mp4::Mp4Adapter>(*mp4);
  };
  title.recipe = &make_mp4_recipe;
  // The game module stays loaded between runtimes (module_loader.h); unload it at exit. Needs no SDL.
  // It reports a failure on stderr and never throws: shutdown_titles runs it from the shell's
  // noexcept shutdown guard.
  title.shutdown = [] {
    std::string error;
    if (!mpt::mp4::shutdown_module(error)) std::fprintf(stderr, "mp4: %s\n", error.c_str());
  };
  titles.push_back(std::move(title));
}
