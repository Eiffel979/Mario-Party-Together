#include "mpt/asset_import/git_tree.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <vector>

#include "mpt/asset_import/byte_source.h"
#include "mpt/asset_import/sha1.h"

namespace mpt::asset_import {
namespace {

namespace fs = std::filesystem;
using ObjectId = std::array<std::uint8_t, 20>;

struct Node {
  std::string mode;  // empty for a tree
  ObjectId id{};
  std::map<std::string, Node> children;
};

std::string utf8(const fs::path& path) {
  const std::u8string text = path.generic_u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

ObjectId parse_id(const std::string& hex) {
  ObjectId id{};
  auto nibble = [](char c) { return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
  for (std::size_t i = 0; i < id.size(); ++i) {
    id[i] = static_cast<std::uint8_t>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]));
  }
  return id;
}

ObjectId hash_object(std::string_view type, std::span<const std::byte> content) {
  Sha1 sha;
  sha.update(std::string(type) + " " + std::to_string(content.size()));
  sha.update(std::string_view("\0", 1));
  sha.update(content);
  return sha.finish();
}

bool hash_file(const fs::path& file, ObjectId& out, std::string& error) {
  std::error_code ec;
  const std::uintmax_t size = fs::file_size(file, ec);
  std::ifstream in(file, std::ios::binary);
  if (ec || !in) {
    error = "Could not read " + path_for_message(file);
    return false;
  }
  Sha1 sha;
  sha.update("blob " + std::to_string(size));
  sha.update(std::string_view("\0", 1));
  std::vector<char> chunk(1 << 16);
  std::uintmax_t total = 0;
  while (in) {
    in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    const auto n = static_cast<std::size_t>(in.gcount());
    sha.update(std::as_bytes(std::span(chunk.data(), n)));
    total += n;
  }
  if (total != size) {
    error = "The file " + path_for_message(file) + " changed while it was being read.";
    return false;
  }
  out = sha.finish();
  return true;
}

// Inserts a leaf at a '/'-separated path, creating the trees on the way.
bool insert(Node& root, const std::string& path, std::string mode, const ObjectId& id, std::string& error) {
  Node* node = &root;
  std::size_t start = 0;
  while (true) {
    const std::size_t slash = path.find('/', start);
    Node& child = node->children[path.substr(start, slash - start)];
    if (slash == std::string::npos) {
      if (!child.children.empty()) {
        error = "'" + path + "' is both a folder and a file or submodule.";
        return false;
      }
      child.mode = std::move(mode);
      child.id = id;
      return true;
    }
    if (!child.mode.empty()) {
      error = "'" + path + "' is inside a file or submodule.";
      return false;
    }
    node = &child;
    start = slash + 1;
  }
}

bool walk(const fs::path& dir, const std::string& prefix, const TreeHashOptions& options, Node& root,
          std::string& error) {
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path& path = it->path();
    const std::string name = utf8(path.filename());
    if (prefix.empty() && name == ".git") continue;
    const std::string rel = prefix.empty() ? name : prefix + "/" + name;
    if (options.gitlinks.contains(rel)) continue;  // added separately, contents ignored

    const fs::file_status status = it->symlink_status(ec);
    if (ec) break;
    if (fs::is_symlink(status)) {
      const fs::path target = fs::read_symlink(path, ec);
      if (ec) break;
      const std::string text = utf8(target);
      if (!insert(root, rel, "120000", hash_object("blob", std::as_bytes(std::span(text.data(), text.size()))),
                  error)) {
        return false;
      }
    } else if (fs::is_directory(status)) {
      if (!walk(path, rel, options, root, error)) return false;
    } else if (fs::is_regular_file(status)) {
      ObjectId id;
      if (!hash_file(path, id, error)) return false;
      const auto mode = options.modes.find(rel);
      if (!insert(root, rel, mode != options.modes.end() ? mode->second : "100644", id, error)) return false;
    } else {
      error = "'" + rel + "' is not a regular file, folder or link.";
      return false;
    }
  }
  if (ec) {
    error = "Could not list " + path_for_message(dir) + ": " + ec.message();
    return false;
  }
  return true;
}

ObjectId hash_tree(const Node& node) {
  // Git orders entries by name, comparing a tree's name as if it ended in '/'.
  std::vector<std::pair<std::string, const Node*>> entries;
  for (const auto& [name, child] : node.children) {
    if (child.mode.empty() && child.children.empty()) continue;  // an empty folder is not stored
    entries.emplace_back(child.mode.empty() ? name + "/" : name, &child);
  }
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

  std::string content;
  for (const auto& [key, child] : entries) {
    const bool tree = child->mode.empty();
    const std::string name = tree ? key.substr(0, key.size() - 1) : key;
    const ObjectId id = tree ? hash_tree(*child) : child->id;
    content += tree ? "40000" : child->mode;
    content += ' ';
    content += name;
    content.push_back('\0');
    content.append(reinterpret_cast<const char*>(id.data()), id.size());
  }
  return hash_object("tree", std::as_bytes(std::span(content.data(), content.size())));
}

// A folder that holds only empty folders hashes like an absent one.
bool has_leaves(const Node& node) {
  if (!node.mode.empty()) return true;
  return std::any_of(node.children.begin(), node.children.end(), [](const auto& c) { return has_leaves(c.second); });
}

void prune(Node& node) {
  for (auto it = node.children.begin(); it != node.children.end();) {
    if (!has_leaves(it->second)) {
      it = node.children.erase(it);
    } else {
      prune(it->second);
      ++it;
    }
  }
}

}  // namespace

std::string git_blob_id(std::span<const std::byte> content) {
  const ObjectId id = hash_object("blob", content);
  return to_hex(id);
}

std::optional<std::string> git_tree_id(const fs::path& dir, const TreeHashOptions& options, std::string& error) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    error = path_for_message(dir) + " is not a folder.";
    return std::nullopt;
  }
  Node root;
  if (!walk(dir, {}, options, root, error)) return std::nullopt;
  for (const auto& [path, commit] : options.gitlinks) {
    if (commit.size() != 40) {
      error = "The submodule '" + path + "' has an invalid commit.";
      return std::nullopt;
    }
    if (!insert(root, path, "160000", parse_id(commit), error)) return std::nullopt;
  }
  prune(root);
  const ObjectId id = hash_tree(root);
  return to_hex(id);
}

}  // namespace mpt::asset_import
