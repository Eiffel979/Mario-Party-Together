#include <doctest/doctest.h>

#include "mpt/ui/model/launcher_flow.h"

using namespace mpt::ui;

TEST_CASE("the launcher has focus everywhere except while playing") {
  LauncherFlow flow;
  CHECK(flow.screen() == Screen::library);
  CHECK(flow.has_focus());
  flow.sync(SessionView{true, false, 0, ""});
  CHECK(flow.screen() == Screen::playing);
  CHECK_FALSE(flow.has_focus());
}

TEST_CASE("back pops the stack and never leaves the library") {
  LauncherFlow flow;
  CHECK_FALSE(flow.back(0));
  flow.go(Screen::add_dump);
  flow.go(Screen::file_browser);
  CHECK(flow.back(0));
  CHECK(flow.screen() == Screen::add_dump);
  CHECK(flow.back(0));
  CHECK(flow.screen() == Screen::library);
  CHECK_FALSE(flow.back(0));
  CHECK(flow.screen() == Screen::library);
}

TEST_CASE("a start that succeeds goes to playing; one that fails stays with the reason") {
  LauncherFlow flow;
  flow.go(Screen::seating);
  flow.begin_start(3);
  CHECK(flow.starting());
  CHECK_FALSE(flow.back(0));  // cannot leave while the start is pending
  flow.sync(SessionView{false, false, 3, ""});
  CHECK(flow.screen() == Screen::seating);
  flow.sync(SessionView{false, false, 4, "no runtime"});
  CHECK_FALSE(flow.starting());
  CHECK(flow.screen() == Screen::seating);
  CHECK(flow.message().find("no runtime") != std::string::npos);

  flow.begin_start(4);
  flow.sync(SessionView{true, false, 4, ""});
  CHECK(flow.screen() == Screen::playing);
  CHECK_FALSE(flow.starting());
  CHECK_FALSE(flow.back(0));  // nothing to go back to while playing
}

TEST_CASE("pause opens only while playing, and resume waits for held buttons") {
  LauncherFlow flow;
  CHECK_FALSE(flow.open_pause());  // on the library: nothing to pause
  flow.sync(SessionView{true, false, 0, ""});
  REQUIRE(flow.open_pause());
  CHECK(flow.screen() == Screen::pause);
  CHECK(flow.has_focus());

  flow.choose_resume(1000);
  CHECK(flow.resuming());
  CHECK_FALSE(flow.resume_ready(true, 2000));  // the confirm press is still held
  CHECK(flow.screen() == Screen::pause);
  CHECK(flow.resume_ready(false, 3000));
  CHECK(flow.screen() == Screen::playing);
  CHECK_FALSE(flow.has_focus());
}

TEST_CASE("a button held forever does not keep the game paused") {
  LauncherFlow flow;
  flow.sync(SessionView{true, false, 0, ""});
  flow.open_pause();
  flow.choose_resume(0);
  CHECK_FALSE(flow.resume_ready(true, LauncherFlow::kResumeHoldCapNs - 1));
  CHECK(flow.resume_ready(true, LauncherFlow::kResumeHoldCapNs));
}

TEST_CASE("back on the pause screen means resume") {
  LauncherFlow flow;
  flow.sync(SessionView{true, false, 0, ""});
  flow.open_pause();
  CHECK(flow.back(10));
  CHECK(flow.resuming());
  CHECK(flow.resume_ready(false, 11));
}

TEST_CASE("quit to library waits for the session to stop instead of bouncing back") {
  LauncherFlow flow;
  flow.sync(SessionView{true, false, 0, ""});
  flow.open_pause();
  flow.quit_to_library();
  CHECK(flow.screen() == Screen::library);
  flow.sync(SessionView{true, true, 0, ""});  // the shell has not applied the stop yet
  CHECK(flow.screen() == Screen::library);
  flow.sync(SessionView{false, false, 0, ""});
  CHECK(flow.screen() == Screen::library);
  CHECK(flow.message().empty());
}

TEST_CASE("a session that ends by itself returns to the library with a note") {
  LauncherFlow flow;
  flow.sync(SessionView{true, false, 0, ""});
  flow.sync(SessionView{false, false, 0, ""});
  CHECK(flow.screen() == Screen::library);
  CHECK_FALSE(flow.message().empty());
}
