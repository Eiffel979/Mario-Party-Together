#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iterator>
#include <stdexcept>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "asset_import_test_support.h"
#include "mpt/asset_import/build.h"
#include "mpt/asset_import/fs_retry.h"
#include "mpt/asset_import/gclient_deps.h"
#include "mpt/asset_import/git_tree.h"
#include "mpt/asset_import/process.h"
#include "mpt/asset_import/sha1.h"
#include "mpt/core/sha256.h"
#include "mpt/asset_import/source_lock.h"
#include "mpt/asset_import/sources.h"

using namespace mpt;
using namespace mpt::asset_import;
using namespace mpt_test;

// Build orchestration tests (P1-10). Everything is synthetic and local: discs are
// generated in memory, "game sources" are a few made-up text files, and git is
// replaced by a fake runner. The only tests that run the real git do so against a
// repository they create in a temp folder (no network), and skip if git is absent.

namespace {

std::string sha1_of(std::string_view text) {
  Sha1 sha;
  sha.update(text);
  return sha.finish_hex();
}

void write_text(const fs::path& path, std::string_view text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary).write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string read_text(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

// A small synthetic "game source" tree.
const std::map<std::string, std::string> kSourceFiles = {
    {"README.md", "synthetic source for tests\n"},
    {"src/game.c", "int synthetic_main(void) { return 0; }\n"},
    {"src/sub/helper.h", "#define SYNTHETIC 1\n"},
    {"tools/run.sh", "#!/bin/sh\necho synthetic\n"},
};
const std::map<std::string, std::string> kSourceModes = {{"tools/run.sh", "100755"}};
const std::string kCommitA = std::string(40, 'a');
const std::string kCommitB = std::string(40, 'b');
const std::string kSubCommit = std::string(40, 'c');

void write_tree(const fs::path& dir, const std::map<std::string, std::string>& files) {
  for (const auto& [path, text] : files) write_text(dir / fs::path(path), text);
}

std::string tree_of(const fs::path& dir, const TreeHashOptions& options = {}) {
  std::string error;
  const auto id = git_tree_id(dir, options, error);
  REQUIRE_MESSAGE(id.has_value(), error);
  return *id;
}

std::string lock_json(const std::string& commit, const std::string& tree, bool with_submodule = false) {
  std::string json = R"({"lock_version":1,"sources":[{"name":"gamesrc","repo":"https://example.invalid/gamesrc",)"
                     R"("commit":")" + commit + R"(","tree":")" + tree + R"(","license":"CC0-1.0",)"
                     R"("modes":{"tools/run.sh":"100755"})";
  if (with_submodule) json += R"(,"submodules":[{"path":"extern/lib","source":"libsrc"}])";
  json += "}";
  if (with_submodule) {
    json += R"(,{"name":"libsrc","repo":"https://example.invalid/libsrc","commit":")" + kSubCommit +
            R"(","license":"MIT"})";
  }
  return json + "]}";
}

SourceLock parse_lock(const std::string& json, const LockParseOptions& options = {}) {
  std::string error;
  auto lock = SourceLock::parse(json, error, options);
  REQUIRE_MESSAGE(lock.has_value(), error);
  return *lock;
}

// --- A fake git -------------------------------------------------------------

struct FakeRepo {
  std::string commit;
  std::map<std::string, std::string> files;
  std::map<std::string, std::string> gitlinks;  // path → commit recorded in the tree
};

// Keeps each fake checkout's state in files under its .git folder, so it survives
// the resolver renaming the checkout into place.
void set_fake_checkout(const fs::path& dir, const std::string& head, const std::string& tree, bool dirty,
                       const std::string& url = {}) {
  write_text(dir / ".git" / "fake_head", head);
  write_text(dir / ".git" / "fake_tree", tree);
  write_text(dir / ".git" / "fake_url", url);
  if (dirty) {
    write_text(dir / ".git" / "fake_dirty", "1");
  } else {
    fs::remove(dir / ".git" / "fake_dirty");
  }
}

class FakeGit final : public ProcessRunner {
 public:
  bool installed = true;
  std::map<std::string, FakeRepo> repos;  // by URL
  std::vector<std::vector<std::string>> calls;

  ProcessResult run(const ProcessRequest& request) override {
    calls.push_back(request.argv);
    ProcessResult r;
    if (!installed) return r;
    r.started = true;
    r.exit_code = 0;
    const std::vector<std::string>& a = request.argv;
    auto has = [&a](std::string_view arg) { return std::find(a.begin(), a.end(), arg) != a.end(); };
    if (has("clone")) {
      const std::string url = a[a.size() - 2];
      const fs::path dir = fs::path(std::u8string(reinterpret_cast<const char8_t*>(a.back().data()), a.back().size()));
      if (!repos.contains(url)) return fail(r, "fatal: repository not found");
      set_fake_checkout(dir, {}, {}, false, url);
      return r;
    }
    const fs::path git_dir = request.cwd / ".git";
    if (!fs::exists(git_dir / "fake_head")) return fail(r, "fatal: not a git repository");
    const auto repo = repos.find(read_text(git_dir / "fake_url"));
    if (has("checkout")) {
      const std::string& sha = a.back();
      if (repo == repos.end() || sha != repo->second.commit) return fail(r, "fatal: reference is not a tree");
      write_tree(request.cwd, repo->second.files);
      TreeHashOptions options;
      options.modes = kSourceModes;
      options.gitlinks = repo->second.gitlinks;
      set_fake_checkout(request.cwd, sha, tree_of(request.cwd, options), false, repo->first);
      return r;
    }
    if (has("rev-parse")) {
      r.output = read_text(git_dir / (a.back() == "HEAD^{tree}" ? "fake_tree" : "fake_head")) + "\n";
      return r;
    }
    if (has("ls-tree")) {
      const std::string& path = a.back();
      if (repo != repos.end()) {
        if (const auto it = repo->second.gitlinks.find(path); it != repo->second.gitlinks.end()) {
          r.output = "160000 commit " + it->second + "\t" + path + "\n";
        }
      }
      return r;
    }
    if (has("status")) {
      r.output = fs::exists(git_dir / "fake_dirty") ? " M src/game.c\n" : "";
      return r;
    }
    return fail(r, "fake git: unsupported command");
  }

  int count(std::string_view verb) const {
    int n = 0;
    for (const auto& call : calls) n += std::find(call.begin(), call.end(), verb) != call.end() ? 1 : 0;
    return n;
  }

 private:
  static ProcessResult fail(ProcessResult& r, std::string message) {
    r.exit_code = 128;
    r.output = std::move(message);
    return r;
  }
};

// --- A fake recipe ----------------------------------------------------------

struct RecipeLog {
  int toolchain_checks = 0;
  int step_runs = 0;
  std::vector<BuildContext> contexts;  // copies of what the first step saw
};

class FakeStep final : public BuildStep {
 public:
  FakeStep(int index, RecipeLog& log, std::function<Result(BuildContext&)> hook)
      : index_(index), log_(log), hook_(std::move(hook)) {}
  std::string name() const override { return "Fake step " + std::to_string(index_); }
  Result run(BuildContext& context) override {
    ++log_.step_runs;
    if (index_ == 1) log_.contexts.push_back(context);
    context.report(0.5, "halfway");
    write_text(context.output_dir / ("step" + std::to_string(index_) + ".txt"), "output of step " + std::to_string(index_));
    return hook_ ? hook_(context) : Result::success();
  }

 private:
  int index_;
  RecipeLog& log_;
  std::function<Result(BuildContext&)> hook_;
};

