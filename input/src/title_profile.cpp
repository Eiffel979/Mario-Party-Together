#include "mpt/input/title_profile.h"

#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "mpt/core/content_id.h"

namespace mpt::input {
namespace {

namespace fs = std::filesystem;

// UTF-8 text of a path on every platform (path::string() can throw on Windows).
std::string display(const fs::path& path) {
  const std::u8string text = path.generic_u8string();
  return std::string(text.begin(), text.end());
}

// Reads the file into `text`. False with `why` set if it cannot be used.
bool read_small_file(const fs::path& path, std::string& text, std::string& why) {
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec) {
    why = "cannot read its size (" + ec.message() + ")";
    return false;
  }
  if (size > kMaxProfileFileBytes) {
    why = "larger than " + std::to_string(kMaxProfileFileBytes) + " bytes";
    return false;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    why = "cannot be opened";
    return false;
  }
  text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  if (in.bad()) {
    why = "read error";
    return false;
  }
  return true;
}

// Title checks on top of parse_profile(), which only checks overrides against
// the profile's own "title" when it has one.
bool fits_title(const Profile& profile, TitleId title, std::string& why) {
  if (profile.title && *profile.title != title) {
    why = "made for title '" + std::string(to_string(*profile.title)) + "'";
    return false;
  }
  for (const auto& entry : profile.minigames) {
    const std::string& id = entry.first;
    const auto parsed = ContentId::parse(id);
    if (!parsed || parsed->title() != title) {
      why = "minigame override '" + id + "' belongs to another title";
      return false;
    }
  }
  return true;
}

TitleProfileLoad search(TitleId title, std::span<const fs::path> search_dirs) {
  TitleProfileLoad result;
  const std::string name(to_string(title));
  for (const fs::path& dir : search_dirs) {
    const fs::path path = title_profile_path(dir, title);
    std::error_code ec;
    const fs::file_status status = fs::status(path, ec);
    // Not installed here (also covers a missing search directory).
    if (status.type() == fs::file_type::not_found) continue;
    std::string why;
    std::string text;
    Profile profile;
    if (status.type() == fs::file_type::none) {
      why = "cannot be inspected (" + ec.message() + ")";
    } else if (!fs::is_regular_file(status)) {
      why = "not a regular file";
    } else if (read_small_file(path, text, why)) {
      if (const Result r = parse_profile(text, profile); !r) why = r.error();
      else fits_title(profile, title, why);
    }
    if (!why.empty()) {
      result.warnings.push_back("ignoring " + name + " input profile " + display(path) + ": " + why);
      continue;
    }
    result.profile = std::move(profile);
    result.source = path;
    return result;
  }

  std::string where;
  for (const fs::path& dir : search_dirs) where += (where.empty() ? "" : ", ") + display(dir);
  result.warnings.push_back("no usable " + name + " input profile" +
                            (where.empty() ? std::string(" (no search directories)") : " in " + where) +
                            "; using the built-in default");
  result.profile = builtin_default_profile();
  return result;
}

}  // namespace

void stderr_warning_sink(std::string_view message) {
  std::fprintf(stderr, "input: %.*s\n", static_cast<int>(message.size()), message.data());
}

fs::path title_profile_path(const fs::path& search_dir, TitleId title) {
  return search_dir / fs::path(to_string(title)) / fs::path(kTitleProfileFileName);
}

TitleProfileLoad find_title_profile(TitleId title, std::span<const fs::path> search_dirs) {
  try {
    return search(title, search_dirs);
  } catch (const std::exception& e) {
    TitleProfileLoad fallback;
    fallback.warnings.push_back(std::string("loading the input profile failed (") + e.what() +
                                "); using the built-in default");
    fallback.profile = builtin_default_profile();
    return fallback;
  } catch (...) {
    TitleProfileLoad fallback;
    fallback.warnings.push_back("loading the input profile failed; using the built-in default");
    fallback.profile = builtin_default_profile();
    return fallback;
  }
}

TitleProfileLoad load_title_profile(Mapper& mapper, TitleId title, std::span<const fs::path> search_dirs,
                                    const WarningSink& warn) {
  TitleProfileLoad result = find_title_profile(title, search_dirs);
  mapper.set_profile_all(result.profile);
  if (warn) {
    for (const std::string& line : result.warnings) {
      try {
        warn(line);
      } catch (...) {  // a logging failure must not stop a session
      }
    }
  }
  return result;
}

}  // namespace mpt::input
