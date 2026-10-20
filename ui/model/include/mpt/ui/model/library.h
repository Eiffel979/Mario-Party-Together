#pragma once

// The library: the titles the user has added and verified (ADR 0010 §4, P1-23).
//
// It is a small list in the user data folder (<user>/library.json) of where each
// verified dump is and what it hashed to. That is the user's own local data: it holds
// paths and hashes only, never game bytes. A VerifiedDump is never stored: after a
// restart the launcher re-checks the file through asset-import (identify, then a
// build-cache hit) and gets a fresh one from make_verified.

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mpt/core/dump.h"
#include "mpt/core/title_id.h"

namespace mpt::ui {

struct LibraryEntry {
  TitleId title{};
  std::string region;
  std::string revision;
  std::string sha256;              // what the dump hashed to when it was added
  std::filesystem::path dump_path;  // the user's file, never copied or modified
  std::optional<VerifiedDump> verified;  // this run only, from make_verified

  std::string display_name() const;  // "Mario Party 4": text from the catalog, never art
  std::string detail() const;        // "USA, Rev 0"
};

class Library {
 public:
  // Reads the list. A missing file is an empty library; a damaged one is an empty
  // library plus a warning (the file is left as it is until the next save).
  static Library load(const std::filesystem::path& file, std::string& warning);
  static std::optional<Library> parse(std::string_view json, std::string& error);
  // The file's text. The launcher writes it with the shell's durable atomic write
  // (core/shell/durable_file.h), which keeps this model free of the shell and SDL.
  std::string to_json() const;

  // Adds a verified dump, replacing an entry for the same title and region. Returns
  // its index.
  std::size_t add(const VerifiedDump& dump, const std::filesystem::path& dump_path);
  void remove(std::size_t index);

  const std::vector<LibraryEntry>& entries() const { return entries_; }
  LibraryEntry* at(std::size_t index) { return index < entries_.size() ? &entries_[index] : nullptr; }

 private:
  std::vector<LibraryEntry> entries_;  // sorted by title, then region
};

}  // namespace mpt::ui
