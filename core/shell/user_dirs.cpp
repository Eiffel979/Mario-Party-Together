#include "user_dirs.h"

#include <SDL3/SDL.h>

namespace mpt::shell {

std::optional<UserDirs> user_dirs(const std::optional<std::filesystem::path>& user_dir, std::string& error) {
  std::filesystem::path root;
  if (user_dir) {
    root = *user_dir;
  } else {
    // Short org and app names keep build-cache paths well inside Windows' 260-character
    // limit (P1-09/P1-10; see also the longPathAware manifest).
    char* pref = SDL_GetPrefPath("MarioPartyTogether", "mpt");
    if (!pref) {
      error = std::string("no user data folder: ") + SDL_GetError();
      return std::nullopt;
    }
    root = std::filesystem::path(reinterpret_cast<const char8_t*>(pref));
    SDL_free(pref);
  }
  return UserDirs{root, root / "saves", root / "cache"};
}

}  // namespace mpt::shell
