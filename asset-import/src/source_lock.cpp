#include "mpt/asset_import/source_lock.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "mpt/asset_import/byte_source.h"

namespace mpt::asset_import {
namespace {

constexpr int kLockVersion = 2;  // newest supported; 1 is still read

bool is_source_name(std::string_view text) {
  if (text.empty() || text.size() > 64) return false;
  for (char c : text) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}

// A relative, '/'-separated path with no empty, "." or ".." components.
bool is_relative_tree_path(std::string_view path) {
  if (path.empty() || path.front() == '/' || path.back() == '/') return false;
  if (path.find('\\') != std::string_view::npos || path.find(':') != std::string_view::npos) return false;
  std::size_t start = 0;
  while (true) {
    const std::size_t slash = path.find('/', start);
    const std::string_view part = path.substr(start, slash - start);
    if (part.empty() || part == "." || part == ".." || part == ".git") return false;
    if (slash == std::string_view::npos) return true;
    start = slash + 1;
  }
}

// The first key of `object` not in `allowed`, or an empty string.
std::string unknown_field(const nlohmann::json& object, std::initializer_list<std::string_view> allowed) {
  for (const auto& [key, value] : object.items()) {
    if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) return key.empty() ? "(empty)" : key;
  }
  return {};
}

std::string unknown_field(const nlohmann::json& object, const std::vector<std::string_view>& allowed) {
  for (const auto& [key, value] : object.items()) {
    if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) return key.empty() ? "(empty)" : key;
  }
  return {};
}

