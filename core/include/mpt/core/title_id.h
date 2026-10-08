#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace mpt {

// Every mainline title the shell knows about. Values are stable and appear in
// save states and netplay handshakes, so append only and never reorder.
enum class TitleId : unsigned char {
  mp1 = 0,
  mp2,
  mp3,
  mp4,
  mp5,
  mp6,
  mp7,
  mp8,
  mp9,
  mp10,
  smp,   // Super Mario Party
  mpss,  // Mario Party Superstars
};

inline constexpr std::array<TitleId, 12> kAllTitles = {
    TitleId::mp1, TitleId::mp2, TitleId::mp3, TitleId::mp4,  TitleId::mp5, TitleId::mp6,
    TitleId::mp7, TitleId::mp8, TitleId::mp9, TitleId::mp10, TitleId::smp, TitleId::mpss,
};

// Short ID used in content IDs, file names and docs ("mp4", "smp", ...).
std::string_view to_string(TitleId id);
std::optional<TitleId> parse_title_id(std::string_view text);

}  // namespace mpt
