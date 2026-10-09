#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
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
#include <vector>

#include "asset_import_test_support.h"
#include "mpt/asset_import/build.h"
#include "mpt/asset_import/git_tree.h"
#include "mpt/asset_import/process.h"
#include "mpt/asset_import/sha1.h"
#include "mpt/asset_import/sha256.h"
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
    info.description = "fakecc 1.0";
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
  std::function<Result(BuildContext&)> first_hook;
  std::function<Result(BuildContext&)> second_hook;

 private:
  RecipeLog& log_;
};

std::string sha256_of(const std::vector<std::byte>& data) {
  Sha256 sha;
  sha.update(data);
  return sha.finish_hex();
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
  rejected(R"({"lock_version":2,"sources":[]})", "lock_version");
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
  const auto first = resolver.resolve(*lock.find("gamesrc"), {}, error);
  REQUIRE_MESSAGE(first.has_value(), error);
  CHECK(first->pinned);
  CHECK(first->origin == SourceOrigin::fetched);
  CHECK(first->dir == tmp.path() / "sources" / "gamesrc" / kCommitA);
  CHECK(read_text(first->dir / "src" / "game.c") == kSourceFiles.at("src/game.c"));
  CHECK(git.count("clone") == 1);
  const auto& clone = git.calls.front();
  CHECK(std::find(clone.begin(), clone.end(), "--filter=blob:none") != clone.end());
  CHECK_FALSE(fs::exists(tmp.path() / "sources" / "gamesrc" / (".tmp-" + kCommitA)));

  const auto second = resolver.resolve(*lock.find("gamesrc"), {}, error);
  REQUIRE_MESSAGE(second.has_value(), error);
  CHECK(git.count("clone") == 1);  // reused, not fetched again

  SUBCASE("a lock whose tree does not match the commit is refused") {
    const SourceLock wrong = parse_lock(lock_json(kCommitA, std::string(40, 'f')));
    SourceResolver other(tmp.path() / "other_sources", git);
    CHECK_FALSE(other.resolve(*wrong.find("gamesrc"), {}, error).has_value());
    CHECK(error.find("lock is wrong") != std::string::npos);
    CHECK_FALSE(fs::exists(tmp.path() / "other_sources" / "gamesrc" / kCommitA));
  }
  SUBCASE("a commit the repository does not have is refused") {
    const SourceLock missing = parse_lock(lock_json(kCommitB, tree));
    CHECK_FALSE(resolver.resolve(*missing.find("gamesrc"), {}, error).has_value());
    CHECK(error.find("Checking out the locked commit") != std::string::npos);
  }
  SUBCASE("git not installed") {
    git.installed = false;
    const SourceLock other = parse_lock(lock_json(kCommitB, tree));
    CHECK_FALSE(resolver.resolve(*other.find("gamesrc"), {}, error).has_value());
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
  CHECK_MESSAGE(resolver.resolve(*lock.find("gamesrc"), {}, error).has_value(), error);

  // The tree matches (the lock's tree was taken from a commit recording kCommitB), but
  // the lock says the submodule is at kSubCommit: refused on the gitlink itself.
  git.repos["https://example.invalid/gamesrc"].gitlinks["extern/lib"] = kCommitB;
  options.gitlinks["extern/lib"] = kCommitB;
  const SourceLock inconsistent =
      parse_lock(lock_json(kCommitA, tree_of(reference, options), /*with_submodule=*/true));
  SourceResolver fresh(tmp.path() / "sources2", git);
  CHECK_FALSE(fresh.resolve(*inconsistent.find("gamesrc"), {}, error).has_value());
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

  const auto ok = resolver.resolve(*lock.find("gamesrc"), {SourceOrigin::archive, archive}, error);
  REQUIRE_MESSAGE(ok.has_value(), error);
  CHECK(ok->pinned);
  CHECK(ok->dir == archive / "gamesrc-aaaa");
  CHECK(git.calls.empty());  // verified without git

  // Lost executable bit, as after extracting on Windows: still matches, because the
  // lock supplies the modes.
  CHECK(resolver.resolve(*lock.find("gamesrc"), {SourceOrigin::archive, archive / "gamesrc-aaaa"}, error).has_value());

  write_text(archive / "gamesrc-aaaa" / "src" / "game.c", "int synthetic_main(void) { return 0; }\r\n");  // CRLF
  CHECK_FALSE(resolver.resolve(*lock.find("gamesrc"), {SourceOrigin::archive, archive}, error).has_value());
  CHECK(error.find("git checkout") != std::string::npos);

  const SourceLock no_tree = parse_lock(
      R"({"lock_version":1,"sources":[{"name":"gamesrc","repo":"https://e.invalid/g","commit":")" + kCommitA +
      R"(","license":"CC0-1.0"}]})");
  CHECK_FALSE(resolver.resolve(*no_tree.find("gamesrc"), {SourceOrigin::archive, archive}, error).has_value());
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
  const SourcePin& pin = *lock.find("gamesrc");
  SourceResolver resolver(tmp.path() / "sources", git);
  std::string error;

  auto matching = resolver.resolve(pin, {SourceOrigin::override_dir, plain}, error);
  REQUIRE(matching.has_value());
  CHECK(matching->pinned);

  write_text(plain / "src" / "local_change.c", "a developer's edit\n");
  auto edited = resolver.resolve(pin, {SourceOrigin::override_dir, plain}, error);
  REQUIRE(edited.has_value());
  CHECK_FALSE(edited->pinned);
  CHECK(edited->actual.starts_with("tree:"));
  CHECK(edited->dir == plain);

  // A git checkout: clean at the locked commit is pinned; dirty or elsewhere is not.
  const fs::path checkout = tmp.path() / "checkout";
  write_tree(checkout, kSourceFiles);
  set_fake_checkout(checkout, kCommitA, tree, false);
  auto clean = resolver.resolve(pin, {SourceOrigin::override_dir, checkout}, error);
  REQUIRE(clean.has_value());
  CHECK(clean->pinned);

  set_fake_checkout(checkout, kCommitA, tree, true);
  auto dirty = resolver.resolve(pin, {SourceOrigin::override_dir, checkout}, error);
  REQUIRE(dirty.has_value());
  CHECK_FALSE(dirty->pinned);
  CHECK(dirty->actual.starts_with(kCommitA + "-dirty:tree:"));

  set_fake_checkout(checkout, kCommitB, tree, false);
  auto other = resolver.resolve(pin, {SourceOrigin::override_dir, checkout}, error);
  REQUIRE(other.has_value());
  CHECK_FALSE(other->pinned);
  CHECK(other->actual == kCommitB);

  CHECK_FALSE(resolver.resolve(pin, {SourceOrigin::override_dir, tmp.path() / "missing"}, error).has_value());
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
  const auto fetched = resolver.resolve(*lock.find("gamesrc"), {}, error);
  REQUIRE_MESSAGE(fetched.has_value(), error);
  CHECK(fetched->pinned);
  CHECK(read_text(fetched->dir / "src" / "game.c") == kSourceFiles.at("src/game.c"));

  // The same commit as a plain folder (as from an archive) verifies by tree hash.
  CHECK_MESSAGE(resolver.resolve(*lock.find("gamesrc"), {SourceOrigin::archive, upstream}, error).has_value(), error);
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
  CHECK(m.toolchain == "fakecc 1.0");
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
  CHECK(f.log.toolchain_checks == 1);  // a cache hit needs no toolchain
  CHECK(f.recipe_dir_entries() == std::vector<std::string>{".lock", m.key.substr(0, 32)});

  std::string error;
  const auto parsed = BuildManifest::parse(read_text(first.build_dir / "manifest.json"), error);
  REQUIRE_MESSAGE(parsed.has_value(), error);
  CHECK(parsed->key == m.key);
  CHECK(parsed->sources[0].actual == m.sources[0].actual);
}

TEST_CASE("a rebuild happens only when the dump, runtime, recipe or pins change") {
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

  CHECK(keys.size() == 5);
  CHECK(f.log.step_runs == 2 * 5);

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
