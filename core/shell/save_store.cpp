#include "save_store.h"

#include <fstream>
#include <iterator>
#include <string>
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

std::optional<SaveMedia> load_save_media(const std::filesystem::path& path, std::string& error) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    if (ec) error = "cannot check " + display_path(path) + ": " + ec.message();
    return std::nullopt;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot read " + display_path(path);
    return std::nullopt;
  }
  const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) {
    error = "cannot read " + display_path(path);
    return std::nullopt;
  }
  if (chars.size() < 4) {
    error = display_path(path) + " is not a save image (shorter than 4 bytes)";
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

SlotOpen open_save_slot(const std::filesystem::path& path) {
  SlotOpen out;
  std::string error;
  out.initial = load_save_media(path, error);
  if (error.empty()) return out;

  // Keep the unreadable file: move it aside to the first free .bak name.
  std::error_code ec;
  for (int n = 1; n <= 99; ++n) {
    std::filesystem::path backup = path;
    backup += n == 1 ? ".bak" : ".bak" + std::to_string(n);
    if (std::filesystem::exists(backup, ec)) continue;
    std::filesystem::rename(path, backup, ec);
    if (!ec) {
      out.warning = error + "; kept it as " + display_path(backup) + " and started with a blank card";
      return out;
    }
    break;
  }
  out.persist = false;
  out.warning = error + "; could not move it aside, so this session will not save (the file is unchanged)";
  return out;
}

}  // namespace mpt::shell
