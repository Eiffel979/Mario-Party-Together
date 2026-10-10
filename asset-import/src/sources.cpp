#include "mpt/asset_import/sources.h"

#include <fstream>
#include <sstream>

#include "mpt/asset_import/byte_source.h"
#include "mpt/asset_import/gclient_deps.h"
#include "mpt/asset_import/git_tree.h"

namespace mpt::asset_import {
namespace {

namespace fs = std::filesystem;

std::string trim(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
  return text;
}

TreeHashOptions tree_options(const SourcePin& pin) {
  TreeHashOptions options;
  options.modes = pin.modes;
  for (const SubmodulePin& sub : pin.submodules) options.gitlinks[sub.path] = sub.gitlink;
  return options;
}

std::string git_failure(const ProcessResult& result, std::string_view what) {
  if (!result.started) return "Git was not found. Install Git, make sure it is on PATH, and try again.";
  std::string output = trim(result.output);
  if (output.size() > 400) output = output.substr(0, 400) + "...";
  return std::string(what) + " failed (git exit code " + std::to_string(result.exit_code) + "): " + output;
}

}  // namespace

std::string_view to_string(SourceOrigin origin) {
  switch (origin) {
    case SourceOrigin::fetched: return "fetched";
    case SourceOrigin::archive: return "archive";
    case SourceOrigin::override_dir: return "override";
  }
  return "unknown";
}

ProcessResult SourceResolver::git(const fs::path& cwd, std::vector<std::string> args) {
  ProcessRequest request;
  request.argv.push_back("git");
  for (std::string& arg : args) request.argv.push_back(std::move(arg));
  request.cwd = cwd;
  // Never wait on a credential prompt: the pins are public repositories.
  request.env = {{"GIT_TERMINAL_PROMPT", "0"}, {"GCM_INTERACTIVE", "never"}};
  return runner_.run(request);
}

std::optional<ResolvedSource> SourceResolver::resolve(const SourceLock& lock, std::string_view name,
                                                      const SourceInput& input, std::string& error) {
  const SourcePin* pin = lock.find(name);
  if (!pin) {
    error = "The lock has no source named '" + std::string(name) + "'.";
    return std::nullopt;
  }
  std::optional<ResolvedSource> resolved;
  switch (input.origin) {
    case SourceOrigin::fetched: resolved = fetch(*pin, error); break;
    case SourceOrigin::archive: resolved = from_archive(*pin, input.path, error); break;
    case SourceOrigin::override_dir: resolved = from_override(*pin, input.path, error); break;
  }
  if (!resolved) return std::nullopt;
  // An unpinned override is not the locked commit, so its DEPS says nothing about the lock.
  if (resolved->pinned && (!pin->deps.empty() || !pin->skipped_deps.empty()) &&
      !check_deps(lock, *pin, resolved->dir, error)) {
    return std::nullopt;
  }
  return resolved;
}

std::optional<std::map<std::string, ResolvedSource>> SourceResolver::resolve_all(
    const SourceLock& lock, const std::map<std::string, SourceInput>& inputs, std::string& error) {
  for (const auto& [name, input] : inputs) {
    if (!lock.find(name)) {
      error = "The lock has no source named '" + name + "'.";
      return std::nullopt;
    }
  }
  std::map<std::string, ResolvedSource> out;
  for (const SourcePin& pin : lock.pins()) {
    const auto input = inputs.find(pin.name);
    auto source = resolve(lock, pin.name, input != inputs.end() ? input->second : SourceInput{}, error);
    if (!source) return std::nullopt;
    out.emplace(pin.name, std::move(*source));
  }
  return out;
}

bool SourceResolver::check_deps(const SourceLock& lock, const SourcePin& pin, const fs::path& dir,
                                std::string& error) {
  std::ifstream file(dir / "DEPS", std::ios::binary);
  if (!file) {
    error = pin.name + " has no DEPS file, but the lock lists dependencies for it.";
    return false;
  }
  std::ostringstream text;
  text << file.rdbuf();
  std::string parse_error;
  const auto deps = parse_gclient_deps(text.str(), parse_error);
  if (!deps) {
    error = "Could not read " + pin.name + "'s DEPS: " + parse_error;
    return false;
  }
  const std::string where = "The lock for " + pin.name + " does not match its DEPS: ";
  std::map<std::string, const DepPin*> pinned;
  std::map<std::string, const SkippedDep*> skipped;
  for (const DepPin& dep : pin.deps) pinned[dep.path] = &dep;
  for (const SkippedDep& dep : pin.skipped_deps) skipped[dep.path] = &dep;

  for (const auto& [path, dep] : *deps) {
    if (const auto it = pinned.find(path); it != pinned.end()) {
      const SourcePin* source = lock.find(it->second->source);
      if (dep.cipd) {
        error = where + "'" + path + "' is a CIPD package, not a git repository; list it in skipped_deps.";
        return false;
      }
      if (dep.commit.empty()) {
        error = where + "DEPS does not pin '" + path + "' by commit hash.";
        return false;
      }
      if (source->commit != dep.commit) {
        error = where + "DEPS pins '" + path + "' at " + dep.commit + ", the lock's '" + source->name + "' at " +
                source->commit + ".";
        return false;
      }
    } else if (!skipped.contains(path)) {
      error = where + "DEPS lists '" + path + "', which the lock neither pins in deps nor names in skipped_deps.";
      return false;
    }
  }
  for (const auto& [path, dep] : pinned) {
    if (!deps->contains(path)) {
      error = where + "deps lists '" + path + "', which DEPS no longer has.";
      return false;
    }
  }
  for (const auto& [path, dep] : skipped) {
    if (!deps->contains(path)) {
      error = where + "skipped_deps lists '" + path + "', which DEPS no longer has.";
      return false;
    }
  }
  return true;
}

bool SourceResolver::check_checkout(const SourcePin& pin, const fs::path& dir, std::string& error) {
  const ProcessResult head = git(dir, {"rev-parse", "HEAD"});
  if (!head.ok()) {
    error = git_failure(head, "Reading the checked-out commit of " + pin.name);
    return false;
  }
  if (trim(head.output) != pin.commit) {
    error = "The checkout of " + pin.name + " is at " + trim(head.output) + ", not the locked commit " + pin.commit + ".";
    return false;
  }
  if (!pin.tree.empty()) {
    const ProcessResult tree = git(dir, {"rev-parse", "HEAD^{tree}"});
    if (!tree.ok() || trim(tree.output) != pin.tree) {
      error = "The locked commit of " + pin.name + " does not have the tree the lock records. The lock is wrong.";
      return false;
    }
  }
  for (const SubmodulePin& sub : pin.submodules) {
    const ProcessResult entry = git(dir, {"ls-tree", "HEAD", "--", sub.path});
    // "160000 commit <sha>\t<path>"
    const std::string line = trim(entry.output);
    if (!entry.ok() || !line.starts_with("160000 commit ") || line.substr(14, 40) != sub.gitlink) {
      error = "The locked commit of " + pin.name + " does not record submodule '" + sub.path + "' at " + sub.gitlink +
              ". The lock is wrong.";
      return false;
    }
  }
  return true;
}

std::optional<ResolvedSource> SourceResolver::fetch(const SourcePin& pin, std::string& error) {
  const fs::path parent = cache_dir_ / pin.name;
  const fs::path target = parent / pin.commit;
  std::error_code ec;
  ResolvedSource out{pin.name, target, SourceOrigin::fetched, true, pin.commit, pin.commit};

  if (fs::exists(target / ".git", ec)) {
    std::string ignored;
    if (check_checkout(pin, target, ignored)) return out;
  }
  // Anything else under the target is a leftover from an interrupted fetch.
  fs::remove_all(target, ec);
  const fs::path tmp = parent / (".tmp-" + pin.commit);
  fs::remove_all(tmp, ec);
  fs::create_directories(parent, ec);
  if (ec) {
    error = "Could not create " + path_for_message(parent) + ": " + ec.message();
    return std::nullopt;
  }
  auto cleanup = [&tmp] {
    std::error_code ignored;
    fs::remove_all(tmp, ignored);
  };

  const std::u8string tmp_utf8 = tmp.u8string();
  const ProcessResult clone =
      git({}, {"clone", "--filter=blob:none", "--no-checkout", "-c", "core.autocrlf=false",
               // Deep dependency trees (Dawn's third_party) pass Windows' 260-character limit.
               "-c", "core.longpaths=true", "--", pin.repo,
               std::string(reinterpret_cast<const char*>(tmp_utf8.data()), tmp_utf8.size())});
  if (!clone.ok()) {
    cleanup();
    error = git_failure(clone, "Downloading " + pin.name + " from " + pin.repo);
    return std::nullopt;
  }
  const ProcessResult checkout = git(tmp, {"-c", "advice.detachedHead=false", "checkout", "--detach", pin.commit});
  if (!checkout.ok()) {
    cleanup();
    error = git_failure(checkout, "Checking out the locked commit " + pin.commit + " of " + pin.name);
    return std::nullopt;
  }
  if (!check_checkout(pin, tmp, error)) {
    cleanup();
    return std::nullopt;
  }
  fs::rename(tmp, target, ec);
  if (ec) {
    cleanup();
    error = "Could not move the checkout of " + pin.name + " into place: " + ec.message();
    return std::nullopt;
  }
  return out;
}

std::optional<ResolvedSource> SourceResolver::from_archive(const SourcePin& pin, const fs::path& dir,
                                                           std::string& error) {
  if (pin.tree.empty()) {
    error = "The lock does not record a tree hash for " + pin.name +
            ", so an archive cannot be verified. Use a git checkout instead.";
    return std::nullopt;
  }
  const TreeHashOptions options = tree_options(pin);
  auto matches = [&](const fs::path& candidate) -> std::optional<bool> {
    const auto tree = git_tree_id(candidate, options, error);
    if (!tree) return std::nullopt;
    return *tree == pin.tree;
  };

  auto result = matches(dir);
  if (!result) return std::nullopt;
  fs::path root = dir;
  if (!*result) {
    // GitHub archives wrap everything in one "<repo>-<commit>" folder; look inside it.
    std::error_code ec;
    fs::path only_child;
    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      ++count;
      only_child = entry.path();
    }
    if (!ec && count == 1 && fs::is_directory(only_child, ec)) {
      result = matches(only_child);
      if (!result) return std::nullopt;
      root = only_child;
    }
  }
  if (!*result) {
    error = "The archive for " + pin.name + " does not match the locked commit " + pin.commit +
            " (its files differ, for example in content, line endings or executable bits). "
            "Use a git checkout of that commit instead.";
    return std::nullopt;
  }
  return ResolvedSource{pin.name, root, SourceOrigin::archive, true, pin.commit, "tree:" + pin.tree};
}