class FakeRecipe final : public BuildRecipe {
 public:
  explicit FakeRecipe(RecipeLog& log) : log_(log) {}
  std::string id() const override { return "fake"; }
  std::uint32_t version() const override { return recipe_version; }
  std::string runtime_build_version() const override { return runtime_version; }
  const SourceLock& sources() const override { return lock; }
  bool extracts_disc() const override { return disc; }
  Result check_toolchain(ProcessRunner&, ToolchainInfo& info) const override {
    ++log_.toolchain_checks;
    if (!toolchain_present) return Result::failure("fakecc was not found. Install it and try again.");
    info = toolchain;
    return Result::success();
  }
  std::vector<std::unique_ptr<BuildStep>> steps() const override {
    std::vector<std::unique_ptr<BuildStep>> out;
    out.push_back(std::make_unique<FakeStep>(1, log_, first_hook));
    out.push_back(std::make_unique<FakeStep>(2, log_, second_hook));
    return out;
  }

  std::uint32_t recipe_version = 1;
  std::string runtime_version = "runtime-1";
  SourceLock lock;
  bool disc = true;
  bool toolchain_present = true;
  ToolchainInfo toolchain{"fakecc", "1.0.0", "x86_64-synthetic-none"};
  std::function<Result(BuildContext&)> first_hook;
  std::function<Result(BuildContext&)> second_hook;

 private:
  RecipeLog& log_;
};

std::string sha256_of(const std::vector<std::byte>& data) {
  Sha256 sha;
  sha.update(data);
  return to_hex(sha.finish());
}

// A cache, a synthetic disc on disk and a synthetic source archive, all in one temp folder.
struct BuildFixture {
  TempDir tmp;
  FakeGit git;
  RecipeLog log;
  FakeRecipe recipe{log};
  BuildCache cache{tmp.path() / "cache", git};
  BuildRequest request;
  fs::path archive = tmp.path() / "archive";

  BuildFixture() {
    const BuiltDisc disc = build_disc(default_spec());
    request.dump = {sha256_of(disc.image), disc.image.size(), tmp.write("synthetic.iso", disc.image), "GMPE01_00"};
    write_tree(archive, kSourceFiles);
    TreeHashOptions options;
    options.modes = kSourceModes;
    recipe.lock = parse_lock(lock_json(kCommitA, tree_of(archive, options)));
    request.source_inputs["gamesrc"] = {SourceOrigin::archive, archive};
  }

  std::optional<BuildOutcome> build(std::string& error) { return cache.build(recipe, request, error); }
  BuildOutcome build_ok() {
    std::string error;
    auto outcome = build(error);
    REQUIRE_MESSAGE(outcome.has_value(), error);
    return *outcome;
  }
  std::vector<std::string> recipe_dir_entries() const {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(tmp.path() / "cache" / "builds" / "fake", ec)) {
      names.push_back(e.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
  }
};

}  // namespace

// --- Hashes -----------------------------------------------------------------

