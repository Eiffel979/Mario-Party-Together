#include "mpt/core/title_id.h"

namespace mpt {

std::string_view to_string(TitleId id) {
  switch (id) {
    case TitleId::mp1: return "mp1";
    case TitleId::mp2: return "mp2";
    case TitleId::mp3: return "mp3";
    case TitleId::mp4: return "mp4";
    case TitleId::mp5: return "mp5";
    case TitleId::mp6: return "mp6";
    case TitleId::mp7: return "mp7";
    case TitleId::mp8: return "mp8";
    case TitleId::mp9: return "mp9";
    case TitleId::mp10: return "mp10";
    case TitleId::smp: return "smp";
    case TitleId::mpss: return "mpss";
  }
  return "unknown";
}

std::optional<TitleId> parse_title_id(std::string_view text) {
  for (TitleId id : kAllTitles) {
    if (to_string(id) == text) return id;
  }
  return std::nullopt;
}

}  // namespace mpt
