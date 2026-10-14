#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <string>

#include "app.h"
#include "mpt/ui/launcher.h"
#include "titles.h"

int main(int argc, char** argv) {
  SDL_SetMainReady();

  std::string message;
  int exit_code = 0;
  const auto options = mpt::shell::parse_args(argc, argv, message, exit_code);
  if (!options) {
    std::fputs(message.c_str(), exit_code == 0 ? stdout : stderr);
    return exit_code;
  }
  mpt::shell::ShellTitles titles;
  mpt::shell::register_titles(titles);  // generated from the enabled runtime targets
  // The launcher (ui/, ADR 0010). run() creates it only for a windowed live run.
  return mpt::shell::run(*options, titles,
                         [](const mpt::shell::UiHost& host) { return mpt::ui::make_launcher(host); });
}