TEST_CASE("SHA-1 matches FIPS 180-4 test vectors") {
  CHECK(sha1_of("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  CHECK(sha1_of("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
  CHECK(sha1_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
  const std::string million_a(1'000'000, 'a');
  CHECK(sha1_of(million_a) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
  Sha1 pieces;
  for (std::size_t pos = 0, step = 1; pos < million_a.size(); pos += step, step = step * 5 % 127 + 1) {
    pieces.update(std::string_view(million_a).substr(pos, step));
  }
  CHECK(pieces.finish_hex() == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST_CASE("git object IDs match git's own") {
  CHECK(git_blob_id({}) == "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391");
  const std::string hello = "hello\n";
  CHECK(git_blob_id(std::as_bytes(std::span(hello.data(), hello.size()))) == "ce013625030ba8dba906f756967f9e9ca394464a");

  const TempDir tmp;
  fs::create_directories(tmp.path() / "empty" / "nested_empty");
  CHECK(tree_of(tmp.path() / "empty") == "4b825dc642cb6eb9a060e54bf8d69288fbee4904");  // git's empty tree
}

TEST_CASE("tree hashes follow modes, gitlinks, empty folders and .git") {
  const TempDir tmp;
  const fs::path dir = tmp.path() / "src";
  write_tree(dir, kSourceFiles);
  const std::string plain = tree_of(dir);

  TreeHashOptions exec;
  exec.modes = kSourceModes;
  CHECK(tree_of(dir, exec) != plain);

  fs::create_directories(dir / "empty_folder");
  write_text(dir / ".git" / "HEAD", "ref: refs/heads/main\n");
  CHECK(tree_of(dir) == plain);

  TreeHashOptions link;
  link.gitlinks["extern/lib"] = kSubCommit;
  const std::string with_link = tree_of(dir, link);
  CHECK(with_link != plain);
  write_text(dir / "extern" / "lib" / "ignored.c", "contents under a submodule path are ignored\n");
  CHECK(tree_of(dir, link) == with_link);

  write_text(dir / "src" / "game.c", "changed\n");
  CHECK(tree_of(dir) != plain);
}

TEST_CASE("tree hashes agree with a local git repository" * doctest::description("skips if git is not installed")) {
  if (!find_program("git")) {
    MESSAGE("git not found; skipping the cross-check against git");
    return;
  }
  const TempDir tmp;
  const fs::path dir = tmp.path() / "repo";
  write_tree(dir, kSourceFiles);
  write_text(dir / "src" / "a b.c", "a name with a space\n");
  write_text(dir / "src-x.c", "sorts between 'src' and 'src/' in git's order\n");
  fs::create_directories(dir / "empty_folder");

  SystemProcessRunner runner;
  auto git = [&](std::vector<std::string> args) {
    ProcessRequest request;
    request.argv = {"git", "-c", "core.autocrlf=false", "-c", "core.safecrlf=false"};
    request.argv.insert(request.argv.end(), args.begin(), args.end());
    request.cwd = dir;
    const ProcessResult r = runner.run(request);
    REQUIRE_MESSAGE(r.ok(), r.output);
    return r.output.substr(0, r.output.find_last_not_of("\r\n") + 1);
  };
  git({"init", "-q"});
  git({"add", "-A"});
  git({"update-index", "--chmod=+x", "tools/run.sh"});
  git({"update-index", "--add", "--cacheinfo", "160000," + kSubCommit + ",extern/lib"});
  const std::string expected = git({"write-tree"});

  TreeHashOptions options;
  options.modes = kSourceModes;
  options.gitlinks["extern/lib"] = kSubCommit;
  CHECK(tree_of(dir, options) == expected);
}

// --- Lock files -------------------------------------------------------------

TEST_CASE("source locks are parsed and validated") {
  const std::string tree = std::string(40, 'e');
  const SourceLock lock = parse_lock(lock_json(kCommitA, tree, /*with_submodule=*/true));
  REQUIRE(lock.pins().size() == 2);
  const SourcePin* game = lock.find("gamesrc");
  REQUIRE(game != nullptr);
  CHECK(game->commit == kCommitA);
  CHECK(game->tree == tree);
  CHECK(game->modes.at("tools/run.sh") == "100755");
  REQUIRE(game->submodules.size() == 1);
  CHECK(game->submodules[0].gitlink == kSubCommit);  // defaults to the submodule source's pin

  auto rejected = [](const std::string& json, std::string_view needle, const LockParseOptions& options = {}) {
    std::string error;
    CHECK_FALSE(SourceLock::parse(json, error, options).has_value());
    CHECK_MESSAGE(error.find(needle) != std::string::npos, "error was: " << error);
  };
  auto one = [](const std::string& fields) {
    return R"({"lock_version":1,"sources":[{"name":"x","license":"MIT",)" + fields + "}]}";
  };
  const std::string repo = R"("repo":"https://example.invalid/x")";
  rejected(one(repo + R"(,"commit":"main")"), "commit");
  rejected(one(repo + R"(,"commit":"v1.0.0")"), "never a tag");
  rejected(one(repo + R"(,"commit":")" + std::string(40, 'A') + "\""), "lowercase");
  rejected(one(R"("repo":"http://example.invalid/x","commit":")" + kCommitA + "\""), "https");
  rejected(one(R"("repo":"file:///tmp/x","commit":")" + kCommitA + "\""), "https");
  rejected(one(repo + R"(,"commit":")" + kCommitA + R"(","modes":{"../evil":"100755"})"), "invalid path");
  rejected(one(repo + R"(,"commit":")" + kCommitA + R"(","modes":{"a":"100666"})"), "100755");
  rejected(one(repo + R"(,"commit":")" + kCommitA + R"(","submodules":[{"path":"e/l","source":"nope"}])"),
           "unknown source");
  rejected(R"({"lock_version":0,"sources":[]})", "lock_version");
  rejected(R"({"lock_version":1,"sources":[{"name":"x","repo":"https://e.invalid/x","commit":")" + kCommitA +
               R"(","license":"MIT"},{"name":"x","repo":"https://e.invalid/y","commit":")" + kCommitB +
               R"(","license":"MIT"}]})",
           "duplicates");
  const std::string differing = R"({"lock_version":1,"sources":[{"name":"p","repo":"https://e.invalid/p","commit":")" +
                                kCommitA + R"(","license":"MIT","submodules":[{"path":"l","source":"s","gitlink":")" +
                                kCommitB + R"("}]},{"name":"s","repo":"https://e.invalid/s","commit":")" + kSubCommit +
                                R"(","license":"MIT"}]})";
  rejected(differing, "give a reason");

  std::string error;
  LockParseOptions local;
  local.allow_file_urls = true;
  CHECK(SourceLock::parse(one(R"("repo":"file:///tmp/x","commit":")" + kCommitA + "\""), error, local).has_value());
}

// --- Sources ----------------------------------------------------------------

TEST_CASE("sources are fetched by commit, verified and reused") {
  const TempDir tmp;
  FakeGit git;
  const fs::path reference = tmp.path() / "reference";
  write_tree(reference, kSourceFiles);
  TreeHashOptions options;
  options.modes = kSourceModes;
  const std::string tree = tree_of(reference, options);
  git.repos["https://example.invalid/gamesrc"] = {kCommitA, kSourceFiles, {}};
  const SourceLock lock = parse_lock(lock_json(kCommitA, tree));
  SourceResolver resolver(tmp.path() / "sources", git);

  std::string error;
  const auto first = resolver.resolve(lock, "gamesrc", {}, error);
  REQUIRE_MESSAGE(first.has_value(), error);
  CHECK(first->pinned);
  CHECK(first->origin == SourceOrigin::fetched);
  CHECK(first->dir == tmp.path() / "sources" / "gamesrc" / kCommitA);
  CHECK(read_text(first->dir / "src" / "game.c") == kSourceFiles.at("src/game.c"));
  CHECK(git.count("clone") == 1);
  const auto& clone = git.calls.front();
  CHECK(std::find(clone.begin(), clone.end(), "--filter=blob:none") != clone.end());
  CHECK_FALSE(fs::exists(tmp.path() / "sources" / "gamesrc" / (".tmp-" + kCommitA)));

  const auto second = resolver.resolve(lock, "gamesrc", {}, error);
  REQUIRE_MESSAGE(second.has_value(), error);
  CHECK(git.count("clone") == 1);  // reused, not fetched again

  SUBCASE("a lock whose tree does not match the commit is refused") {
    const SourceLock wrong = parse_lock(lock_json(kCommitA, std::string(40, 'f')));
    SourceResolver other(tmp.path() / "other_sources", git);
    CHECK_FALSE(other.resolve(wrong, "gamesrc", {}, error).has_value());
    CHECK(error.find("lock is wrong") != std::string::npos);
    CHECK_FALSE(fs::exists(tmp.path() / "other_sources" / "gamesrc" / kCommitA));
  }
  SUBCASE("a commit the repository does not have is refused") {
    const SourceLock missing = parse_lock(lock_json(kCommitB, tree));
    CHECK_FALSE(resolver.resolve(missing, "gamesrc", {}, error).has_value());
    CHECK(error.find("Checking out the locked commit") != std::string::npos);
  }
  SUBCASE("git not installed") {
    git.installed = false;
    const SourceLock other = parse_lock(lock_json(kCommitB, tree));
    CHECK_FALSE(resolver.resolve(other, "gamesrc", {}, error).has_value());
    CHECK(error.find("Git was not found") != std::string::npos);
  }
}

TEST_CASE("submodule gitlinks are checked against the lock") {
  const TempDir tmp;
  FakeGit git;
  git.repos["https://example.invalid/gamesrc"] = {kCommitA, kSourceFiles, {{"extern/lib", kSubCommit}}};
  const fs::path reference = tmp.path() / "reference";
  write_tree(reference, kSourceFiles);
  TreeHashOptions options;
  options.modes = kSourceModes;
  options.gitlinks["extern/lib"] = kSubCommit;
  const SourceLock lock = parse_lock(lock_json(kCommitA, tree_of(reference, options), /*with_submodule=*/true));

  std::string error;
  SourceResolver resolver(tmp.path() / "sources", git);
  CHECK_MESSAGE(resolver.resolve(lock, "gamesrc", {}, error).has_value(), error);

  // The tree matches (the lock's tree was taken from a commit recording kCommitB), but
  // the lock says the submodule is at kSubCommit: refused on the gitlink itself.
  git.repos["https://example.invalid/gamesrc"].gitlinks["extern/lib"] = kCommitB;
  options.gitlinks["extern/lib"] = kCommitB;
  const SourceLock inconsistent =
      parse_lock(lock_json(kCommitA, tree_of(reference, options), /*with_submodule=*/true));
  SourceResolver fresh(tmp.path() / "sources2", git);
  CHECK_FALSE(fresh.resolve(inconsistent, "gamesrc", {}, error).has_value());
  CHECK(error.find("submodule 'extern/lib'") != std::string::npos);
}

TEST_CASE("archives are accepted only if their tree matches the lock") {
  const TempDir tmp;
  FakeGit git;
  const fs::path archive = tmp.path() / "unzipped";
  write_tree(archive / "gamesrc-aaaa", kSourceFiles);  // GitHub-style wrapper folder
  TreeHashOptions options;
  options.modes = kSourceModes;
  const std::string tree = tree_of(archive / "gamesrc-aaaa", options);
  const SourceLock lock = parse_lock(lock_json(kCommitA, tree));
  SourceResolver resolver(tmp.path() / "sources", git);
  std::string error;

  const auto ok = resolver.resolve(lock, "gamesrc", {SourceOrigin::archive, archive}, error);
  REQUIRE_MESSAGE(ok.has_value(), error);
  CHECK(ok->pinned);
  CHECK(ok->dir == archive / "gamesrc-aaaa");
  CHECK(git.calls.empty());  // verified without git

  // Lost executable bit, as after extracting on Windows: still matches, because the
  // lock supplies the modes.
  CHECK(resolver.resolve(lock, "gamesrc", {SourceOrigin::archive, archive / "gamesrc-aaaa"}, error).has_value());

  write_text(archive / "gamesrc-aaaa" / "src" / "game.c", "int synthetic_main(void) { return 0; }\r\n");  // CRLF
  CHECK_FALSE(resolver.resolve(lock, "gamesrc", {SourceOrigin::archive, archive}, error).has_value());
  CHECK(error.find("git checkout") != std::string::npos);

  const SourceLock no_tree = parse_lock(
      R"({"lock_version":1,"sources":[{"name":"gamesrc","repo":"https://e.invalid/g","commit":")" + kCommitA +
      R"(","license":"CC0-1.0"}]})");
  CHECK_FALSE(resolver.resolve(no_tree, "gamesrc", {SourceOrigin::archive, archive}, error).has_value());
  CHECK(error.find("tree hash") != std::string::npos);
}

TEST_CASE("overrides are used as they are and marked unpinned unless they match") {
  const TempDir tmp;
  FakeGit git;
  const fs::path plain = tmp.path() / "plain";
  write_tree(plain, kSourceFiles);
  TreeHashOptions options;
  options.modes = kSourceModes;
  const std::string tree = tree_of(plain, options);
  const SourceLock lock = parse_lock(lock_json(kCommitA, tree));
  SourceResolver resolver(tmp.path() / "sources", git);
  std::string error;

  auto matching = resolver.resolve(lock, "gamesrc", {SourceOrigin::override_dir, plain}, error);
  REQUIRE(matching.has_value());
  CHECK(matching->pinned);

  write_text(plain / "src" / "local_change.c", "a developer's edit\n");
  auto edited = resolver.resolve(lock, "gamesrc", {SourceOrigin::override_dir, plain}, error);
  REQUIRE(edited.has_value());
  CHECK_FALSE(edited->pinned);
  CHECK(edited->actual.starts_with("tree:"));
  CHECK(edited->dir == plain);

  // A git checkout: clean at the locked commit is pinned; dirty or elsewhere is not.
  const fs::path checkout = tmp.path() / "checkout";
  write_tree(checkout, kSourceFiles);
  set_fake_checkout(checkout, kCommitA, tree, false);
  auto clean = resolver.resolve(lock, "gamesrc", {SourceOrigin::override_dir, checkout}, error);
  REQUIRE(clean.has_value());
  CHECK(clean->pinned);

  set_fake_checkout(checkout, kCommitA, tree, true);
  auto dirty = resolver.resolve(lock, "gamesrc", {SourceOrigin::override_dir, checkout}, error);
  REQUIRE(dirty.has_value());
  CHECK_FALSE(dirty->pinned);
  CHECK(dirty->actual.starts_with(kCommitA + "-dirty:tree:"));

  set_fake_checkout(checkout, kCommitB, tree, false);
  auto other = resolver.resolve(lock, "gamesrc", {SourceOrigin::override_dir, checkout}, error);
  REQUIRE(other.has_value());
  CHECK_FALSE(other->pinned);
  CHECK(other->actual == kCommitB);

  CHECK_FALSE(resolver.resolve(lock, "gamesrc", {SourceOrigin::override_dir, tmp.path() / "missing"}, error).has_value());
}

TEST_CASE("fetching from a local git repository works end to end" * doctest::description("skips if git is absent")) {
  if (!find_program("git")) {
    MESSAGE("git not found; skipping the local fetch test");
    return;
  }
  const TempDir tmp;
  const fs::path upstream = tmp.path() / "upstream";
  write_tree(upstream, kSourceFiles);
  SystemProcessRunner runner;
  auto git = [&](const fs::path& cwd, std::vector<std::string> args) {
    ProcessRequest request;
    request.argv = {"git", "-c", "core.autocrlf=false", "-c", "user.name=Synthetic Test",
                    "-c", "user.email=synthetic@example.invalid", "-c", "commit.gpgsign=false"};
    request.argv.insert(request.argv.end(), args.begin(), args.end());
    request.cwd = cwd;
    const ProcessResult r = runner.run(request);
    REQUIRE_MESSAGE(r.ok(), r.output);
    return r.output.substr(0, r.output.find_last_not_of("\r\n") + 1);
  };
  git(upstream, {"init", "-q"});
  git(upstream, {"add", "-A"});
  git(upstream, {"update-index", "--chmod=+x", "tools/run.sh"});
  git(upstream, {"commit", "-q", "-m", "synthetic"});
  const std::string commit = git(upstream, {"rev-parse", "HEAD"});
  const std::string tree = git(upstream, {"rev-parse", "HEAD^{tree}"});

  const std::u8string url_path = upstream.generic_u8string();
  std::string url = "file://";
  if (!url_path.starts_with(u8"/")) url += "/";
  url += std::string(reinterpret_cast<const char*>(url_path.data()), url_path.size());
  LockParseOptions local;
  local.allow_file_urls = true;
  const SourceLock lock = parse_lock(R"({"lock_version":1,"sources":[{"name":"gamesrc","repo":")" + url +
                                         R"(","commit":")" + commit + R"(","tree":")" + tree +
                                         R"(","license":"CC0-1.0","modes":{"tools/run.sh":"100755"}}]})",
                                     local);
  SourceResolver resolver(tmp.path() / "cache" / "sources", runner);
  std::string error;
  const auto fetched = resolver.resolve(lock, "gamesrc", {}, error);
  REQUIRE_MESSAGE(fetched.has_value(), error);
  CHECK(fetched->pinned);
  CHECK(read_text(fetched->dir / "src" / "game.c") == kSourceFiles.at("src/game.c"));

  // The same commit as a plain folder (as from an archive) verifies by tree hash.
  CHECK_MESSAGE(resolver.resolve(lock, "gamesrc", {SourceOrigin::archive, upstream}, error).has_value(), error);
}

