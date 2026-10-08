#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "mpt/core/title_id.h"

namespace mpt {

// Stable content identifier: "<title>.<kind>.<snake_name>",
// e.g. "mp3.minigame.hand_line_and_sinker" or "mp4.board.toads_midway_madness".
class ContentId {
 public:
  // Returns nullopt unless `text` is well formed: a known title ID, then a kind
  // and a name that are both lower snake_case ([a-z0-9_], starting with a letter).
  static std::optional<ContentId> parse(std::string_view text);

  TitleId title() const { return title_; }
  std::string_view kind() const { return std::string_view(text_).substr(kind_begin_, kind_size_); }
  std::string_view name() const { return std::string_view(text_).substr(kind_begin_ + kind_size_ + 1); }
  const std::string& str() const { return text_; }

  friend bool operator==(const ContentId&, const ContentId&) = default;
  friend auto operator<=>(const ContentId& a, const ContentId& b) { return a.text_ <=> b.text_; }

 private:
  ContentId(std::string text, TitleId title, std::size_t kind_begin, std::size_t kind_size)
      : text_(std::move(text)), title_(title), kind_begin_(kind_begin), kind_size_(kind_size) {}

  std::string text_;
  TitleId title_;
  std::size_t kind_begin_;
  std::size_t kind_size_;
};

using MinigameId = ContentId;
using BoardId = ContentId;
using CharacterId = ContentId;

}  // namespace mpt
