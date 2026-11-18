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
  // Every session and runtime is gone once run() returns or unwinds; then release what titles
  // keep for the process (ADR 0008, "Game module kept loaded for the process"), last
  // registered first. The guard also runs it while an exception from run() unwinds. The
  // catch-and-rethrow is what makes that unwinding happen: for an exception that no handler
  // catches, whether the stack unwinds before std::terminate is implementation-defined.
  struct ShutdownGuard {
    const mpt::shell::ShellTitles& titles;
    ~ShutdownGuard() { mpt::shell::shutdown_titles(titles); }
  };
  try {
    const ShutdownGuard shutdown_guard{titles};
    // The launcher (ui/, ADR 0010). run() creates it only for a windowed live run.
    return mpt::shell::run(*options, titles,
                           [](const mpt::shell::UiHost& host) { return mpt::ui::make_launcher(host); });
  } catch (...) {
    throw;  // shutdown_titles has run; the exception still ends the process as before
  }
}