// --- Lock version 2: dependencies pinned through a DEPS file -----------------

namespace {

// A made-up DEPS file in gclient syntax, exercising the subset the parser accepts.
// None of these names, URLs or hashes refer to a real project.
std::string synthetic_deps(const std::string& alpha_commit) {
  return std::string(R"(# Synthetic DEPS for tests.
use_relative_paths = True

vars = {
  'example_git': 'https://git.example.invalid',
  'alpha_revision': ')") + alpha_commit + R"(',
  'standalone': True,
  'tool_version': 'version:1.2.3',
}

deps = {
  'third_party/alpha': {
    'url': '{example_git}/alpha.git@' + Var('alpha_revision'),
    'condition': 'standalone',
  },
  'third_party/beta': Var('example_git') + '/beta.git' + '@' + ')" + std::string(40, '4') + R"(',
  "third_party/gamma": {
    "url": "{example_git}/gamma.git@)" + std::string(40, '5') + R"(",
  },
  'tools/fetcher': {
    'packages': [
      {
        'package': 'synthetic/tools/fetcher/${{platform}}',
        'version': Var('tool_version'),
      },
    ],
    'dep_type': 'cipd',
    'condition': 'standalone and host_os == "linux"',
  },
}

hooks = [
  {
    'name': 'synthetic_hook',
    'pattern': '.',
    'action': [ 'python3', 'never_run.py' ],
  },
]

recursedeps = [ 'third_party/alpha' ]
)";
}

const std::string kAlpha = std::string(40, '3');

std::string host_lock_json(const std::string& tree, const std::string& alpha_commit = kAlpha,
                           const std::string& extra_skipped = {}) {
  return R"({"lock_version":2,"sources":[
    {"name":"host","repo":"https://example.invalid/host","commit":")" + kCommitA + R"(","tree":")" + tree +
         R"(","license":"BSD-3-Clause",
     "deps":[{"path":"third_party/alpha","source":"host_alpha"},
             {"path":"third_party/beta","source":"host_beta"}],
     "skipped_deps":[{"path":"third_party/gamma","reason":"Not needed by our backend."},
                     {"path":"tools/fetcher","reason":"A CIPD tool we do not use."})" + extra_skipped + R"(]},
    {"name":"host_alpha","repo":"https://example.invalid/alpha","commit":")" + alpha_commit + R"(","license":"Apache-2.0"},
    {"name":"host_beta","repo":"https://example.invalid/beta","commit":")" + std::string(40, '4') +
         R"(","license":"MIT"}]})";
}

}  // namespace

