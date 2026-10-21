// The shell's registration of Mario Party 4 (mpt_register_title in CMakeLists.txt).
#include <filesystem>
#include <memory>
#include <string>

#include "mpt/mp4/adapter.h"
#include "mpt/mp4/recipe.h"
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
  titles.push_back(std::move(title));
}
