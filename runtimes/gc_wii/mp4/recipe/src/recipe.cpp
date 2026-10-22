// See mpt/mp4/recipe.h.
#include "mpt/mp4/recipe.h"

#include <regex>
#include <system_error>
#include <utility>

#include "mpt/asset_import/source_lock.h"

namespace mpt::mp4 {
namespace fs = std::filesystem;
using asset_import::BuildContext;
using asset_import::BuildStep;
using asset_import::ProcessRequest;
using asset_import::ProcessResult;
using asset_import::ProcessRunner;
using asset_import::SourceLock;
using asset_import::ToolchainInfo;

namespace {

constexpr const char* kSourceNames[] = {"partyboard", "musyx", "aurora"};

std::string program(const fs::path& configured, const char* name) {
  if (!configured.empty()) return configured.string();
  if (auto found = asset_import::find_program(name)) return found->string();
  return {};
}

#if defined(_WIN32)
constexpr const char* kClangC = "clang-cl";
constexpr const char* kClangCxx = "clang-cl";
#else
constexpr const char* kClangC = "clang";
constexpr const char* kClangCxx = "clang++";
#endif

struct Tools {
  std::string c, cxx, cmake, ninja;
};

Result findTools(const RecipeConfig& config, Tools& tools) {
  tools.c = program(config.clang_c, kClangC);
  tools.cxx = program(config.clang_cxx, kClangCxx);
  tools.cmake = program(config.cmake, "cmake");
  tools.ninja = program(config.ninja, "ninja");
  if (tools.c.empty() || tools.cxx.empty())
    return Result::failure(std::string(kClangC) + " was not found. Install LLVM (Clang) and try again.");
  if (tools.cmake.empty()) return Result::failure("cmake was not found. Install CMake 3.25 or later and try again.");
  if (tools.ninja.empty()) return Result::failure("ninja was not found. Install Ninja and try again.");
  return Result::success();
}

std::string tail(const std::string& output) {
  constexpr std::size_t kMax = 2000;
  return output.size() <= kMax ? output : "..." + output.substr(output.size() - kMax);
}

Result runTool(BuildContext& context, ProcessRequest request, const char* what) {
  if (context.runner == nullptr) return Result::failure(std::string(what) + ": no process runner");
  const ProcessResult r = context.runner->run(request);
  if (!r.started) return Result::failure(std::string(what) + ": could not run " + request.argv.front());
  if (r.exit_code != 0) return Result::failure(std::string(what) + " failed:\n" + tail(r.output));
  return Result::success();
}

fs::path moduleBuildDir(const BuildContext& context) { return context.output_dir / "module-build"; }

class ConfigureStep final : public BuildStep {
 public:
  explicit ConfigureStep(RecipeConfig config) : config_(std::move(config)) {}
  std::string name() const override { return "Configuring the game module"; }
  Result run(BuildContext& context) override {
    if (context.game_version != kGameVersion)
      return Result::failure("this build supports " + std::string(kGameVersion) + " only, not " + context.game_version);
    Tools tools;
    if (Result r = findTools(config_, tools); !r) return r;
    std::vector<std::string> argv = {
        tools.cmake, "-S", config_.gc_wii_source_dir.string(), "-B", moduleBuildDir(context).string(), "-G", "Ninja",
        "-DCMAKE_MAKE_PROGRAM=" + tools.ninja, "-DCMAKE_C_COMPILER=" + tools.c, "-DCMAKE_CXX_COMPILER=" + tools.cxx,
        "-DCMAKE_BUILD_TYPE=Release", "-DMPT_CORE_INCLUDE_DIR=" + config_.core_include_dir.string(),
        "-DMPT_BUILD_TESTS=OFF", "-DMPT_REQUIRE_GC_WII=ON", "-DMPT_BUILD_MP4_GAME_MODULE=ON"};
    const std::pair<const char*, const char*> dirs[] = {
        {"partyboard", "-DMPT_MP4_PARTYBOARD_DIR="}, {"musyx", "-DMPT_MUSYX_DIR="}, {"aurora", "-DMPT_AURORA_DIR="}};
    for (const auto& [source, arg] : dirs) {
      const auto it = context.sources.find(source);
      if (it == context.sources.end()) return Result::failure(std::string("source ") + source + " was not resolved");
      argv.push_back(arg + it->second.dir.string());
    }
    for (const std::string& a : config_.cmake_args) argv.push_back(a);
    context.report(0.0, "cmake");
    return runTool(context, {argv, {}, {}}, "Configuring the game module");
  }

