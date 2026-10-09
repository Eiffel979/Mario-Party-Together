#include "mpt/asset_import/known_dumps.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "mpt/asset_import/byte_source.h"

namespace mpt::asset_import {
namespace {

constexpr int kSchemaVersion = 1;

bool is_sha256_hex(std::string_view text) {
  if (text.size() != 64) return false;
  for (char c : text) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

}  // namespace

std::optional<KnownDumps> KnownDumps::parse(std::string_view json_text, std::string& error) {
  const nlohmann::json root = nlohmann::json::parse(json_text, nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    error = "known_dumps.json is not a JSON object";
    return std::nullopt;
  }
  if (root.value("version", 0) != kSchemaVersion) {
    error = "known_dumps.json has an unsupported version (expected " + std::to_string(kSchemaVersion) + ")";
    return std::nullopt;
  }
  const auto dumps = root.find("dumps");
  if (dumps == root.end() || !dumps->is_array()) {
    error = "known_dumps.json needs a \"dumps\" array";
    return std::nullopt;
  }

  KnownDumps out;
  for (std::size_t i = 0; i < dumps->size(); ++i) {
    const nlohmann::json& entry = (*dumps)[i];
    const std::string where = "dumps[" + std::to_string(i) + "]";
    if (!entry.is_object()) {
      error = where + " is not an object";
      return std::nullopt;
    }
    const auto title = parse_title_id(entry.value("title", std::string{}));
    const std::string sha256 = entry.value("sha256", std::string{});
    const std::uint64_t size = entry.value("size", std::uint64_t{0});
    if (!title) {
      error = where + ".title is not a known title ID";
      return std::nullopt;
    }
    if (!is_sha256_hex(sha256)) {
      error = where + ".sha256 must be 64 lowercase hex characters";
      return std::nullopt;
    }
    if (size == 0) {
      error = where + ".size must be a positive byte count";
      return std::nullopt;
    }
    if (out.find_by_sha256(sha256)) {
      error = where + " duplicates an earlier sha256";
      return std::nullopt;
    }
    out.entries_.push_back(KnownDump{*title, entry.value("region", std::string{}),
                                     entry.value("revision", std::string{}), sha256, size});
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

const KnownDump* KnownDumps::find_by_sha256(std::string_view sha256) const {
  for (const KnownDump& dump : entries_) {
    if (dump.sha256 == sha256) return &dump;
  }
  return nullptr;
}

}  // namespace mpt::asset_import