TEST_CASE("lock version 2 adds deps and skipped_deps, and unknown fields are errors") {
  const SourceLock lock = parse_lock(host_lock_json(std::string(40, 'e')));
  const SourcePin& host = *lock.find("host");
  REQUIRE(host.deps.size() == 2);
  CHECK(host.deps[0].path == "third_party/alpha");
  CHECK(host.deps[0].source == "host_alpha");
  REQUIRE(host.skipped_deps.size() == 2);
  CHECK(host.skipped_deps[1].reason == "A CIPD tool we do not use.");

  auto rejected = [](const std::string& json, std::string_view needle) {
    std::string error;
    CHECK_FALSE(SourceLock::parse(json, error).has_value());
    CHECK_MESSAGE(error.find(needle) != std::string::npos, "error was: " << error);
  };
  const std::string head = R"({"name":"x","repo":"https://e.invalid/x","commit":")" + kCommitA + R"(","license":"MIT")";
  rejected(R"({"lock_version":1,"sources":[)" + head + R"(,"deps":[]}]})", "needs lock_version 2");
  rejected(R"({"lock_version":2,"sources":[)" + head + R"(,"submodule":[]}]})", "unknown field 'submodule'");
  rejected(R"({"lock_version":2,"sources":[],"comment":"x"})", "unknown field 'comment'");
  rejected(R"({"lock_version":3,"sources":[]})", "lock_version");
  rejected(R"({"lock_version":2,"sources":[)" + head + R"(,"skipped_deps":[{"path":"a"}]}]})", "reason");
  rejected(R"({"lock_version":2,"sources":[)" + head + R"(,"deps":[{"path":"a","source":"nope"}]}]})",
           "unknown source");
  rejected(R"({"lock_version":2,"sources":[)" + head +
               R"(,"deps":[{"path":"a","source":"x"}]}]})",
           "cannot be the entry itself");
  rejected(R"({"lock_version":2,"sources":[)" + head +
               R"(,"skipped_deps":[{"path":"a","reason":"r"},{"path":"a","reason":"r"}]}]})",
           "repeats the path");
  rejected(R"({"lock_version":2,"sources":[)" + head + R"(,"deps":[{"path":"a","source":"x","commit":"y"}]}]})",
           "unknown field 'commit'");

  std::string error;
  CHECK(SourceLock::parse(R"({"lock_version":1,"sources":[]})", error).has_value());  // version 1 still reads
}

TEST_CASE("gclient DEPS files are read without running them") {
  std::string error;
  const auto deps = parse_gclient_deps(synthetic_deps(kAlpha), error);
  REQUIRE_MESSAGE(deps.has_value(), error);
  REQUIRE(deps->size() == 4);
  CHECK(deps->at("third_party/alpha").url == "https://git.example.invalid/alpha.git@" + kAlpha);
  CHECK(deps->at("third_party/alpha").commit == kAlpha);
  CHECK(deps->at("third_party/beta").commit == std::string(40, '4'));
  CHECK(deps->at("third_party/gamma").commit == std::string(40, '5'));
  CHECK(deps->at("tools/fetcher").cipd);
  CHECK(deps->at("tools/fetcher").commit.empty());

  auto bad = [](const std::string& text, std::string_view needle) {
    std::string e;
    CHECK_FALSE(parse_gclient_deps(text, e).has_value());
    CHECK_MESSAGE(e.find(needle) != std::string::npos, "error was: " << e);
  };
  bad("deps = { 'a': '{nope}/a@x' }", "unknown variable 'nope'");
  bad("deps = { 'a': 'unterminated }", "unterminated string");
  bad("import os\ndeps = {}", "expected '='");
  bad("deps = { 'a': exec('rm') }", "unsupported name 'exec'");
  bad("deps = { 'a': 'x', 'a': 'y' }", "twice");
  bad("vars = {}", "no 'deps'");
  bad("deps = " + std::string(200, '[') + std::string(200, ']'), "nested too deeply");
  bad("deps = { 'a': 1 + 'x' }", "joins strings only");
}

TEST_CASE("a source's DEPS is cross-checked against the lock's deps") {
  const TempDir tmp;
  FakeGit git;
  git.repos["https://example.invalid/alpha"] = {kAlpha, {{"alpha.c", "synthetic alpha\n"}}, {}};
  git.repos["https://example.invalid/beta"] = {std::string(40, '4'), {{"beta.c", "synthetic beta\n"}}, {}};
  const fs::path host = tmp.path() / "host";
  auto make_host = [&](const std::string& deps_text) {
    fs::remove_all(host);
    write_text(host / "DEPS", deps_text);
    write_text(host / "src" / "host.cc", "synthetic host source\n");
    return tree_of(host);
  };
  SourceResolver resolver(tmp.path() / "sources", git);
  const std::map<std::string, SourceInput> inputs{{"host", {SourceOrigin::archive, host}}};
  std::string error;

  SUBCASE("matching: everything resolves, dependencies fetched as their own entries") {
    const SourceLock lock = parse_lock(host_lock_json(make_host(synthetic_deps(kAlpha))));
    const auto all = resolver.resolve_all(lock, inputs, error);
    REQUIRE_MESSAGE(all.has_value(), error);
    CHECK(all->size() == 3);
    CHECK(all->at("host").origin == SourceOrigin::archive);
    CHECK(all->at("host_alpha").origin == SourceOrigin::fetched);
    CHECK(read_text(all->at("host_beta").dir / "beta.c") == "synthetic beta\n");
    const auto& clone = git.calls.front();
    CHECK(std::find(clone.begin(), clone.end(), "core.longpaths=true") != clone.end());
  }
  SUBCASE("DEPS pins a dependency at another commit") {
    const SourceLock lock = parse_lock(host_lock_json(make_host(synthetic_deps(std::string(40, '9')))));
    CHECK_FALSE(resolver.resolve(lock, "host", inputs.at("host"), error).has_value());
    CHECK(error.find("DEPS pins 'third_party/alpha' at " + std::string(40, '9')) != std::string::npos);
  }
  SUBCASE("DEPS has an entry the lock neither pins nor skips") {
    std::string text = synthetic_deps(kAlpha);
    text.replace(text.find("deps = {"), 8, "deps = {\n  'third_party/delta': 'https://e.invalid/d@" +
                                                 std::string(40, '6') + "',");
    const SourceLock lock = parse_lock(host_lock_json(make_host(text)));
    CHECK_FALSE(resolver.resolve(lock, "host", inputs.at("host"), error).has_value());
    CHECK(error.find("'third_party/delta', which the lock neither pins") != std::string::npos);
  }
  SUBCASE("the lock skips something DEPS no longer has") {
    const SourceLock lock = parse_lock(host_lock_json(make_host(synthetic_deps(kAlpha)), kAlpha,
                                                      R"(,{"path":"third_party/old","reason":"Gone."})"));
    CHECK_FALSE(resolver.resolve(lock, "host", inputs.at("host"), error).has_value());
    CHECK(error.find("skipped_deps lists 'third_party/old'") != std::string::npos);
  }
  SUBCASE("a CIPD package listed as a git dependency") {
    std::string json = host_lock_json(make_host(synthetic_deps(kAlpha)));
    json.replace(json.find(R"({"path":"tools/fetcher","reason":"A CIPD tool we do not use."})"),
                 std::string_view(R"({"path":"tools/fetcher","reason":"A CIPD tool we do not use."})").size(),
                 R"({"path":"third_party/unused","reason":"placeholder"})");
    json.replace(json.find(R"({"path":"third_party/beta","source":"host_beta"})"),
                 std::string_view(R"({"path":"third_party/beta","source":"host_beta"})").size(),
                 R"({"path":"third_party/beta","source":"host_beta"},{"path":"tools/fetcher","source":"host_beta"})");
    const SourceLock lock = parse_lock(json);
    CHECK_FALSE(resolver.resolve(lock, "host", inputs.at("host"), error).has_value());
    CHECK(error.find("CIPD package") != std::string::npos);
  }
  SUBCASE("no DEPS file at all") {
    make_host(synthetic_deps(kAlpha));
    fs::remove(host / "DEPS");
    const SourceLock relocked = parse_lock(host_lock_json(tree_of(host)));
    CHECK_FALSE(resolver.resolve(relocked, "host", inputs.at("host"), error).has_value());
    CHECK(error.find("no DEPS file") != std::string::npos);
  }
  SUBCASE("an unpinned override is not cross-checked") {
    const SourceLock lock = parse_lock(host_lock_json(std::string(40, 'e')));  // tree will not match
    make_host(synthetic_deps(std::string(40, '9')));
    const auto local = resolver.resolve(lock, "host", {SourceOrigin::override_dir, host}, error);
    REQUIRE_MESSAGE(local.has_value(), error);
    CHECK_FALSE(local->pinned);
  }
}

