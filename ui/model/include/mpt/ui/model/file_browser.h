#pragma once

// The in-UI file list for controller users (ADR 0010 §5): drives, folders and dump
// files, because the native file dialog is not controller-navigable. Mouse users get
// the native dialog and drag-and-drop instead; both end in the same ImportFlow.

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mpt::ui {

struct FileEntry {
  enum class Kind { up, root, folder, file };
  Kind kind = Kind::folder;
  std::string label;  // UTF-8
  std::filesystem::path path;
};

class FileBrowser {
 public:
  using RootsProvider = std::function<std::vector<std::filesystem::path>()>;

  // `roots` lists the top level (drive letters on Windows, / and the home folder
  // elsewhere). Tests pass their own.
  explicit FileBrowser(RootsProvider roots = default_roots);

  // Opens `folder`, or the top level when it is empty or cannot be listed.
  void open(const std::filesystem::path& folder);
  void open_roots();
  // Enters a folder, goes up, or returns the chosen file.
  std::optional<std::filesystem::path> activate(std::size_t index);
  void up();

  const std::vector<FileEntry>& entries() const { return entries_; }
  const std::filesystem::path& folder() const { return folder_; }  // empty at the top level
  std::string location() const;                                      // UTF-8, for the heading
  const std::string& error() const { return error_; }

  // Files the list shows: plain disc and cartridge images, plus the compressed
  // containers, so choosing one gives asset-import's specific message (E11).
  static bool is_dump_candidate(const std::filesystem::path& path);
  static std::vector<std::filesystem::path> default_roots();

 private:
  RootsProvider roots_;
  std::filesystem::path folder_;
  std::vector<FileEntry> entries_;
  std::string error_;
};

std::string to_utf8(const std::filesystem::path& path);
std::filesystem::path from_utf8(const std::string& text);

}  // namespace mpt::ui
