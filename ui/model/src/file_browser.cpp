#include "mpt/ui/model/file_browser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string_view>
#include <system_error>

namespace mpt::ui {
namespace fs = std::filesystem;

std::string to_utf8(const fs::path& path) {
  const std::u8string s = path.u8string();
  return std::string(s.begin(), s.end());
}

fs::path from_utf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool label_less(const FileEntry& a, const FileEntry& b) { return lower(a.label) < lower(b.label); }

}  // namespace

bool FileBrowser::is_dump_candidate(const fs::path& path) {
  static constexpr std::array<std::string_view, 11> kExtensions = {
      ".iso", ".gcm", ".z64", ".n64", ".v64", ".rvz", ".wia", ".gcz", ".wbfs", ".ciso", ".tgc"};
  const std::string ext = lower(to_utf8(path.extension()));
  return std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end();
}

std::vector<fs::path> FileBrowser::default_roots() {
  std::vector<fs::path> roots;
#ifdef _WIN32
  for (char letter = 'A'; letter <= 'Z'; ++letter) {
    const fs::path root = std::string(1, letter) + ":\\";
    std::error_code ec;
    if (fs::is_directory(root, ec)) roots.push_back(root);
  }
#else
  roots.push_back("/");
  if (const char* home = std::getenv("HOME"); home && *home) roots.push_back(home);
#endif
  return roots;
}

FileBrowser::FileBrowser(RootsProvider roots) : roots_(std::move(roots)) { open_roots(); }

void FileBrowser::open_roots() {
  folder_.clear();
  entries_.clear();
  for (const fs::path& root : roots_ ? roots_() : std::vector<fs::path>{}) {
    entries_.push_back(FileEntry{FileEntry::Kind::root, to_utf8(root), root});
  }
}

void FileBrowser::open(const fs::path& folder) {
  error_.clear();
  if (folder.empty()) {
    open_roots();
    return;
  }
  std::vector<FileEntry> folders;
  std::vector<FileEntry> files;
  std::error_code ec;
  fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    error_ = "That folder could not be opened.";
    return;  // stay where we were
  }
  for (const fs::directory_iterator end; it != end; it.increment(ec)) {
    if (ec) break;
    const fs::directory_entry& entry = *it;
    std::error_code type_ec;
    const std::string name = to_utf8(entry.path().filename());
    if (name.empty() || name.front() == '.') continue;  // hidden on Unix; never a dump folder we need
    if (entry.is_directory(type_ec)) {
      folders.push_back(FileEntry{FileEntry::Kind::folder, name, entry.path()});
    } else if (entry.is_regular_file(type_ec) && is_dump_candidate(entry.path())) {
      files.push_back(FileEntry{FileEntry::Kind::file, name, entry.path()});
    }
  }
  std::sort(folders.begin(), folders.end(), label_less);
  std::sort(files.begin(), files.end(), label_less);
  folder_ = folder;
  entries_.clear();
  entries_.push_back(FileEntry{FileEntry::Kind::up, "Up one level", folder.parent_path()});
  entries_.insert(entries_.end(), folders.begin(), folders.end());
  entries_.insert(entries_.end(), files.begin(), files.end());
}

void FileBrowser::up() {
  if (folder_.empty()) return;
  const fs::path parent = folder_.parent_path();
  // At a drive or filesystem root the parent is itself: go to the top level.
  if (parent.empty() || parent == folder_) {
    open_roots();
  } else {
    open(parent);
  }
}

std::optional<fs::path> FileBrowser::activate(std::size_t index) {
  if (index >= entries_.size()) return std::nullopt;
  const FileEntry entry = entries_[index];
  switch (entry.kind) {
    case FileEntry::Kind::up: up(); break;
    case FileEntry::Kind::root:
    case FileEntry::Kind::folder: open(entry.path); break;
    case FileEntry::Kind::file: return entry.path;
  }
  return std::nullopt;
}

std::string FileBrowser::location() const { return folder_.empty() ? std::string("This computer") : to_utf8(folder_); }

}  // namespace mpt::ui
