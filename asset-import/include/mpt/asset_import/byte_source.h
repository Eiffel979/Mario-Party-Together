#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>

namespace mpt::asset_import {

// Random-access, read-only view of a dump. Readers take this instead of a path so
// they can be tested against synthetic in-memory images.
class ByteSource {
 public:
  virtual ~ByteSource() = default;
  virtual std::uint64_t size() const = 0;
  // Fills `out` with the bytes at [offset, offset + out.size()). Returns false if that
  // range is not entirely inside the source or the read fails.
  virtual bool read(std::uint64_t offset, std::span<std::byte> out) const = 0;
};

// A source over bytes the caller keeps alive.
class MemorySource final : public ByteSource {
 public:
  explicit MemorySource(std::span<const std::byte> data) : data_(data) {}
  std::uint64_t size() const override { return data_.size(); }
  bool read(std::uint64_t offset, std::span<std::byte> out) const override;

 private:
  std::span<const std::byte> data_;
};

// A source over a file on disk. The file is opened read-only and never modified.
class FileSource final : public ByteSource {
 public:
  static std::unique_ptr<FileSource> open(const std::filesystem::path& path, std::string& error);
  std::uint64_t size() const override { return size_; }
  bool read(std::uint64_t offset, std::span<std::byte> out) const override;

 private:
  FileSource(std::ifstream file, std::uint64_t size) : file_(std::move(file)), size_(size) {}

  mutable std::ifstream file_;
  std::uint64_t size_ = 0;
};

}  // namespace mpt::asset_import
