#include "mpt/gc/file_source.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
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

namespace {

std::uint32_t be32(std::span<const std::byte> b, std::size_t at) {
  return (std::to_integer<std::uint32_t>(b[at]) << 24) | (std::to_integer<std::uint32_t>(b[at + 1]) << 16) |
         (std::to_integer<std::uint32_t>(b[at + 2]) << 8) | std::to_integer<std::uint32_t>(b[at + 3]);
}

std::string nameProblem(const std::string& name) {
  if (name.empty()) return "an empty name";
  if (name.size() > 255) return "a name longer than 255 bytes";
  if (name == "." || name == "..") return "the name \"" + name + "\"";
  for (char c : name) {
    const auto u = static_cast<unsigned char>(c);
    if (c == '/' || c == '\\') return "a path separator in \"" + name + "\"";
    if (u < 0x20 || u == 0x7F) return "a control character in a name";
  }
  return {};
}

}  // namespace

std::optional<std::vector<FileSourceEntry>> parse_fst(std::span<const std::byte> fst, std::string& error) {
  constexpr std::size_t kEntry = 12;
  if (fst.size() < kEntry) {
    error = "FST too small";
    return std::nullopt;
  }
  if (std::to_integer<std::uint8_t>(fst[0]) != 1) {
    error = "FST root is not a directory";
    return std::nullopt;
  }
  const std::uint32_t count = be32(fst, 8);
  if (count == 0 || std::uint64_t{count} * kEntry > fst.size()) {
    error = "FST entry count does not fit the table";
    return std::nullopt;
  }
  const std::size_t strings = std::size_t{count} * kEntry;
  std::vector<FileSourceEntry> out;
  out.reserve(count);
  out.push_back({"", true, 0});
  // Open directories: (one past their last entry, path).
  std::vector<std::pair<std::uint32_t, std::string>> dirs{{count, ""}};
  for (std::uint32_t i = 1; i < count; ++i) {
    while (dirs.size() > 1 && i >= dirs.back().first) dirs.pop_back();
    const std::size_t at = std::size_t{i} * kEntry;
    const auto flags = std::to_integer<std::uint8_t>(fst[at]);
    const std::uint32_t name_off = be32(fst, at) & 0x00FF'FFFFu;
    const std::uint32_t next_or_size = be32(fst, at + 8);
    if (flags > 1) {
      error = "FST entry " + std::to_string(i) + " has unknown flags";
      return std::nullopt;
    }
    if (strings + name_off >= fst.size()) {
      error = "FST entry " + std::to_string(i) + " has a name outside the string table";
      return std::nullopt;
    }
    std::string name;
    for (std::size_t k = strings + name_off; k < fst.size() && fst[k] != std::byte{0} && name.size() <= 255; ++k)
      name.push_back(static_cast<char>(std::to_integer<unsigned char>(fst[k])));
    if (const std::string problem = nameProblem(name); !problem.empty()) {
      error = "FST entry " + std::to_string(i) + " is unsafe: " + problem;
      return std::nullopt;
    }
    const std::string path = dirs.back().second.empty() ? name : dirs.back().second + "/" + name;
    if (flags == 1) {
      if (next_or_size <= i || next_or_size > dirs.back().first) {
        error = "FST directory " + std::to_string(i) + " has a bad extent";
        return std::nullopt;
      }
      out.push_back({path, true, 0});
      dirs.emplace_back(next_or_size, path);
    } else {
      out.push_back({path, false, next_or_size});
    }
  }
  return out;
}

struct DirectoryFileSource::Handle {
  std::uint32_t entry = 0;
  std::ifstream stream;
};

DirectoryFileSource::DirectoryFileSource(std::filesystem::path files_dir, std::vector<FileSourceEntry> entries)
    : files_dir_(std::move(files_dir)), entries_(std::move(entries)) {}

DirectoryFileSource::~DirectoryFileSource() = default;

std::filesystem::path DirectoryFileSource::pathOf(std::uint32_t entry) const {
  const std::string& rel = entries_[entry].path;
  return files_dir_ / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(rel.data()), rel.size()));
}

std::unique_ptr<DirectoryFileSource> DirectoryFileSource::open(const std::filesystem::path& root, std::string& error) {
  namespace fs = std::filesystem;
  std::ifstream f(root / "sys" / "fst.bin", std::ios::binary);
  if (!f) {
    error = "no sys/fst.bin in the extracted disc";
    return nullptr;
  }
  std::vector<char> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::vector<std::byte> fst(raw.size());
  if (!raw.empty()) std::memcpy(fst.data(), raw.data(), raw.size());
  auto entries = parse_fst(fst, error);
  if (!entries) return nullptr;
  std::unique_ptr<DirectoryFileSource> src(new DirectoryFileSource(root / "files", std::move(*entries)));
  for (std::uint32_t i = 1; i < src->entries_.size(); ++i) {
    const FileSourceEntry& e = src->entries_[i];
    std::error_code ec;
    const fs::path p = src->pathOf(i);
    if (e.is_dir) {
      if (!fs::is_directory(p, ec)) {
        error = "missing directory files/" + e.path;
        return nullptr;
      }
    } else if (!fs::is_regular_file(p, ec) || fs::file_size(p, ec) != e.size || ec) {
      error = "missing file or wrong size: files/" + e.path;
      return nullptr;
    }
  }
  return src;
}

bool DirectoryFileSource::read(std::uint32_t entry, std::uint64_t offset, std::span<std::byte> dst) const {
  if (entry >= entries_.size() || entries_[entry].is_dir) return false;
  if (offset > entries_[entry].size || dst.size() > entries_[entry].size - offset) return false;
  // Reuse an open handle if there is one; keep the most recently used last.
  std::unique_ptr<Handle> h;
  for (std::size_t i = 0; i < open_.size(); ++i) {
    if (open_[i]->entry == entry) {
      h = std::move(open_[i]);
      open_.erase(open_.begin() + static_cast<std::ptrdiff_t>(i));
      break;
    }
  }
  if (!h) {
    h = std::make_unique<Handle>();
    h->entry = entry;
    h->stream.open(pathOf(entry), std::ios::binary);
    if (!h->stream) return false;
    if (open_.size() >= kMaxOpenFiles) open_.erase(open_.begin());
  }
  h->stream.clear();
  h->stream.seekg(static_cast<std::streamoff>(offset));
  h->stream.read(reinterpret_cast<char*>(dst.data()), static_cast<std::streamsize>(dst.size()));
  const bool ok = static_cast<std::size_t>(h->stream.gcount()) == dst.size();
  open_.push_back(std::move(h));
  return ok;
}

}  // namespace mpt::gc
