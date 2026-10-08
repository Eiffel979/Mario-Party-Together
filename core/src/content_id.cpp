#include "mpt/core/content_id.h"

namespace mpt {
namespace {

bool is_snake_case(std::string_view part) {
  if (part.empty() || part.front() < 'a' || part.front() > 'z') return false;
  for (char c : part) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    if (!ok) return false;
  }
  return part.back() != '_' && part.find("__") == std::string_view::npos;
}

}  // namespace

std::optional<ContentId> ContentId::parse(std::string_view text) {
  const std::size_t first_dot = text.find('.');
  if (first_dot == std::string_view::npos) return std::nullopt;
  const std::size_t second_dot = text.find('.', first_dot + 1);
  if (second_dot == std::string_view::npos) return std::nullopt;

  const std::optional<TitleId> title = parse_title_id(text.substr(0, first_dot));
  const std::string_view kind = text.substr(first_dot + 1, second_dot - first_dot - 1);
  const std::string_view name = text.substr(second_dot + 1);
  if (!title || !is_snake_case(kind) || !is_snake_case(name)) return std::nullopt;

  return ContentId(std::string(text), *title, first_dot + 1, kind.size());
}

}  // namespace mpt
