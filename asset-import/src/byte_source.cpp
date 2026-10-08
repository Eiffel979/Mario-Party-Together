#include "mpt/asset_import/byte_source.h"

#include <cstring>

namespace mpt::asset_import {

bool MemorySource::read(std::uint64_t offset, std::span<std::byte> out) const {
  if (offset > data_.size() || out.size() > data_.size() - offset) return false;
  if (!out.empty()) std::memcpy(out.data(), data_.data() + offset, out.size());
  return true;
}

std::unique_ptr<FileSource> FileSource::open(const std::filesystem::path& path, std::string& error) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) {
    error = "Could not read " + path.string() + ": not a regular file.";
    return nullptr;
  }
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  std::ifstream file(path, std::ios::binary);
  if (ec || !file) {
    error = "Could not read " + path.string();
    return nullptr;
  }
  return std::unique_ptr<FileSource>(new FileSource(std::move(file), static_cast<std::uint64_t>(size)));
}

bool FileSource::read(std::uint64_t offset, std::span<std::byte> out) const {
  if (offset > size_ || out.size() > size_ - offset) return false;
  if (out.empty()) return true;
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(offset));
  file_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
  return file_ && static_cast<std::size_t>(file_.gcount()) == out.size();
}

}  // namespace mpt::asset_import