 private:
  RecipeConfig config_;
};

class CompileStep final : public BuildStep {
 public:
  explicit CompileStep(RecipeConfig config) : config_(std::move(config)) {}
  std::string name() const override { return "Compiling the game module"; }
  Result run(BuildContext& context) override {
    Tools tools;
    if (Result r = findTools(config_, tools); !r) return r;
    context.report(0.0, "mp4_game");
    return runTool(context, {{tools.cmake, "--build", moduleBuildDir(context).string(), "--target", "mp4_game"}, {}, {}},
                   "Compiling the game module");
  }

 private:
  RecipeConfig config_;
};

class InstallStep final : public BuildStep {
 public:
  std::string name() const override { return "Installing the game module"; }
  Result run(BuildContext& context) override {
    const fs::path built = moduleBuildDir(context) / "mp4" / kModuleFileName;
    const fs::path dest_dir = context.output_dir / "module";
    std::error_code ec;
    fs::create_directories(dest_dir, ec);
    if (ec) return Result::failure("cannot create " + dest_dir.string() + ": " + ec.message());
    fs::copy_file(built, dest_dir / kModuleFileName, fs::copy_options::overwrite_existing, ec);
    if (ec) return Result::failure("the build produced no " + std::string(kModuleFileName) + ": " + ec.message());
    // The intermediates hold nothing the runtime needs; keep the cache small.
    fs::remove_all(moduleBuildDir(context), ec);
    return Result::success();
  }
};

class Mp4Recipe final : public asset_import::BuildRecipe {
 public:
  Mp4Recipe(RecipeConfig config, SourceLock lock) : config_(std::move(config)), lock_(std::move(lock)) {}
  std::string id() const override { return "mp4"; }
  std::uint32_t version() const override { return 1; }
  std::string runtime_build_version() const override { return config_.runtime_build_version; }
  const SourceLock& sources() const override { return lock_; }
  bool extracts_disc() const override { return true; }
  Result check_toolchain(ProcessRunner& runner, ToolchainInfo& info) const override {
    Tools tools;
    if (Result r = findTools(config_, tools); !r) return r;
    const ProcessResult r = runner.run({{tools.c, "--version"}, {}, {}});
    if (!r.ok()) return Result::failure(std::string(kClangC) + " --version failed. Reinstall LLVM and try again.");
    if (!parse_clang_version(r.output, info))
      return Result::failure(std::string(kClangC) + " did not report a Clang version and target.");
    return Result::success();
  }
  std::vector<std::unique_ptr<BuildStep>> steps() const override {
    std::vector<std::unique_ptr<BuildStep>> out;
    out.push_back(std::make_unique<ConfigureStep>(config_));
    out.push_back(std::make_unique<CompileStep>(config_));
    out.push_back(std::make_unique<InstallStep>());
    return out;
  }

 private:
  RecipeConfig config_;
  SourceLock lock_;
};

}  // namespace

bool parse_clang_version(const std::string& output, ToolchainInfo& info) {
  std::smatch m;
  static const std::regex version(R"(clang version ([0-9]+\.[0-9]+\.[0-9]+))");
  static const std::regex target(R"(Target: ([A-Za-z0-9_.-]+))");
  if (!std::regex_search(output, m, version)) return false;
  info.compiler = "clang";
  info.version = m[1].str();
  if (!std::regex_search(output, m, target)) return false;
  info.target = m[1].str();
  return true;
}

std::unique_ptr<asset_import::BuildRecipe> make_recipe(const RecipeConfig& config, std::string& error) {
  const fs::path lock_path = config.gc_wii_source_dir / "mp4" / "decomp.lock";
  std::optional<SourceLock> lock = SourceLock::load(lock_path, error);
  if (!lock) {
    error = lock_path.string() + ": " + error;
    return nullptr;
  }
  for (const char* name : kSourceNames) {
    if (lock->find(name) == nullptr) {
      error = lock_path.string() + ": no pin for " + name;
      return nullptr;
    }
  }
  return std::make_unique<Mp4Recipe>(config, std::move(*lock));
}

}  // namespace mpt::mp4