// --- Build cache ------------------------------------------------------------

TEST_CASE("a dump builds once and is reused") {
  BuildFixture f;
  std::vector<BuildProgress> progress;
  f.request.progress = [&progress](const BuildProgress& p) { progress.push_back(p); };

  const BuildOutcome first = f.build_ok();
  CHECK_FALSE(first.reused);
  CHECK(f.log.step_runs == 2);
  CHECK(f.log.toolchain_checks == 1);
  CHECK(read_text(first.build_dir / "step2.txt") == "output of step 2");
  CHECK(fs::exists(first.build_dir / "disc" / "sys" / "boot.bin"));
  CHECK(fs::exists(first.build_dir / "disc" / "files" / "data" / "board.bin"));
  CHECK(fs::exists(first.build_dir / "manifest.json"));
  CHECK(first.build_dir.parent_path() == f.tmp.path() / "cache" / "builds" / "fake");

  const BuildManifest& m = first.manifest;
  CHECK(m.key.size() == 64);
  CHECK(first.build_dir.filename().string() == m.key.substr(0, 32));
  CHECK(m.recipe == "fake");
  CHECK(m.recipe_version == 1);
  CHECK(m.runtime_build_version == "runtime-1");
  CHECK(m.dump_sha256 == f.request.dump.sha256);
  CHECK(m.game_version == "GMPE01_00");
  CHECK(m.toolchain.description() == "fakecc 1.0.0 (x86_64-synthetic-none)");
  CHECK_FALSE(m.unpinned);
  REQUIRE(m.sources.size() == 1);
  CHECK(m.sources[0].origin == "archive");
  CHECK(m.sources[0].pinned);

  // The steps saw the extracted disc and the verified source.
  REQUIRE(f.log.contexts.size() == 1);
  CHECK(f.log.contexts[0].disc_dir.filename() == "disc");
  CHECK(f.log.contexts[0].sources.at("gamesrc").dir == f.archive);
  CHECK(f.log.contexts[0].game_version == "GMPE01_00");

  // Progress: every stage reported, in order, ending at the last stage.
  REQUIRE_FALSE(progress.empty());
  std::set<std::string> stages;
  for (const BuildProgress& p : progress) {
    stages.insert(p.stage);
    CHECK(p.stage_count == 5);  // sources, disc, 2 steps, finishing
    CHECK(p.fraction >= 0.0);
    CHECK(p.fraction <= 1.0);
  }
  CHECK(stages == std::set<std::string>{"Checking sources", "Extracting disc", "Fake step 1", "Fake step 2", "Finishing"});
  CHECK(progress.back().stage == "Finishing");
  CHECK(progress.back().fraction == 1.0);
  for (std::size_t i = 1; i < progress.size(); ++i) CHECK(progress[i].stage_index >= progress[i - 1].stage_index);

  const BuildOutcome second = f.build_ok();
  CHECK(second.reused);
  CHECK(second.build_dir == first.build_dir);
  CHECK(second.manifest.key == m.key);
  CHECK(f.log.step_runs == 2);         // nothing ran again
  CHECK(f.log.toolchain_checks == 2);  // checked on a cache hit too: the toolchain is part of the key
  CHECK(f.recipe_dir_entries() == std::vector<std::string>{".lock", m.key.substr(0, 32)});

  std::string error;
  const auto parsed = BuildManifest::parse(read_text(first.build_dir / "manifest.json"), error);
  REQUIRE_MESSAGE(parsed.has_value(), error);
  CHECK(parsed->key == m.key);
  CHECK(parsed->sources[0].actual == m.sources[0].actual);
}

TEST_CASE("a rebuild happens only when the dump, runtime, recipe, toolchain or pins change") {
  BuildFixture f;
  const std::string base = f.build_ok().manifest.key;
  std::set<std::string> keys{base};

  f.recipe.runtime_version = "runtime-2";
  const BuildOutcome runtime = f.build_ok();
  CHECK_FALSE(runtime.reused);
  keys.insert(runtime.manifest.key);
  f.recipe.runtime_version = "runtime-1";
  CHECK(f.build_ok().reused);  // back to the first inputs: the first build is still there

  f.recipe.recipe_version = 2;
  const BuildOutcome recipe = f.build_ok();
  CHECK_FALSE(recipe.reused);
  keys.insert(recipe.manifest.key);
  f.recipe.recipe_version = 1;

  f.recipe.toolchain.version = "1.0.1";  // a compiler upgrade
  const BuildOutcome compiler = f.build_ok();
  CHECK_FALSE(compiler.reused);
  CHECK(compiler.manifest.toolchain.version == "1.0.1");
  keys.insert(compiler.manifest.key);
  f.recipe.toolchain.target = "aarch64-synthetic-none";
  const BuildOutcome target = f.build_ok();
  CHECK_FALSE(target.reused);
  keys.insert(target.manifest.key);
  f.recipe.toolchain = {"fakecc", "1.0.0", "x86_64-synthetic-none"};
  CHECK(f.build_ok().reused);

  DiscSpec rev1 = default_spec();
  rev1.version = 1;
  const BuiltDisc other_disc = build_disc(rev1);
  const auto saved = f.request.dump;
  f.request.dump = {sha256_of(other_disc.image), other_disc.image.size(), f.tmp.write("rev1.iso", other_disc.image),
                    "GMPE01_01"};
  const BuildOutcome dump = f.build_ok();
  CHECK_FALSE(dump.reused);
  keys.insert(dump.manifest.key);
  f.request.dump = saved;

  TreeHashOptions options;
  options.modes = kSourceModes;
  f.recipe.lock = parse_lock(lock_json(kCommitB, tree_of(f.archive, options)));  // a new pin (same files)
  const BuildOutcome pins = f.build_ok();
  CHECK_FALSE(pins.reused);
  keys.insert(pins.manifest.key);

  CHECK(keys.size() == 7);
  CHECK(f.log.step_runs == 2 * 7);

  f.request.force_rebuild = true;
  const BuildOutcome forced = f.build_ok();
  CHECK_FALSE(forced.reused);
  CHECK(forced.manifest.key == pins.manifest.key);
}

