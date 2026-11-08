#include <doctest/doctest.h>

#include <filesystem>

#include "mpt/ui/model/launcher_flow.h"

using namespace mpt;
using namespace mpt::ui;

TEST_CASE("the launcher has focus everywhere except while playing") {
  LauncherFlow flow;
  CHECK(flow.screen() == Screen::library);
  CHECK(flow.has_focus());
  flow.sync(SessionView{.running = true});
  CHECK(flow.screen() == Screen::playing);
  CHECK_FALSE(flow.has_focus());
}

TEST_CASE("back pops the stack and never leaves the library") {
  LauncherFlow flow;
  CHECK_FALSE(flow.back());
  flow.go(Screen::add_dump);
  flow.go(Screen::file_browser);
  CHECK(flow.back());
  CHECK(flow.screen() == Screen::add_dump);
  CHECK(flow.back());
  CHECK(flow.screen() == Screen::library);
  CHECK_FALSE(flow.back());
  CHECK(flow.screen() == Screen::library);
}

TEST_CASE("a start that succeeds goes to playing; one that fails stays with the reason") {
  LauncherFlow flow;
  flow.go(Screen::seating);
  flow.begin_start(3);
  CHECK(flow.starting());
  CHECK_FALSE(flow.back());  // cannot leave while the start is pending
  flow.sync(SessionView{.start_failures = 3});
  CHECK(flow.screen() == Screen::seating);
  flow.sync(SessionView{.start_failures = 4, .last_error = "no runtime"});
  CHECK_FALSE(flow.starting());
  CHECK(flow.screen() == Screen::seating);
  CHECK(flow.message().find("no runtime") != std::string::npos);

  flow.begin_start(4);
  flow.sync(SessionView{.running = true, .start_failures = 4});
  CHECK(flow.screen() == Screen::playing);
  CHECK_FALSE(flow.starting());
  CHECK_FALSE(flow.back());  // nothing to go back to while playing
}

TEST_CASE("pause opens only while playing, and resume goes straight back to playing") {
  LauncherFlow flow;
  CHECK_FALSE(flow.open_pause());  // on the library: nothing to pause
  CHECK_FALSE(flow.resume());      // nor to resume
  flow.sync(SessionView{.running = true});
  REQUIRE(flow.open_pause());
  CHECK(flow.screen() == Screen::pause);
  CHECK(flow.has_focus());
  CHECK_FALSE(flow.back());  // the launcher turns back on the pause screen into resume()

  // Held buttons are the shell's GameInputGate's job, not the flow's.
  CHECK(flow.resume());
  CHECK(flow.screen() == Screen::playing);
  CHECK_FALSE(flow.has_focus());
  CHECK_FALSE(flow.resume());
}

TEST_CASE("quit to library opens the post-session screen and waits for the stop") {
  LauncherFlow flow;
  flow.sync(SessionView{.running = true});
  flow.open_pause();
  flow.quit_to_library();
  CHECK(flow.screen() == Screen::session_end);
  CHECK(flow.ending());
  CHECK_FALSE(flow.ended());
  // The shell has not applied the stop yet: still running, no bounce back to playing.
  flow.sync(SessionView{.running = true, .paused = true, .phase = PartyPhase::board, .turn = 20, .max_turns = 20});
  CHECK(flow.screen() == Screen::session_end);
  CHECK(flow.ending());
  flow.sync(SessionView{.phase = PartyPhase::finished, .turn = 20, .max_turns = 20, .replay_path = "u/replays/a.mptr"});
  CHECK(flow.screen() == Screen::session_end);
  CHECK_FALSE(flow.ending());
  REQUIRE(flow.ended());
  CHECK(flow.ended()->phase == PartyPhase::finished);
  CHECK(flow.ended()->replay_path == std::filesystem::path("u/replays/a.mptr"));
  CHECK(flow.message().empty());
  CHECK(flow.has_focus());
  // Back (or the "Back to library" button) leaves it for the library.
  CHECK(flow.back());
  CHECK(flow.screen() == Screen::library);
  CHECK_FALSE(flow.back());
}

