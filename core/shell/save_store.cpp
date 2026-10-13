#include "save_store.h"

#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include "mpt/core/replay.h"

namespace mpt::shell {

std::filesystem::path save_slot_path(const std::filesystem::path& saves_root, TitleId title, std::string_view region) {
  bool plain = !region.empty() && region.size() <= 16;
  for (char c : region) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    plain = plain && ok;
  }
  return saves_root / std::string(to_string(title)) / (plain ? std::string(region) : std::string("unknown")) /
         "slot0.mptsave";
}

Result write_file_atomic(const std::filesystem::path& path, std::span<const std::byte> bytes) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) return Result::failure("cannot create " + path.parent_path().string() + ": " + ec.message());

  std::filesystem::path temp = path;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return Result::failure("cannot write " + temp.string());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
      out.close();
      std::filesystem::remove(temp, ec);
      return Result::failure("cannot write " + temp.string());
    }
  }
  std::filesystem::rename(temp, path, ec);  // replaces an existing file on every platform
  if (ec) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return Result::failure("cannot replace " + path.string() + ": " + ec.message());
  }
  return Result::success();
}

std::optional<SaveMedia> load_save_media(const std::filesystem::path& path, std::string& error) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return std::nullopt;
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot read " + path.string();
    return std::nullopt;
  }
  const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (chars.size() < 4) {
    error = path.string() + " is not a save image (shorter than 4 bytes)";
    return std::nullopt;
  }
  SaveMedia media;
  for (std::size_t i = 0; i < 4; ++i) {
    media.format_version |= static_cast<std::uint32_t>(static_cast<unsigned char>(chars[i])) << (8 * i);
  }
  media.data.reserve(chars.size() - 4);
  for (std::size_t i = 4; i < chars.size(); ++i) media.data.push_back(static_cast<std::byte>(chars[i]));
  return media;
}

Result store_save_media(const std::filesystem::path& path, const SaveMedia& media) {
  return write_file_atomic(path, save_media_image_bytes(media));
}

}  // namespace mpt::shell
