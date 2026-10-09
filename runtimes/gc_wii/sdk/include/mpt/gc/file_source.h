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

// Files under a host directory (the extracted disc in build_dir). `entries` is
// the FST listing in disc order, normally written by asset-import's extractor.
class DirectoryFileSource final : public IFileSource {
 public:
  DirectoryFileSource(std::filesystem::path root, std::vector<FileSourceEntry> entries);
  std::span<const FileSourceEntry> entries() const override { return entries_; }
  bool read(std::uint32_t entry, std::uint64_t offset, std::span<std::byte> dst) const override;

 private:
  std::filesystem::path root_;
  std::vector<FileSourceEntry> entries_;
};

}  // namespace mpt::gc
