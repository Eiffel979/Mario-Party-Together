#include "mpt/ui/model/library.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

#include <nlohmann/json.hpp>

#include "mpt/asset_import/known_dumps.h"

namespace mpt::ui {
namespace {

constexpr std::string_view kFormat = "mpt.library";
constexpr int kVersion = 1;

std::string path_to_utf8(const std::filesystem::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

std::filesystem::path path_from_utf8(const std::string& s) {
  return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

bool is_hex64(const std::string& s) {
  return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

bool entry_less(const LibraryEntry& a, const LibraryEntry& b) {
  if (a.title != b.title) return static_cast<int>(a.title) < static_cast<int>(b.title);
  return a.region < b.region;
}

}  // namespace

std::string LibraryEntry::display_name() const { return std::string(asset_import::title_display_name(title)); }

std::string LibraryEntry::detail() const {
  std::string text = region;
  if (!revision.empty()) text += (text.empty() ? "" : ", ") + revision;
  return text;
}

std::optional<Library> Library::parse(std::string_view json_text, std::string& error) {
  const nlohmann::json doc = nlohmann::json::parse(json_text, nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded() || !doc.is_object()) {
    error = "library.json is not valid JSON";
    return std::nullopt;
  }
  if (doc.value("format", std::string()) != kFormat || doc.value("version", 0) != kVersion) {
    error = "library.json has an unknown format or version";
    return std::nullopt;
  }
  const auto titles = doc.find("titles");
  if (titles == doc.end() || !titles->is_array()) {
    error = "library.json has no titles list";
    return std::nullopt;
  }
  Library library;
  for (const nlohmann::json& item : *titles) {
    if (!item.is_object()) {
      error = "library.json has an entry that is not an object";
      return std::nullopt;
    }
    LibraryEntry entry;
    const std::optional<TitleId> title = parse_title_id(item.value("title", std::string()));
    entry.region = item.value("region", std::string());
    entry.revision = item.value("revision", std::string());
    entry.sha256 = item.value("sha256", std::string());
    const std::string path = item.value("path", std::string());
    if (!title || !is_hex64(entry.sha256) || path.empty()) {
      error = "library.json has an incomplete entry";
      return std::nullopt;
    }
    entry.title = *title;
    entry.dump_path = path_from_utf8(path);
    library.entries_.push_back(std::move(entry));
  }
  std::sort(library.entries_.begin(), library.entries_.end(), entry_less);
  return library;
}

Library Library::load(const std::filesystem::path& file, std::string& warning) {
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) return {};
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    warning = "Your library list could not be read, so it starts empty.";
    return {};
  }
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::string error;
  std::optional<Library> library = parse(text, error);
  if (!library) {
    warning = "Your library list is damaged (" + error + "), so it starts empty. Add your games again.";
    return {};
  }
  return std::move(*library);
}

std::string Library::to_json() const {
  nlohmann::json titles = nlohmann::json::array();
  for (const LibraryEntry& e : entries_) {
    titles.push_back({{"title", std::string(to_string(e.title))},
                      {"region", e.region},
                      {"revision", e.revision},
                      {"sha256", e.sha256},
                      {"path", path_to_utf8(e.dump_path)}});
  }
  const nlohmann::json doc = {{"format", std::string(kFormat)}, {"version", kVersion}, {"titles", titles}};
  return doc.dump(2) + "\n";
}

bool Library::save(const std::filesystem::path& file, std::string& error) const {
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  std::filesystem::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    const std::string text = to_json();
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
      error = "could not write the library list";
      return false;
    }
  }
  std::filesystem::rename(tmp, file, ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    error = "could not replace the library list";
    return false;
  }
  return true;
}

std::size_t Library::add(const VerifiedDump& dump, const std::filesystem::path& dump_path) {
  LibraryEntry entry;
  entry.title = dump.title;
  entry.region = dump.region;
  entry.revision = dump.revision;
  entry.sha256 = dump.sha256;
  entry.dump_path = dump_path;
  entry.verified = dump;
  std::erase_if(entries_, [&](const LibraryEntry& e) { return e.title == dump.title && e.region == dump.region; });
  const auto at = std::lower_bound(entries_.begin(), entries_.end(), entry, entry_less);
  const auto index = static_cast<std::size_t>(at - entries_.begin());
  entries_.insert(at, std::move(entry));
  return index;
}

void Library::remove(std::size_t index) {
  if (index < entries_.size()) entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
}

}  // namespace mpt::ui
