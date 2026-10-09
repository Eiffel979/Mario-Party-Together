#include "mpt/asset_import/source_lock.h"

#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "mpt/asset_import/byte_source.h"

namespace mpt::asset_import {
namespace {

constexpr int kLockVersion = 1;

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
  if (root.value("lock_version", 0) != kLockVersion) {
    return fail("unsupported lock_version (expected " + std::to_string(kLockVersion) + ").");
  }
  const auto sources = root.find("sources");
  if (sources == root.end() || !sources->is_array()) return fail("needs a \"sources\" array.");

  SourceLock lock;
  for (std::size_t i = 0; i < sources->size(); ++i) {
    const nlohmann::json& entry = (*sources)[i];
    const std::string where = "sources[" + std::to_string(i) + "]";
    if (!entry.is_object()) return fail(where + " is not an object.");

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
    lock.pins_.push_back(std::move(pin));
  }

  // Second pass: submodules refer to entries that exist, and differing gitlinks say why.
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