TEST_CASE("a failed, cancelled or interrupted build leaves nothing behind") {
  BuildFixture f;
  std::string error;

  SUBCASE("a step fails") {
    f.recipe.second_hook = [](BuildContext&) { return Result::failure("synthetic compiler error"); };
    CHECK_FALSE(f.build(error).has_value());
    CHECK(error == "Fake step 2 failed: synthetic compiler error");
    CHECK(f.recipe_dir_entries() == std::vector<std::string>{".lock"});
  }
  SUBCASE("a step throws") {
    f.recipe.first_hook = [](BuildContext&) -> Result { throw std::runtime_error("boom"); };
    CHECK_FALSE(f.build(error).has_value());
    CHECK(error.find("Fake step 1 failed: unexpected error: boom") != std::string::npos);
    CHECK(f.recipe_dir_entries() == std::vector<std::string>{".lock"});
  }
  SUBCASE("the user cancels mid-build") {
    std::atomic<bool> cancel{false};
    f.request.cancelled = [&cancel] { return cancel.load(); };
    f.recipe.first_hook = [&cancel](BuildContext&) {
      cancel = true;
      return Result::success();
    };
    CHECK_FALSE(f.build(error).has_value());
    CHECK(error.find("cancelled") != std::string::npos);
    CHECK(f.log.step_runs == 1);
    CHECK(f.recipe_dir_entries() == std::vector<std::string>{".lock"});
    cancel = false;
    f.recipe.first_hook = nullptr;
  }
  SUBCASE("the process died mid-build earlier") {
    // What a crash leaves: a temp folder, and a key folder with no manifest.
    const BuildOutcome done = f.build_ok();
    const std::string key_dir = done.manifest.key.substr(0, 32);
    fs::remove(done.build_dir / "manifest.json");
    write_text(f.tmp.path() / "cache" / "builds" / "fake" / ".tmp-0123456789abcdef0123456789abcdef" / "half.o", "x");
    const BuildOutcome again = f.build_ok();
    CHECK_FALSE(again.reused);  // a folder without a complete manifest is never trusted
    CHECK(f.recipe_dir_entries() == std::vector<std::string>{".lock", key_dir});
    CHECK(fs::exists(again.build_dir / "manifest.json"));
  }

  // Whatever happened, the next build succeeds.
  f.recipe.first_hook = nullptr;
  f.recipe.second_hook = nullptr;
  f.request.cancelled = nullptr;
  CHECK(f.build_ok().build_dir.parent_path() == f.tmp.path() / "cache" / "builds" / "fake");
}

TEST_CASE("build errors are clear") {
  BuildFixture f;
  std::string error;

  SUBCASE("toolchain not found") {
    f.recipe.toolchain_present = false;
    CHECK_FALSE(f.build(error).has_value());
    CHECK(error == "fakecc was not found. Install it and try again.");
    CHECK(f.log.step_runs == 0);
  }
  SUBCASE("toolchain removed after a build: the cached build is not handed out blind") {
    f.build_ok();
    f.recipe.toolchain_present = false;
    CHECK_FALSE(f.build(error).has_value());
    CHECK(error.find("fakecc was not found") != std::string::npos);
  }
  SUBCASE("a toolchain identified by its path") {
    f.recipe.toolchain = {"C:\\LLVM\\bin\\clang-cl.exe", "22.1.3", "x86_64-pc-windows-msvc"};
    CHECK_FALSE(f.build(error).has_value());
    CHECK(error.find("looks like a path") != std::string::npos);
    f.recipe.toolchain = {"clang", "22.1.3", "/usr/lib/llvm-22"};
    CHECK_FALSE(f.build(error).has_value());
    CHECK(error.find("looks like a path") != std::string::npos);
    f.recipe.toolchain = {"clang", "", ""};
    CHECK_FALSE(f.build(error).has_value());
    CHECK(error.find("together") != std::string::npos);
  }
  SUBCASE("the dump changed after it was verified") {
    f.request.dump.size += 1;
    CHECK_FALSE(f.build(error).has_value());
    CHECK(error.find("changed since it was verified") != std::string::npos);
  }
  SUBCASE("an archive that does not match the lock") {
    write_text(f.archive / "src" / "game.c", "edited\n");
    CHECK_FALSE(f.build(error).has_value());
    CHECK(error.find("does not match the locked commit") != std::string::npos);
  }
  SUBCASE("an input for a source the lock does not have") {
    f.request.source_inputs["nope"] = {SourceOrigin::archive, f.archive};
    CHECK_FALSE(f.build(error).has_value());
    CHECK(error.find("no source named 'nope'") != std::string::npos);
  }
  SUBCASE("a disc that breaks a strict extraction rule") {
    BuiltDisc strict = build_disc(default_spec());
    put32(strict.image, strict.entry_at(strict.index_of("board.bin")) + 4,
          get32(strict.image, strict.entry_at(strict.index_of("deep.bin")) + 4));
    f.request.dump = {sha256_of(strict.image), strict.image.size(), f.tmp.write("strict.iso", strict.image), "GMPE01_00"};
    CHECK_FALSE(f.build(error).has_value());
    CHECK(error.find("cannot be extracted safely") != std::string::npos);
    CHECK(f.recipe_dir_entries() == std::vector<std::string>{".lock"});
  }
  SUBCASE("a second build of the same recipe while one is running") {
    std::string inner_error;
    f.recipe.first_hook = [&](BuildContext&) {
      RecipeLog other_log;
      FakeRecipe other(other_log);
      other.lock = f.recipe.lock;
      BuildCache second(f.tmp.path() / "cache", f.git);
      CHECK_FALSE(second.build(other, f.request, inner_error).has_value());
      return Result::success();
    };
    CHECK_MESSAGE(f.build(error).has_value(), error);
    CHECK(inner_error.find("already running") != std::string::npos);
  }
}

TEST_CASE("an unpinned override is built separately and marked") {
  BuildFixture f;
  const BuildOutcome pinned = f.build_ok();

  const fs::path local = f.tmp.path() / "local_checkout";
  write_tree(local, kSourceFiles);
  write_text(local / "src" / "experiment.c", "a local experiment\n");
  f.request.source_inputs["gamesrc"] = {SourceOrigin::override_dir, local};
  const BuildOutcome unpinned = f.build_ok();
  CHECK_FALSE(unpinned.reused);
  CHECK(unpinned.manifest.unpinned);
  CHECK(unpinned.manifest.key != pinned.manifest.key);
  REQUIRE(unpinned.manifest.sources.size() == 1);
  CHECK(unpinned.manifest.sources[0].origin == "override");
  CHECK_FALSE(unpinned.manifest.sources[0].pinned);
  CHECK(f.log.contexts.back().unpinned);
  CHECK(read_text(unpinned.build_dir / "manifest.json").find("\"unpinned\": true") != std::string::npos);

  // Editing the override changes its fingerprint, so it rebuilds.
  CHECK(f.build_ok().reused);
  write_text(local / "src" / "experiment.c", "edited again\n");
  CHECK_FALSE(f.build_ok().reused);
}

TEST_CASE("a recipe with no disc and no sources still caches") {
  BuildFixture f;
  f.recipe.disc = false;
  f.recipe.lock = SourceLock{};
  f.request.source_inputs.clear();
  const BuildOutcome first = f.build_ok();
  CHECK_FALSE(fs::exists(first.build_dir / "disc"));
  CHECK(first.manifest.sources.empty());
  CHECK(f.build_ok().reused);
  CHECK(f.git.calls.empty());
}

TEST_CASE("a VerifiedDump comes only from an identified dump and its build") {
  BuildFixture f;
  std::string error;
  const std::string sha1 = sha1_of(read_text(f.request.dump.path));
  const auto known = KnownDumps::parse(R"({"version":2,"dumps":[{"title":"mp4","region":"USA","revision":"Rev 0",)"
                                       R"("supported":true,"game_version":"GMPE01_00","disc_id":"GMPE01",)"
                                       R"("disc_version":0,"sha1":")" + sha1 +
                                           R"(","sha256":")" + f.request.dump.sha256 + R"(","size":)" +
                                           std::to_string(f.request.dump.size) + "}]}",
                                       error);
  REQUIRE_MESSAGE(known.has_value(), error);
  const Importer importer(*known);
  const IdentifyReport report = importer.identify(f.request.dump.path);
  REQUIRE(report.match.has_value());

  const auto input = dump_for_build(report, f.request.dump.path, error);
  REQUIRE_MESSAGE(input.has_value(), error);
  CHECK(input->sha256 == f.request.dump.sha256);
  CHECK(input->size == f.request.dump.size);
  CHECK(input->game_version == "GMPE01_00");
  f.request.dump = *input;
  const BuildOutcome outcome = f.build_ok();

  const auto verified = make_verified(report, outcome, error);
  REQUIRE_MESSAGE(verified.has_value(), error);
  CHECK(verified->title == TitleId::mp4);
  CHECK(verified->region == "USA");
  CHECK(verified->revision == "Rev 0");
  CHECK(verified->sha256 == f.request.dump.sha256);
  CHECK(verified->build_dir == outcome.build_dir);

  // A build of another dump, an unknown dump, or a vanished build folder is refused.
  BuildOutcome other = outcome;
  other.manifest.dump_sha256 = std::string(64, '0');
  CHECK_FALSE(make_verified(report, other, error).has_value());
  CHECK(error.find("not a build of this dump") != std::string::npos);

  IdentifyReport unknown = report;
  unknown.match.reset();
  CHECK_FALSE(make_verified(unknown, outcome, error).has_value());
  CHECK_FALSE(dump_for_build(unknown, f.request.dump.path, error).has_value());

  fs::remove_all(outcome.build_dir);
  CHECK_FALSE(make_verified(report, outcome, error).has_value());
  CHECK(error.find("Build the game again") != std::string::npos);
}

