// mpt_shell_fake: the real shell (mpt_shell_app) with FakeRuntime registered as mp4
// instead of the generated registration. Headless smoke tests drive it (P1-06).

#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <string>

#include "app.h"
#include "fake_title.h"

int main(int argc, char** argv) {
  SDL_SetMainReady();
  std::string message;
  int exit_code = 0;
  const auto options = mpt::shell::parse_args(argc, argv, message, exit_code);
  if (!options) {
    std::fputs(message.c_str(), exit_code == 0 ? stdout : stderr);
    return exit_code;
  }
#ifdef MPT_SHELL_FAKE_PARTY
  // mpt_shell_fake_party (M4-06): the title plays the whole-party script by itself.
  const mpt::shell::ShellTitles titles{mpt::test::fake_party_shell_title()};
#else
  const mpt::shell::ShellTitles titles{mpt::test::fake_shell_title()};
#endif
  return mpt::shell::run(*options, titles);
}
