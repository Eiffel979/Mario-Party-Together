#include "mpt/asset_import/known_dumps.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <sstream>

#include <nlohmann/json.hpp>

#include "mpt/asset_import/byte_source.h"

namespace mpt::asset_import {
namespace {

constexpr int kSchemaVersion = 2;

bool is_lower_hex(std::string_view text, std::size_t length) {
  if (text.size() != length) return false;
  for (char c : text) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

bool is_disc_id(std::string_view text) {
  if (text.size() != 6) return false;
  for (char c : text) {
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
  }
  return true;
}

constexpr std::initializer_list<std::string_view> kFields = {
    "title", "region", "revision", "supported", "game_version", "disc_id", "disc_version",
    "size",  "sha256", "sha1",     "md5",       "reason",       "note"};

}  // namespace

std::optional<KnownDumps> KnownDumps::parse(std::string_view json_text, std::string& error) {
  const nlohmann::json root = nlohmann::json::parse(json_text, nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    error = "known_dumps.json is not a JSON object";
    return std::nullopt;
  }
  const auto version = root.find("version");
  if (version == root.end() || !version->is_number_integer() || version->get<int>() != kSchemaVersion) {
    error = "known_dumps.json has an unsupported version (expected " + std::to_string(kSchemaVersion) + ")";
    return std::nullopt;
  }
  const auto dumps = root.find("dumps");
  if (dumps == root.end() || !dumps->is_array()) {
    error = "known_dumps.json needs a \"dumps\" array";
    return std::nullopt;
  }
  for (const auto& [key, value] : root.items()) {
    if (key != "version" && key != "dumps") {
      error = "known_dumps.json has an unknown field '" + key + "'";
      return std::nullopt;
    }
  }

  KnownDumps out;
  for (std::size_t i = 0; i < dumps->size(); ++i) {
    const nlohmann::json& entry = (*dumps)[i];
    const std::string where = "dumps[" + std::to_string(i) + "]";
    auto fail = [&](const std::string& message) {
      error = where + message;
      return std::nullopt;
    };
    if (!entry.is_object()) return fail(" is not an object");
    for (const auto& [key, value] : entry.items()) {
      if (std::find(kFields.begin(), kFields.end(), key) == kFields.end()) {
        return fail(" has an unknown field '" + key + "'");
      }
    }
    auto text = [&entry](const char* key) -> std::optional<std::string> {
      const auto it = entry.find(key);
      if (it == entry.end()) return std::string{};
      if (!it->is_string()) return std::nullopt;
      return it->get<std::string>();
    };

    KnownDump d;
    const auto title = text("title");
    const auto parsed_title = title ? parse_title_id(*title) : std::nullopt;
    if (!parsed_title) return fail(".title is not a known title ID");
    d.title = *parsed_title;

    const auto supported = entry.find("supported");
    if (supported == entry.end() || !supported->is_boolean()) return fail(".supported must be true or false");
    d.supported = supported->get<bool>();

    for (auto [key, field] : {std::pair{"region", &d.region}, std::pair{"revision", &d.revision},
                              std::pair{"game_version", &d.game_version}, std::pair{"disc_id", &d.disc_id},
                              std::pair{"sha256", &d.sha256}, std::pair{"sha1", &d.sha1}, std::pair{"md5", &d.md5},
                              std::pair{"reason", &d.reason}}) {
      const auto value = text(key);
      if (!value) return fail(std::string(".") + key + " must be a string");
      *field = *value;
    }
    if (const auto note = text("note"); !note) return fail(".note must be a string");
    if (d.region.empty() || d.revision.empty()) return fail(" needs a region and a revision");

    if (const auto size = entry.find("size"); size != entry.end()) {
      if (!size->is_number_unsigned() || size->get<std::uint64_t>() == 0) {
        return fail(".size must be a positive byte count");
      }
      d.size = size->get<std::uint64_t>();
    }
    if (const auto v = entry.find("disc_version"); v != entry.end()) {
      if (!v->is_number_unsigned() || v->get<std::uint64_t>() > 255) return fail(".disc_version must be 0-255");
      d.disc_version = static_cast<std::uint8_t>(v->get<std::uint64_t>());
    }
    if (!d.disc_id.empty() && !is_disc_id(d.disc_id)) return fail(".disc_id must be 6 characters of A-Z and 0-9");
    if (d.disc_version && d.disc_id.empty()) return fail(".disc_version needs a disc_id");
    if (!d.sha256.empty() && !is_lower_hex(d.sha256, 64)) return fail(".sha256 must be 64 lowercase hex characters");
    if (!d.sha1.empty() && !is_lower_hex(d.sha1, 40)) return fail(".sha1 must be 40 lowercase hex characters");
    if (!d.md5.empty() && !is_lower_hex(d.md5, 32)) return fail(".md5 must be 32 lowercase hex characters");

    if (d.supported) {
      if (d.sha256.empty() || d.sha1.empty() || d.size == 0) {
        return fail(" is supported, so it needs sha256, sha1 and size");
      }
      if (!d.reason.empty()) return fail(" is supported, so it must not have a reason");
    } else {
      if (d.reason.empty()) return fail(" is unsupported, so it needs a reason to show the user");
      if (d.sha1.empty() && d.disc_id.empty()) {
        return fail(" is unsupported, so it needs a sha1 or a disc_id to be recognized by");
      }
    }
    for (const KnownDump& earlier : out.entries_) {
      if (!d.sha256.empty() && earlier.sha256 == d.sha256) return fail(" duplicates an earlier sha256");
      if (!d.sha1.empty() && earlier.sha1 == d.sha1) return fail(" duplicates an earlier sha1");
      if (!d.disc_id.empty() && earlier.disc_id == d.disc_id && earlier.disc_version == d.disc_version &&
          earlier.supported == d.supported && d.sha1.empty() && earlier.sha1.empty()) {
        return fail(" duplicates an earlier disc_id and disc_version");
      }
    }
    out.entries_.push_back(std::move(d));
  }
  return out;
}

std::optional<KnownDumps> KnownDumps::load(const std::filesystem::path& path, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "could not open " + path_for_message(path);
    return std::nullopt;
  }
  std::ostringstream text;
  text << file.rdbuf();
  return parse(text.str(), error);
}

const KnownDump* KnownDumps::find_supported(std::string_view sha256, std::string_view sha1,
                                            std::uint64_t size) const {
  for (const KnownDump& dump : entries_) {
    if (dump.supported && dump.sha256 == sha256 && dump.sha1 == sha1 && dump.size == size) return &dump;
  }
  return nullptr;
}

const KnownDump* KnownDumps::find_by_sha1(std::string_view sha1, std::uint64_t size) const {
  for (const KnownDump& dump : entries_) {
    if (!dump.sha1.empty() && dump.sha1 == sha1 && (dump.size == 0 || dump.size == size)) return &dump;
  }
  return nullptr;
}

std::vector<const KnownDump*> KnownDumps::find_by_disc(std::string_view disc_id, std::uint8_t disc_version) const {
  std::vector<const KnownDump*> out;
  for (const KnownDump& dump : entries_) {
    if (!dump.disc_id.empty() && dump.disc_id == disc_id && (!dump.disc_version || *dump.disc_version == disc_version)) {
      out.push_back(&dump);
    }
  }
  return out;
}

std::string_view title_display_name(TitleId title) {
  switch (title) {
    case TitleId::mp1: return "Mario Party";
    case TitleId::mp2: return "Mario Party 2";
    case TitleId::mp3: return "Mario Party 3";
    case TitleId::mp4: return "Mario Party 4";
    case TitleId::mp5: return "Mario Party 5";
    case TitleId::mp6: return "Mario Party 6";
    case TitleId::mp7: return "Mario Party 7";
    case TitleId::mp8: return "Mario Party 8";
    case TitleId::mp9: return "Mario Party 9";
    case TitleId::mp10: return "Mario Party 10";
    case TitleId::smp: return "Super Mario Party";
    case TitleId::mpss: return "Mario Party Superstars";
  }
  return "an unknown title";
}

}  // namespace mpt::asset_import
