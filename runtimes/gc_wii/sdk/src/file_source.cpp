#include "mpt/gc/file_source.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>

namespace mpt::gc {

namespace {

char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool fstLess(const std::string& a, const std::string& b) {
  const std::size_t n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i) {
    const char x = lowerAscii(a[i]), y = lowerAscii(b[i]);
    if (x != y) return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
  }
  if (a.size() != b.size()) return a.size() < b.size();
  return a < b;
}

struct Node {
  std::map<std::string, Node, bool (*)(const std::string&, const std::string&)> children{&fstLess};
  bool is_dir = true;
  std::uint64_t size = 0;
  int index = -1;  // source file index, files only
};

void emit(const Node& n, const std::string& path, std::vector<FileSourceEntry>& out, std::vector<int>& order) {
  for (const auto& [name, child] : n.children) {
    const std::string p = path.empty() ? name : path + "/" + name;
    out.push_back({p, child.is_dir, child.size});
    order.push_back(child.index);
    if (child.is_dir) emit(child, p, out, order);
  }
}

std::vector<FileSourceEntry> buildOrder(const std::vector<std::pair<std::string, std::uint64_t>>& files,
                                        std::vector<int>& order) {
  Node root;
  for (std::size_t i = 0; i < files.size(); ++i) {
    Node* cur = &root;
    const std::string& path = files[i].first;
    std::size_t start = 0;
    while (start <= path.size()) {
      std::size_t slash = path.find('/', start);
      if (slash == std::string::npos) slash = path.size();
      const std::string part = path.substr(start, slash - start);
      if (!part.empty()) {
        Node& child = cur->children[part];
        if (slash == path.size()) {
          child.is_dir = false;
          child.size = files[i].second;
          child.index = static_cast<int>(i);
        }
        cur = &child;
      }
      start = slash + 1;
    }
  }
  std::vector<FileSourceEntry> out{{"", true, 0}};
  order.assign(1, -1);
  emit(root, "", out, order);
  return out;
}

}  // namespace

std::vector<FileSourceEntry> fst_order(std::vector<std::pair<std::string, std::uint64_t>> files) {
  std::vector<int> order;
  return buildOrder(files, order);
}

MemoryFileSource::MemoryFileSource(std::vector<std::pair<std::string, std::vector<std::byte>>> files) {
  std::vector<std::pair<std::string, std::uint64_t>> sizes;
  for (const auto& [path, bytes] : files) sizes.emplace_back(path, bytes.size());
  std::vector<int> order;
  entries_ = buildOrder(sizes, order);
  data_.resize(entries_.size());
  for (std::size_t i = 0; i < entries_.size(); ++i)
    if (order[i] >= 0) data_[i] = std::move(files[static_cast<std::size_t>(order[i])].second);
}

bool MemoryFileSource::read(std::uint32_t entry, std::uint64_t offset, std::span<std::byte> dst) const {
  if (entry >= data_.size() || entries_[entry].is_dir) return false;
  const auto& d = data_[entry];
  if (offset > d.size() || dst.size() > d.size() - offset) return false;
  if (!dst.empty()) std::memcpy(dst.data(), d.data() + offset, dst.size());
  return true;
}

DirectoryFileSource::DirectoryFileSource(std::filesystem::path root, std::vector<FileSourceEntry> entries)
    : root_(std::move(root)), entries_(std::move(entries)) {}

bool DirectoryFileSource::read(std::uint32_t entry, std::uint64_t offset, std::span<std::byte> dst) const {
  if (entry >= entries_.size() || entries_[entry].is_dir) return false;
  const std::string& rel = entries_[entry].path;
  const std::u8string rel8(reinterpret_cast<const char8_t*>(rel.data()), rel.size());
  std::ifstream f(root_ / std::filesystem::path(rel8), std::ios::binary);
  if (!f) return false;
  f.seekg(static_cast<std::streamoff>(offset));
  f.read(reinterpret_cast<char*>(dst.data()), static_cast<std::streamsize>(dst.size()));
  return static_cast<std::size_t>(f.gcount()) == dst.size();
}

}  // namespace mpt::gc
