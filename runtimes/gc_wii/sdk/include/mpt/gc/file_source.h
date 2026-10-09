// Host-provided disc file system for the DVD layer (ADR 0007 §4).
//
// The runtime hands the SDK the files asset-import extracted into
// VerifiedDump::build_dir. Entry numbers (DVDConvertPathToEntrynum) are the
// entries' positions in FST order, so the order must come from the disc's FST,
// never from host directory iteration.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mpt::gc {

// One FST entry. Entry 0 is the root directory (empty path). Paths use '/' and
// have no leading slash. Every directory comes before its contents, and a
// directory's contents follow it contiguously (FST preorder).
struct FileSourceEntry {
  std::string path;
  bool is_dir = false;
  std::uint64_t size = 0;  // files only
};

class IFileSource {
 public:
  virtual ~IFileSource() = default;
  virtual std::span<const FileSourceEntry> entries() const = 0;
  // Reads dst.size() bytes of file `entry` at `offset`. The range is within the
  // file. Returns false on a host I/O error, which the SDK treats as fatal.
  virtual bool read(std::uint32_t entry, std::uint64_t offset, std::span<std::byte> dst) const = 0;
};

// Puts paths into FST preorder: within each directory, entries sorted by
// case-insensitive ASCII name, then by exact bytes. Used for synthetic sources
// and as the fallback when no FST listing is available.
std::vector<FileSourceEntry> fst_order(std::vector<std::pair<std::string, std::uint64_t>> files);

// In-memory files, for tests (synthetic data only).
class MemoryFileSource final : public IFileSource {
 public:
  explicit MemoryFileSource(std::vector<std::pair<std::string, std::vector<std::byte>>> files);
  std::span<const FileSourceEntry> entries() const override { return entries_; }
  bool read(std::uint32_t entry, std::uint64_t offset, std::span<std::byte> dst) const override;

 private:
  std::vector<FileSourceEntry> entries_;
  std::vector<std::vector<std::byte>> data_;  // indexed like entries_
};

// Parses a GameCube FST (the big-endian table asset-import extracts to
// sys/fst.bin) into FST-order entries. Rejects malformed tables and unsafe names
// ("", ".", "..", or containing '/', '\' or control bytes). On failure returns
// nullopt and sets `error`.
std::optional<std::vector<FileSourceEntry>> parse_fst(std::span<const std::byte> fst, std::string& error);

// An extracted disc in asset-import's layout (P1-09): sys/fst.bin plus
// files/<FST paths>, inside VerifiedDump::build_dir. Entry order and sizes come
// from sys/fst.bin, never from directory iteration. Each file must exist with the
// size the FST gives. Recently used files stay open between reads.
class DirectoryFileSource final : public IFileSource {
 public:
  static std::unique_ptr<DirectoryFileSource> open(const std::filesystem::path& root, std::string& error);
  ~DirectoryFileSource() override;
  std::span<const FileSourceEntry> entries() const override { return entries_; }
  bool read(std::uint32_t entry, std::uint64_t offset, std::span<std::byte> dst) const override;

 private:
  DirectoryFileSource(std::filesystem::path files_dir, std::vector<FileSourceEntry> entries);
  std::filesystem::path pathOf(std::uint32_t entry) const;

  struct Handle;
  static constexpr std::size_t kMaxOpenFiles = 16;
  std::filesystem::path files_dir_;
  std::vector<FileSourceEntry> entries_;
  mutable std::vector<std::unique_ptr<Handle>> open_;  // most recently used last
};

}  // namespace mpt::gc