TEST_CASE("a session that ends by itself opens the post-session screen with its final status") {
  LauncherFlow flow;
  flow.sync(SessionView{.running = true, .phase = PartyPhase::board, .turn = 7, .max_turns = 20});
  flow.sync(SessionView{.phase = PartyPhase::minigame, .turn = 7, .max_turns = 20});
  CHECK(flow.screen() == Screen::session_end);
  REQUIRE(flow.ended());
  CHECK(flow.ended()->phase == PartyPhase::minigame);
  CHECK(flow.ended()->turn == 7);
  // A new session clears it.
  flow.sync(SessionView{.running = true});
  CHECK(flow.screen() == Screen::playing);
  CHECK_FALSE(flow.ended());
}

TEST_CASE("turn text shows Turn x/y while a party runs, and nothing without max_turns") {
  CHECK(turn_text(SessionView{.running = true, .phase = PartyPhase::board, .turn = 3, .max_turns = 20}) == "Turn 3/20");
  CHECK(turn_text(SessionView{.running = true, .phase = PartyPhase::minigame, .turn = 10, .max_turns = 10}) == "Turn 10/10");
  CHECK(turn_text(SessionView{.running = true, .phase = PartyPhase::results, .turn = 20, .max_turns = 20}) == "Turn 20/20");
  CHECK(turn_text(SessionView{.running = true, .phase = PartyPhase::board, .turn = 3, .max_turns = 0}).empty());
  CHECK(turn_text(SessionView{.running = true, .phase = PartyPhase::board, .turn = 0, .max_turns = 20}).empty());
  CHECK(turn_text(SessionView{.running = true, .phase = PartyPhase::none, .max_turns = 20}).empty());
  CHECK(turn_text(SessionView{.running = true, .phase = PartyPhase::setup, .max_turns = 20}).empty());
  CHECK(turn_text(SessionView{.running = true, .phase = PartyPhase::finished, .turn = 20, .max_turns = 20}).empty());
}

TEST_CASE("the post-session text for a finished party names the replay relative to the user dir") {
  const std::filesystem::path user = std::filesystem::path("users") / "alice" / "mpt";
  const SessionView s{.phase = PartyPhase::finished, .turn = 10, .max_turns = 10,
                      .replay_path = user / "replays" / "mp4-1-0.mptr"};
  const SessionEnd end = describe_session_end(s, user);
  CHECK(end.finished);
  CHECK(end.heading == "Party finished");
  CHECK(end.detail == "Turns played: 10 of 10.");
  CHECK(end.replay == "Replay saved in your user folder: replays/mp4-1-0.mptr");
  // No other path: not the user dir, and nothing above it.
  CHECK(end.replay.find("alice") == std::string::npos);
  CHECK(end.replay.find("users") == std::string::npos);
}

TEST_CASE("the post-session text for an unfinished party shows the phase it ended in") {
  const std::filesystem::path user = "mpt_user";
  const SessionEnd board = describe_session_end(
      SessionView{.phase = PartyPhase::board, .turn = 7, .max_turns = 20, .replay_path = user / "replays" / "r.mptr"}, user);
  CHECK_FALSE(board.finished);
  CHECK(board.heading == "The game session ended");
  CHECK(board.detail == "It ended in this phase: board (turn 7 of 20).");
  CHECK(board.replay == "Replay saved in your user folder: replays/r.mptr");

  const SessionEnd menus = describe_session_end(SessionView{}, user);
  CHECK_FALSE(menus.finished);
  CHECK(menus.detail == "It ended in this phase: menus (no party yet).");
  CHECK(menus.replay == "No replay was saved.");

  CHECK(describe_session_end(SessionView{.phase = PartyPhase::setup}, user).detail == "It ended in this phase: party setup.");
  CHECK(describe_session_end(SessionView{.phase = PartyPhase::results, .turn = 20}, user).detail ==
        "It ended in this phase: results (turn 20).");
}

TEST_CASE("a replay outside the user dir shows its file name only") {
  CHECK(replay_display_path(std::filesystem::path("elsewhere") / "x" / "mine.mptr", "mpt_user") == "mine.mptr");
  CHECK(replay_display_path(std::filesystem::path("mpt_user") / ".." / "mine.mptr", "mpt_user") == "mine.mptr");
  CHECK(replay_display_path({}, "mpt_user").empty());
  CHECK(replay_display_path(std::filesystem::path("mpt_user") / "replays" / "a.mptr", "mpt_user") == "replays/a.mptr");
}
