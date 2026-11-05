// The MP4 recipe with a fake process runner: no compiler, no sources and no dump are needed.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "mpt/mp4/recipe.h"

namespace fs = std::filesystem;
using namespace mpt::asset_import;

namespace {

struct FakeRunner final : ProcessRunner {
  std::vector<std::vector<std::string>> calls;
  std::string version_output = "clang version 22.1.3\nTarget: x86_64-pc-windows-msvc\nThread model: posix\n";
  ProcessResult run(const ProcessRequest& request) override {
    calls.push_back(request.argv);
    ProcessResult r;
    r.started = true;
    r.exit_code = 0;
    if (request.argv.size() > 1 && request.argv[1] == "--version") r.output = version_output;
    if (request.argv.size() > 2 && request.argv[1] == "--build") {
      const fs::path dir = fs::path(request.argv[2]) / "mp4";
      fs::create_directories(dir);
      std::ofstream(dir / mpt::mp4::kModuleFileName) << "synthetic module";
    }
    return r;
  }
};

mpt::mp4::RecipeConfig config() {
  mpt::mp4::RecipeConfig c;
  c.gc_wii_source_dir = MPT_GC_WII_SOURCE_DIR;
  c.core_include_dir = "core-include";
  c.clang_c = "fake-clang";
  c.clang_cxx = "fake-clang++";
  c.cmake = "fake-cmake";
  c.ninja = "fake-ninja";
  c.cmake_args = {"-DMPT_GX_BACKEND=null"};
  return c;
}

bool has(const std::vector<std::string>& argv, const std::string& arg) {
  for (const auto& a : argv)
    if (a == arg) return true;
  return false;
}

}  // namespace

TEST_CASE("clang --version output gives the toolchain") {
  ToolchainInfo info;
  CHECK(mpt::mp4::parse_clang_version("clang version 22.1.3 (https://github.com/llvm/llvm-project)\nTarget: x86_64-unknown-linux-gnu\n", info));
  CHECK(info.compiler == "clang");
  CHECK(info.version == "22.1.3");
  CHECK(info.target == "x86_64-unknown-linux-gnu");
  CHECK_FALSE(mpt::mp4::parse_clang_version("gcc (GCC) 14.2.0\n", info));
}

TEST_CASE("the recipe pins the lock's sources and extracts the disc") {
  std::string error;
  auto recipe = mpt::mp4::make_recipe(config(), error);
  REQUIRE_MESSAGE(recipe != nullptr, error);
  CHECK(recipe->id() == "mp4");
  CHECK(recipe->extracts_disc());
  for (const char* name : {"partyboard", "musyx", "aurora"}) CHECK(recipe->sources().find(name) != nullptr);
  FakeRunner runner;
  ToolchainInfo info;
  CHECK(recipe->check_toolchain(runner, info).ok());
  CHECK(info.description() == "clang 22.1.3 (x86_64-pc-windows-msvc)");
}

TEST_CASE("the steps configure, compile and install the module") {
  std::string error;
  auto recipe = mpt::mp4::make_recipe(config(), error);
  REQUIRE(recipe != nullptr);
  const fs::path out = fs::path(MPT_RECIPE_TEST_TMP) / "build";
  fs::remove_all(out);
  fs::create_directories(out);
  FakeRunner runner;
  BuildContext ctx;
  ctx.output_dir = out;
  ctx.game_version = mpt::mp4::kGameVersion;
  ctx.runner = &runner;
  ctx.report = [](double, std::string_view) {};
  ctx.cancelled = [] { return false; };
  for (const char* name : {"partyboard", "musyx", "aurora"}) {
    ResolvedSource s;
    s.name = name;
    s.dir = fs::path("sources") / name;
    ctx.sources[name] = s;
  }
  for (auto& step : recipe->steps()) REQUIRE_MESSAGE(step->run(ctx).ok(), step->name());
  REQUIRE(runner.calls.size() == 2);
  const auto& configure = runner.calls[0];
  CHECK(has(configure, "-DMPT_BUILD_MP4_GAME_MODULE=ON"));
  CHECK(has(configure, "-DMPT_MP4_PARTYBOARD_DIR=" + (fs::path("sources") / "partyboard").string()));
  CHECK(has(configure, "-DMPT_GX_BACKEND=null"));
  CHECK(has(runner.calls[1], "mp4_game"));
  CHECK(fs::exists(out / "module" / mpt::mp4::kModuleFileName));
  CHECK_FALSE(fs::exists(out / "module-build"));
}

TEST_CASE("other revisions are refused") {
  std::string error;
  auto recipe = mpt::mp4::make_recipe(config(), error);
  REQUIRE(recipe != nullptr);
  FakeRunner runner;
  BuildContext ctx;
  ctx.output_dir = fs::path(MPT_RECIPE_TEST_TMP) / "other";
  ctx.game_version = "GMPE01_01";
  ctx.runner = &runner;
  ctx.report = [](double, std::string_view) {};
  CHECK_FALSE(recipe->steps().front()->run(ctx).ok());
  CHECK(runner.calls.empty());
}