std::string string_field(const nlohmann::json& object, const char* key) {
  const auto it = object.find(key);
  return (it != object.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

}  // namespace

bool is_git_object_id(std::string_view text) {
  if (text.size() != 40) return false;
  for (char c : text) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

std::optional<SourceLock> SourceLock::parse(std::string_view json_text, std::string& error,
                                            const LockParseOptions& options) {
  const nlohmann::json root = nlohmann::json::parse(json_text, nullptr, /*allow_exceptions=*/false);
  auto fail = [&error](std::string message) {
    error = "Source lock: " + std::move(message);
    return std::nullopt;
  };
  if (root.is_discarded() || !root.is_object()) return fail("not a JSON object.");
  const int version = root.value("lock_version", 0);
  if (version < 1 || version > kLockVersion) {
    return fail("unsupported lock_version (expected 1 to " + std::to_string(kLockVersion) + ").");
  }
  // Unknown fields are errors, so a misspelt or newer field is never silently ignored.
  if (const std::string bad = unknown_field(root, {"lock_version", "sources"}); !bad.empty()) {
    return fail("unknown field '" + bad + "'.");
  }
  const auto sources = root.find("sources");
  if (sources == root.end() || !sources->is_array()) return fail("needs a \"sources\" array.");

  std::vector<std::string_view> entry_fields{"name", "repo", "commit", "tree", "license", "modes", "submodules"};
  if (version >= 2) {
    entry_fields.push_back("deps");
    entry_fields.push_back("skipped_deps");
  }

  SourceLock lock;
  for (std::size_t i = 0; i < sources->size(); ++i) {
    const nlohmann::json& entry = (*sources)[i];
    const std::string where = "sources[" + std::to_string(i) + "]";
    if (!entry.is_object()) return fail(where + " is not an object.");
    if (const std::string bad = unknown_field(entry, entry_fields); !bad.empty()) {
      return fail(where + " has an unknown field '" + bad + "'" +
                  (version < 2 && (bad == "deps" || bad == "skipped_deps") ? " (it needs lock_version 2)." : "."));
    }

    SourcePin pin;
    pin.name = string_field(entry, "name");
    pin.repo = string_field(entry, "repo");
    pin.commit = string_field(entry, "commit");
    pin.tree = string_field(entry, "tree");
    pin.license = string_field(entry, "license");
    if (!is_source_name(pin.name)) return fail(where + ".name must be 1-64 characters of [a-z0-9_-].");
    if (lock.find(pin.name)) return fail(where + " duplicates the name '" + pin.name + "'.");
    const bool https = pin.repo.starts_with("https://") && pin.repo.size() > 8;
    const bool file = options.allow_file_urls && pin.repo.starts_with("file://") && pin.repo.size() > 7;
    if (!https && !file) return fail(where + ".repo must be an https:// URL.");
    if (!is_git_object_id(pin.commit)) {
      return fail(where + ".commit must be a full 40-character lowercase commit hash (never a tag or branch).");
    }
    if (!pin.tree.empty() && !is_git_object_id(pin.tree)) return fail(where + ".tree must be 40 lowercase hex.");
    if (pin.license.empty()) return fail(where + ".license must name an SPDX license.");

    if (const auto modes = entry.find("modes"); modes != entry.end()) {
      if (!modes->is_object()) return fail(where + ".modes must be an object.");
      for (const auto& [path, mode] : modes->items()) {
        if (!is_relative_tree_path(path)) return fail(where + ".modes has an invalid path '" + path + "'.");
        if (!mode.is_string() || (mode != "100755" && mode != "120000" && mode != "100644")) {
          return fail(where + ".modes['" + path + "'] must be \"100644\", \"100755\" or \"120000\".");
        }
        pin.modes[path] = mode.get<std::string>();
      }
    }
    if (const auto subs = entry.find("submodules"); subs != entry.end()) {
      if (!subs->is_array()) return fail(where + ".submodules must be an array.");
      std::set<std::string> seen;
      for (std::size_t j = 0; j < subs->size(); ++j) {
        const nlohmann::json& sub = (*subs)[j];
        const std::string sub_where = where + ".submodules[" + std::to_string(j) + "]";
        if (!sub.is_object()) return fail(sub_where + " is not an object.");
        if (const std::string bad = unknown_field(sub, {"path", "source", "gitlink", "reason"}); !bad.empty()) {
          return fail(sub_where + " has an unknown field '" + bad + "'.");
        }
        SubmodulePin s{string_field(sub, "path"), string_field(sub, "source"), string_field(sub, "gitlink"),
                       string_field(sub, "reason")};
        if (!is_relative_tree_path(s.path)) return fail(sub_where + ".path is not a valid relative path.");
        if (!seen.insert(s.path).second) return fail(sub_where + " repeats the path '" + s.path + "'.");
        if (s.source == pin.name) return fail(sub_where + ".source cannot be the entry itself.");
        if (!s.gitlink.empty() && !is_git_object_id(s.gitlink)) return fail(sub_where + ".gitlink must be 40 lowercase hex.");
        if (pin.modes.contains(s.path)) return fail(sub_where + ".path also appears in modes.");
        pin.submodules.push_back(std::move(s));
      }
    }

    // lock_version 2: dependencies pinned by the source's own lock file (Dawn's DEPS).
    std::set<std::string> dep_paths;
    if (const auto deps = entry.find("deps"); deps != entry.end()) {
      if (!deps->is_array()) return fail(where + ".deps must be an array.");
      for (std::size_t j = 0; j < deps->size(); ++j) {
        const nlohmann::json& dep = (*deps)[j];
        const std::string dep_where = where + ".deps[" + std::to_string(j) + "]";
        if (!dep.is_object()) return fail(dep_where + " is not an object.");
        if (const std::string bad = unknown_field(dep, {"path", "source"}); !bad.empty()) {
          return fail(dep_where + " has an unknown field '" + bad + "'.");
        }
        DepPin d{string_field(dep, "path"), string_field(dep, "source")};
        if (!is_relative_tree_path(d.path)) return fail(dep_where + ".path is not a valid relative path.");
        if (!dep_paths.insert(d.path).second) return fail(dep_where + " repeats the path '" + d.path + "'.");
        if (d.source == pin.name) return fail(dep_where + ".source cannot be the entry itself.");
        pin.deps.push_back(std::move(d));
      }
    }
    if (const auto skipped = entry.find("skipped_deps"); skipped != entry.end()) {
      if (!skipped->is_array()) return fail(where + ".skipped_deps must be an array.");
      for (std::size_t j = 0; j < skipped->size(); ++j) {
        const nlohmann::json& dep = (*skipped)[j];
        const std::string dep_where = where + ".skipped_deps[" + std::to_string(j) + "]";
        if (!dep.is_object()) return fail(dep_where + " is not an object.");
        if (const std::string bad = unknown_field(dep, {"path", "reason"}); !bad.empty()) {
          return fail(dep_where + " has an unknown field '" + bad + "'.");
        }
        SkippedDep d{string_field(dep, "path"), string_field(dep, "reason")};
        if (!is_relative_tree_path(d.path)) return fail(dep_where + ".path is not a valid relative path.");
        if (!dep_paths.insert(d.path).second) {
          return fail(dep_where + " repeats the path '" + d.path + "' (a dependency is pinned or skipped, not both).");
        }
        if (d.reason.empty()) return fail(dep_where + " must give a reason for skipping '" + d.path + "'.");
        pin.skipped_deps.push_back(std::move(d));
      }
    }
    lock.pins_.push_back(std::move(pin));
  }

  // Second pass: references point at entries that exist, and differing gitlinks say why.
  for (const SourcePin& pin : lock.pins_) {
    for (const DepPin& dep : pin.deps) {
      if (!lock.find(dep.source)) {
        return fail(pin.name + ": dependency '" + dep.path + "' names an unknown source '" + dep.source + "'.");
      }
    }
  }
  for (SourcePin& pin : lock.pins_) {
    for (SubmodulePin& sub : pin.submodules) {
      const SourcePin* target = lock.find(sub.source);
      if (!target) return fail(pin.name + ": submodule '" + sub.path + "' names an unknown source '" + sub.source + "'.");
      if (sub.gitlink.empty()) sub.gitlink = target->commit;
      if (sub.gitlink != target->commit && sub.reason.empty()) {
        return fail(pin.name + ": submodule '" + sub.path + "' is pinned to a different commit than the parent records; "
                    "give a reason.");
      }
    }
  }
  return lock;
}

std::optional<SourceLock> SourceLock::load(const std::filesystem::path& path, std::string& error,
                                           const LockParseOptions& options) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "Could not open " + path_for_message(path);
    return std::nullopt;
  }
  std::ostringstream text;
  text << file.rdbuf();
  return parse(text.str(), error, options);
}

const SourcePin* SourceLock::find(std::string_view name) const {
  for (const SourcePin& pin : pins_) {
    if (pin.name == name) return &pin;
  }
  return nullptr;
}

}  // namespace mpt::asset_import