// --- Renames that survive transient Windows file locks ----------------------------------
// Defender or the indexer briefly holding a just-written file makes renaming its folder
// fail with access denied. The retry loop is tested here with an injected rename, so the
// tests do not depend on any scanner's timing.

namespace {

struct FakeRename {
  int transient_failures = 0;  // fail this many times with ec_to_fail, then succeed
  std::error_code ec_to_fail = std::make_error_code(std::errc::permission_denied);
  int calls = 0;
  void operator()(const fs::path&, const fs::path&, std::error_code& ec) {
    ++calls;
    if (calls <= transient_failures) ec = ec_to_fail;
  }
};

}  // namespace

TEST_CASE("renames retry transient permission errors with bounded backoff") {
  const fs::path from = "from";
  const fs::path to = "to";
  std::vector<std::chrono::milliseconds> sleeps;
  auto sleep = [&sleeps](std::chrono::milliseconds d) { sleeps.push_back(d); };
  const RenameRetryPolicy policy;

  SUBCASE("the default bounds are about 4 s of waiting over at most 20 retries") {
    CHECK(policy.max_retries == 20);
    CHECK(policy.first_delay == std::chrono::milliseconds(10));
    CHECK(policy.max_delay == std::chrono::milliseconds(250));
  }

  SUBCASE("a first-try success does not sleep") {
    FakeRename fake;
    std::error_code ec = std::make_error_code(std::errc::io_error);  // stale input is cleared
    rename_with_retry(from, to, ec, policy, std::ref(fake), sleep);
    CHECK_FALSE(ec);
    CHECK(fake.calls == 1);
    CHECK(sleeps.empty());
  }

  SUBCASE("a few transient failures are retried until the rename goes through") {
    FakeRename fake;
    fake.transient_failures = 3;
    std::error_code ec;
    rename_with_retry(from, to, ec, policy, std::ref(fake), sleep);
    CHECK_FALSE(ec);
    CHECK(fake.calls == 4);
    using std::chrono::milliseconds;
    CHECK(sleeps == std::vector<milliseconds>{milliseconds(10), milliseconds(20), milliseconds(40)});
  }

  SUBCASE("a failure that never clears gives up after the bounds, with the last error") {
    FakeRename fake;
    fake.transient_failures = 1000;
    std::error_code ec;
    rename_with_retry(from, to, ec, policy, std::ref(fake), sleep);
    CHECK(ec == std::errc::permission_denied);
    CHECK(fake.calls == policy.max_retries + 1);
    REQUIRE(sleeps.size() == static_cast<std::size_t>(policy.max_retries));
    std::chrono::milliseconds total{0};
    for (const auto d : sleeps) {
      CHECK(d <= policy.max_delay);
      total += d;
    }
    CHECK(total == std::chrono::milliseconds(10 + 20 + 40 + 80 + 160 + 15 * 250));  // 4060 ms
  }

  SUBCASE("any other error fails at once") {
    for (const std::errc e : {std::errc::no_such_file_or_directory, std::errc::file_exists,
                              std::errc::directory_not_empty, std::errc::io_error}) {
      CAPTURE(static_cast<int>(e));
      FakeRename fake;
      fake.transient_failures = 1000;
      fake.ec_to_fail = std::make_error_code(e);
      std::error_code ec;
      sleeps.clear();
      rename_with_retry(from, to, ec, policy, std::ref(fake), sleep);
      CHECK(ec == e);
      CHECK(fake.calls == 1);
      CHECK(sleeps.empty());
    }
  }

  SUBCASE("which errors count as transient") {
    CHECK_FALSE(is_transient_rename_error({}));
    CHECK(is_transient_rename_error(std::make_error_code(std::errc::permission_denied)));
    CHECK_FALSE(is_transient_rename_error(std::make_error_code(std::errc::no_such_file_or_directory)));
    CHECK_FALSE(is_transient_rename_error(std::make_error_code(std::errc::file_exists)));
#ifdef _WIN32
    // ERROR_ACCESS_DENIED, ERROR_SHARING_VIOLATION, ERROR_LOCK_VIOLATION; not ERROR_FILE_NOT_FOUND.
    CHECK(is_transient_rename_error(std::error_code(5, std::system_category())));
    CHECK(is_transient_rename_error(std::error_code(32, std::system_category())));
    CHECK(is_transient_rename_error(std::error_code(33, std::system_category())));
    CHECK_FALSE(is_transient_rename_error(std::error_code(2, std::system_category())));
#endif
  }

  SUBCASE("a sharing violation is retried like access denied") {
#ifdef _WIN32
    FakeRename fake;
    fake.transient_failures = 2;
    fake.ec_to_fail = std::error_code(32, std::system_category());
    std::error_code ec;
    rename_with_retry(from, to, ec, policy, std::ref(fake), sleep);
    CHECK_FALSE(ec);
    CHECK(fake.calls == 3);
#endif
  }
}

TEST_CASE("rename_with_retry moves a real folder, atomically and in one piece") {
  TempDir dir;
  const fs::path from = dir.path() / "tmp-build";
  const fs::path to = dir.path() / "final";
  fs::create_directories(from / "sub");
  std::ofstream(from / "sub" / "file.txt") << "synthetic";

  std::error_code ec;
  rename_with_retry(from, to, ec);
  REQUIRE_FALSE(ec);
  CHECK_FALSE(fs::exists(from));
  CHECK(fs::exists(to / "sub" / "file.txt"));

  // A missing source is not transient: it fails straight away rather than after the bounds.
  rename_with_retry(dir.path() / "missing", dir.path() / "elsewhere", ec);
  CHECK(ec);
  CHECK_FALSE(is_transient_rename_error(ec));

#ifdef _WIN32
  SUBCASE("a file held open inside the folder only delays the rename") {
    // Stands in for a scanner: an open handle without FILE_SHARE_DELETE on a file inside
    // the folder makes the folder's rename fail with access denied until it is closed.
    const fs::path held_dir = dir.path() / "held";
    const fs::path moved = dir.path() / "held-moved";
    fs::create_directories(held_dir);
    auto holder = std::make_unique<std::ofstream>(held_dir / "fresh.bin");
    *holder << "synthetic";
    holder->flush();

    std::error_code direct;
    fs::rename(held_dir, moved, direct);
    if (!direct) {
      WARN_MESSAGE(false, "this file system allowed the rename with a file open; nothing to check");
      return;
    }
    CHECK(is_transient_rename_error(direct));

    std::thread release([&holder] {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      holder.reset();
    });
    rename_with_retry(held_dir, moved, ec);
    release.join();
    CHECK_FALSE(ec);
    CHECK(fs::exists(moved / "fresh.bin"));
  }
#endif
}
