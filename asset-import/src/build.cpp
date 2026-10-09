#include "mpt/asset_import/build.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "mpt/asset_import/byte_source.h"
#include "mpt/asset_import/gc_disc.h"
#include "mpt/asset_import/sha256.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace mpt::asset_import {
namespace {

namespace fs = std::filesystem;

constexpr int kManifestVersion = 2;
constexpr std::size_t kKeyDirLength = 32;  // 128 bits of the key name the folder; the manifest has all of it
constexpr const char* kManifestName = "manifest.json";

// An exclusive lock on a file, released by the OS if the process dies, so a crash
// never leaves a stale lock behind.
class FileLock {
 public:
  FileLock() = default;
  FileLock(const FileLock&) = delete;
  FileLock& operator=(const FileLock&) = delete;
  ~FileLock() { release(); }

  bool try_lock(const fs::path& path) {
#ifdef _WIN32
    handle_ = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) return false;
    OVERLAPPED overlapped{};
    if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlapped)) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
      return false;
    }
    return true;
#else
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd_ < 0) return false;
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    return true;
#endif
  }

  void release() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) {
      OVERLAPPED overlapped{};
      UnlockFileEx(handle_, 0, 1, 0, &overlapped);
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (fd_ >= 0) {
      ::flock(fd_, LOCK_UN);
      ::close(fd_);
      fd_ = -1;
    }
#endif
  }

 private:
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int fd_ = -1;
#endif
};

bool is_recipe_id(std::string_view id) {
  if (id.empty() || id.size() > 32) return false;
  for (char c : id) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

bool is_sha256_hex(std::string_view text) {
  if (text.size() != 64) return false;
  for (char c : text) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

std::optional<BuildManifest> read_manifest(const fs::path& dir) {
  std::ifstream file(dir / kManifestName, std::ios::binary);
  if (!file) return std::nullopt;
  std::ostringstream text;
  text << file.rdbuf();
  std::string ignored;
  return BuildManifest::parse(text.str(), ignored);
}

bool write_file(const fs::path& path, std::string_view text) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  file.close();
  return static_cast<bool>(file);
}

// Text for the build key and manifest values: printable, no line breaks, so the
// canonical key description cannot be made ambiguous by a crafted value.
bool is_key_safe(std::string_view text) {
  for (char c : text) {
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return false;
  }
  return true;
}

// Why a ToolchainInfo is unusable as part of the key, or empty if it is fine.
std::string toolchain_problem(const ToolchainInfo& t) {
  for (const std::string* field : {&t.compiler, &t.version, &t.target}) {
    if (!is_key_safe(*field)) return "a field has control characters.";
    // The key must not depend on where the compiler is installed (ADR 0009).
    if (field->find('/') != std::string::npos || field->find('\\') != std::string::npos) {
      return "'" + *field + "' looks like a path; give the compiler ID, version and target triple only.";
    }
  }
  if (t.compiler.empty() != t.version.empty()) return "the compiler ID and version must be given together.";
  if (t.compiler.empty() && !t.target.empty()) return "a target triple needs a compiler.";
  return {};
}

}  // namespace

std::string BuildManifest::to_json() const {
  nlohmann::json sources_json = nlohmann::json::array();
  for (const ManifestSource& s : sources) {
    sources_json.push_back(
        {{"name", s.name}, {"commit", s.commit}, {"origin", s.origin}, {"pinned", s.pinned}, {"actual", s.actual}});
  }
  const nlohmann::json root = {
      {"manifest_version", kManifestVersion},
      {"key", key},
      {"recipe", recipe},
      {"recipe_version", recipe_version},
      {"runtime_build_version", runtime_build_version},
      {"dump_sha256", dump_sha256},
      {"game_version", game_version},
      {"sources", sources_json},
      {"unpinned", unpinned},
      {"toolchain", {{"compiler", toolchain.compiler}, {"version", toolchain.version}, {"target", toolchain.target}}},
      {"complete", true},
  };
  return root.dump(2) + "\n";
}

std::optional<BuildManifest> BuildManifest::parse(std::string_view json_text, std::string& error) {
  const nlohmann::json root = nlohmann::json::parse(json_text, nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    error = "The build manifest is not a JSON object.";
    return std::nullopt;
  }
  if (root.value("manifest_version", 0) != kManifestVersion || !root.value("complete", false)) {
    error = "The build manifest is from another version or incomplete.";
    return std::nullopt;
  }
  try {
    BuildManifest m;
    m.key = root.at("key").get<std::string>();
    m.recipe = root.at("recipe").get<std::string>();
    m.recipe_version = root.at("recipe_version").get<std::uint32_t>();
    m.runtime_build_version = root.at("runtime_build_version").get<std::string>();
    m.dump_sha256 = root.at("dump_sha256").get<std::string>();
    m.game_version = root.at("game_version").get<std::string>();
    m.unpinned = root.at("unpinned").get<bool>();
    const nlohmann::json& toolchain = root.at("toolchain");
    m.toolchain = {toolchain.at("compiler").get<std::string>(), toolchain.at("version").get<std::string>(),
                   toolchain.at("target").get<std::string>()};
    for (const nlohmann::json& s : root.at("sources")) {
      m.sources.push_back({s.at("name").get<std::string>(), s.at("commit").get<std::string>(),
                           s.at("origin").get<std::string>(), s.at("pinned").get<bool>(),
                           s.at("actual").get<std::string>()});
    }
    return m;
  } catch (const nlohmann::json::exception& e) {
    error = std::string("The build manifest is malformed: ") + e.what();
    return std::nullopt;
  }
}