std::optional<ResolvedSource> SourceResolver::from_override(const SourcePin& pin, const fs::path& dir,
                                                            std::string& error) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    error = "The override folder for " + pin.name + " (" + path_for_message(dir) + ") does not exist.";
    return std::nullopt;
  }
  ResolvedSource out{pin.name, dir, SourceOrigin::override_dir, false, pin.commit, {}};

  if (fs::exists(dir / ".git", ec)) {
    const ProcessResult head = git(dir, {"rev-parse", "HEAD"});
    const ProcessResult status = git(dir, {"status", "--porcelain"});
    if (head.ok() && status.ok()) {
      const std::string commit = trim(head.output);
      const bool clean = trim(status.output).empty();
      if (clean && commit == pin.commit) {
        out.pinned = true;
        out.actual = commit;
        return out;
      }
      if (clean) {
        out.actual = commit;  // a different commit, clean: the commit identifies it
        return out;
      }
      // Dirty: fingerprint the working tree too, so edits change the build key.
      const auto tree = git_tree_id(dir, tree_options(pin), error);
      if (!tree) return std::nullopt;
      out.actual = commit + "-dirty:tree:" + *tree;
      return out;
    }
    // Not usable as a git checkout (or git is missing): fall back to the tree hash.
  }
  const auto tree = git_tree_id(dir, tree_options(pin), error);
  if (!tree) return std::nullopt;
  out.pinned = !pin.tree.empty() && *tree == pin.tree;
  out.actual = out.pinned ? pin.commit : "tree:" + *tree;
  return out;
}

}  // namespace mpt::asset_import