std::string ToolchainInfo::description() const {
  if (compiler.empty()) return "none";
  std::string text = compiler + " " + version;
  if (!target.empty()) text += " (" + target + ")";
  return text;
}

std::string build_key(const BuildRecipe& recipe, std::string_view dump_sha256,
                      const std::vector<ManifestSource>& sources, const ToolchainInfo& toolchain) {
  std::string text = "mpt-build-key 2\n";
  text += "recipe " + recipe.id() + "\n";
  text += "recipe_version " + std::to_string(recipe.version()) + "\n";
  text += "runtime_build_version " + recipe.runtime_build_version() + "\n";
  text += "dump_sha256 " + std::string(dump_sha256) + "\n";
  text += "toolchain_compiler " + toolchain.compiler + "\n";
  text += "toolchain_version " + toolchain.version + "\n";
  text += "toolchain_target " + toolchain.target + "\n";
  for (const ManifestSource& s : sources) {
    text += "source " + s.name + " " + s.commit + " " + (s.pinned ? "pinned" : "unpinned " + s.actual) + "\n";
  }
  Sha256 sha;
  sha.update(std::as_bytes(std::span(text.data(), text.size())));
  return sha.finish_hex();
}

std::optional<BuildOutcome> BuildCache::build(const BuildRecipe& recipe, const BuildRequest& request,
                                              std::string& error) {
  std::error_code ec;
  auto fail = [&error](std::string message) {
    error = std::move(message);
    return std::nullopt;
  };
  auto cancelled = [&request] { return request.cancelled && request.cancelled(); };

  // --- Inputs --------------------------------------------------------------
  const std::string id = recipe.id();
  if (!is_recipe_id(id)) return fail("The recipe ID '" + id + "' is not valid.");
  if (!is_key_safe(recipe.runtime_build_version()) || recipe.runtime_build_version().empty()) {
    return fail("The recipe for " + id + " has an invalid runtime build version.");
  }
  if (!is_sha256_hex(request.dump.sha256)) return fail("The dump's SHA-256 is not valid.");
  if (!is_key_safe(request.dump.game_version)) return fail("The game version is not valid.");
  if (recipe.extracts_disc()) {
    const std::uintmax_t size = fs::file_size(request.dump.path, ec);
    if (ec) return fail("Could not read the dump " + path_for_message(request.dump.path) + ".");
    if (size != request.dump.size) {
      return fail("The dump file has changed since it was verified. Import it again.");
    }
  }
  for (const auto& [name, input] : request.source_inputs) {
    if (!recipe.sources().find(name)) return fail("There is no source named '" + name + "' in the " + id + " lock.");
  }

  const fs::path recipe_dir = root_ / "builds" / id;
  fs::create_directories(recipe_dir, ec);
  if (ec) return fail("Could not create the cache folder " + path_for_message(recipe_dir) + ": " + ec.message());
  FileLock lock;
  if (!lock.try_lock(recipe_dir / ".lock")) {
    return fail("Another build of " + id + " is already running. Wait for it to finish and try again.");
  }

  const std::size_t extract_stages = recipe.extracts_disc() ? 1 : 0;
  std::size_t stage_count = 0;  // known once the steps are listed
  std::size_t stage_index = 0;
  auto progress = [&](std::string stage, double fraction, std::string detail) {
    if (request.progress) request.progress({std::move(stage), stage_index, stage_count, fraction, std::move(detail)});
  };

  // --- Sources -------------------------------------------------------------
  const std::vector<std::unique_ptr<BuildStep>> steps = recipe.steps();
  stage_count = 1 + extract_stages + steps.size() + 1;
  SourceResolver resolver(root_ / "sources", runner_);
  std::map<std::string, ResolvedSource> resolved;
  std::vector<ManifestSource> manifest_sources;
  bool unpinned = false;
  const auto& pins = recipe.sources().pins();
  for (std::size_t i = 0; i < pins.size(); ++i) {
    const SourcePin& pin = pins[i];
    progress("Checking sources", static_cast<double>(i) / static_cast<double>(pins.size()), pin.name);
    if (cancelled()) return fail("The build was cancelled.");
    const auto input = request.source_inputs.find(pin.name);
    const SourceInput source_input = input != request.source_inputs.end() ? input->second : SourceInput{};
    auto source = resolver.resolve(pin, source_input, error);
    if (!source) return std::nullopt;
    unpinned = unpinned || !source->pinned;
    manifest_sources.push_back(
        {pin.name, pin.commit, std::string(to_string(source->origin)), source->pinned, source->actual});
    resolved.emplace(pin.name, std::move(*source));
  }
  progress("Checking sources", 1.0, {});

  // --- Toolchain (part of the key, so checked even when the cache has the build) --
  ToolchainInfo toolchain;
  if (Result r = recipe.check_toolchain(runner_, toolchain); !r) return fail(r.error());
  if (const std::string problem = toolchain_problem(toolchain); !problem.empty()) {
    return fail("The recipe for " + id + " identified its toolchain badly: " + problem);
  }

  // --- Cache lookup --------------------------------------------------------
  const std::string key = build_key(recipe, request.dump.sha256, manifest_sources, toolchain);
  const fs::path final_dir = recipe_dir / key.substr(0, kKeyDirLength);
  if (!request.force_rebuild) {
    if (const auto existing = read_manifest(final_dir); existing && existing->key == key) {
      return BuildOutcome{final_dir, *existing, true};
    }
  }

  // Leftovers of interrupted builds. We hold the recipe lock, so none is in use.
  for (const auto& entry : fs::directory_iterator(recipe_dir, ec)) {
    const std::u8string name = entry.path().filename().u8string();
    if (name.starts_with(u8".tmp-") || name.starts_with(u8".old-")) fs::remove_all(entry.path(), ec);
  }
  ec.clear();
  const fs::path tmp_dir = recipe_dir / (".tmp-" + key.substr(0, kKeyDirLength));
  fs::create_directories(tmp_dir, ec);
  if (ec) return fail("Could not create " + path_for_message(tmp_dir) + ": " + ec.message());
  auto abandon = [&](std::string message) {
    std::error_code ignored;
    fs::remove_all(tmp_dir, ignored);
    error = std::move(message);
    return std::nullopt;
  };

  BuildContext context;
  context.output_dir = tmp_dir;
  context.sources = resolved;
  context.dump_sha256 = request.dump.sha256;
  context.game_version = request.dump.game_version;
  context.unpinned = unpinned;
  context.runner = &runner_;
  context.cancelled = cancelled;

  // --- Disc ----------------------------------------------------------------
  if (recipe.extracts_disc()) {
    ++stage_index;
    progress("Extracting disc", 0.0, {});
    const auto source = FileSource::open(request.dump.path, error);
    if (!source) return abandon(error);
    const auto disc = GcDisc::open(*source, error);
    if (!disc) return abandon("The disc cannot be extracted safely: " + error);
    context.disc_dir = tmp_dir / "disc";
    if (Result r = extract_gc_disc(*source, *disc, context.disc_dir); !r) return abandon(r.error());
    progress("Extracting disc", 1.0, {});
  }

  // --- Steps ---------------------------------------------------------------
  for (const std::unique_ptr<BuildStep>& step : steps) {
    ++stage_index;
    const std::string name = step->name();
    if (cancelled()) return abandon("The build was cancelled.");
    context.report = [&](double fraction, std::string_view detail) { progress(name, fraction, std::string(detail)); };
    progress(name, 0.0, {});
    Result r = Result::success();
    try {
      r = step->run(context);
    } catch (const std::exception& e) {
      r = Result::failure(std::string("unexpected error: ") + e.what());
    }
    if (!r) return abandon(name + " failed: " + r.error());
    if (cancelled()) return abandon("The build was cancelled.");
    progress(name, 1.0, {});
  }

  // --- Finish: manifest last, then one rename ------------------------------
  ++stage_index;
  progress("Finishing", 0.0, {});
  BuildManifest manifest{key,
                         id,
                         recipe.version(),
                         recipe.runtime_build_version(),
                         request.dump.sha256,
                         request.dump.game_version,
                         manifest_sources,
                         unpinned,
                         toolchain};
  if (!write_file(tmp_dir / kManifestName, manifest.to_json())) {
    return abandon("Could not write the build manifest.");
  }
  if (fs::exists(final_dir, ec)) {
    // An older, incomplete or forced-out build with the same key: move it aside first.
    const fs::path old = recipe_dir / (".old-" + key.substr(0, kKeyDirLength));
    fs::remove_all(old, ec);
    fs::rename(final_dir, old, ec);
    if (ec) return abandon("Could not replace the previous build: " + ec.message());
    fs::remove_all(old, ec);
  }
  fs::rename(tmp_dir, final_dir, ec);
  if (ec) return abandon("Could not move the finished build into place: " + ec.message());
  progress("Finishing", 1.0, {});
  return BuildOutcome{final_dir, std::move(manifest), false};
}

}  // namespace mpt::asset_import
