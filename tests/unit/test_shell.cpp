// Headless shell logic (P1-06): pacing, letterbox, save files, and FrameLoop, including
// the ADR 0010 §3.5 check that a replay's trail is the same with the UI on or off and
// with a pause and resume mid-run.

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "../shell/fake_title.h"
#include "../support/fake_party_script.h"
#include "frame_loop.h"
#include "letterbox.h"
#include "mpt/core/replay.h"
#include "overlay.h"
#include "pacer.h"
#include "save_store.h"

using namespace mpt;
using namespace mpt::shell;

namespace {

constexpr std::uint64_t kFrames = 500;

std::uint64_t test_process_id() {
#ifdef _WIN32
  return _getpid();
#else
  return static_cast<std::uint64_t>(getpid());
#endif
}

TitleRegistry fake_registry() {
  TitleRegistry registry;
  const ShellTitle title = test::fake_shell_title();
  REQUIRE(registry.add(title.id, title.runtime, title.adapter).ok());
  return registry;
}

SessionConfig fake_config(std::optional<SaveMedia> media = std::nullopt) {
  SessionConfig config;
  config.dump = test::fake_shell_dump();
  config.runtime.seed = 42;
  config.runtime.save_media = std::move(media);
  return config;
}

std::unique_ptr<Session> start_ok(const TitleRegistry& registry, const SessionConfig& config) {
  auto started = Session::start(registry, config);
  if (const Result* failure = std::get_if<Result>(&started)) FAIL(failure->error());
  return std::move(std::get<std::unique_ptr<Session>>(started));
}

// Per process, so parallel test runs (or two build trees) never share files.
std::filesystem::path scratch_dir(const char* name) {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                    ("mpt_shell_tests_" + std::to_string(test_process_id())) / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

// A stand-in for ui/: takes focus for a while at a fixed host tick, then lets go.
class StubOverlay final : public IOverlay {
 public:
  StubOverlay(std::uint64_t open_at, std::uint64_t close_at) : open_at_(open_at), close_at_(close_at) {}
  bool hasFocus() const override { return tick_ >= open_at_ && tick_ < close_at_; }
  bool handleEvent(const SDL_Event&) override { return hasFocus(); }
  void update(std::uint64_t) override { ++tick_; }
  void render(SDL_Renderer*, int, int) override {}

 private:
  std::uint64_t open_at_;
  std::uint64_t close_at_;
  std::uint64_t tick_ = 0;
};

// Plays `replay` through FrameLoop the way the shell does, with an optional overlay and
// an optional host pause between two host ticks. Returns the checkpoints it saw.
std::vector<ReplayCheckpoint> play(const Replay& replay, IOverlay* overlay,
                                   std::optional<std::pair<std::uint64_t, std::uint64_t>> pause) {
  const TitleRegistry registry = fake_registry();
  auto config = session_config_for(replay.header, test::fake_shell_dump());
  REQUIRE(std::holds_alternative<SessionConfig>(config));
  auto session = start_ok(registry, std::get<SessionConfig>(config));

  ReplayReader reader(replay);
  std::vector<ReplayCheckpoint> trail;
  FrameLoopHooks hooks;
  hooks.sample = [&](std::uint64_t) -> std::optional<InputFrame> {
    InputFrame in;
    if (!reader.next(in)) return std::nullopt;
    return in;
  };
  hooks.after_step = [&](Session& s) {
    if (s.frame() % replay.header.checkpoint_interval == 0) trail.push_back(checkpoint_of(s));
  };
  FrameLoop loop(*session, std::move(hooks));

  for (std::uint64_t host_tick = 0; host_tick < 10 * kFrames; ++host_tick) {
    if (pause && host_tick == pause->first) loop.pause();
    if (pause && host_tick == pause->second) loop.resume();
    const bool focus = overlay && overlay->hasFocus();
    const FrameLoop::Tick tick = loop.tick(focus);
    if (overlay) overlay->update(host_tick);
    if (tick == FrameLoop::Tick::input_ended) break;
    REQUIRE(tick != FrameLoop::Tick::failed);
  }
  return trail;
}

Replay record_party() {
  const TitleRegistry registry = fake_registry();
  const SessionConfig config = fake_config();
  auto header = make_replay_header(config, false);
  REQUIRE(std::holds_alternative<ReplayHeader>(header));
  ReplayRecorder recorder(std::get<ReplayHeader>(header));
  auto session = start_ok(registry, config);
  std::uint64_t frame = 0;
  FrameLoopHooks hooks;
  hooks.sample = [&](std::uint64_t f) -> std::optional<InputFrame> {
    if (f >= kFrames) return std::nullopt;
    frame = f;
    return test::fake_party_script(f);
  };
  FrameLoop loop(*session, std::move(hooks), &recorder);
  while (loop.tick(false) == FrameLoop::Tick::stepped) {
  }
  CHECK(frame == kFrames - 1);
  return recorder.finish();
}

}  // namespace

TEST_CASE("FramePacer gives exact integer deadlines (ADR 0003 rule 2)") {
  FramePacer pacer(1000);
  const FrameRate ntsc{60000, 1001};
  std::uint64_t last = 0;
  for (std::uint64_t k = 1; k <= 60000; ++k) last = pacer.advance(ntsc);
  // 60000 NTSC frames take exactly 1001 seconds.
  CHECK(last == 1000 + 1001ull * FramePacer::kNsPerSecond);
  CHECK(FramePacer::offset_ns(1, ntsc) == 16'683'333);
  CHECK(FramePacer::offset_ns(3, ntsc) == 50'050'000);

  SUBCASE("a rate change continues from the previous deadline") {
    const std::uint64_t before = pacer.last_deadline();
    CHECK(pacer.advance(FrameRate{50, 1}) == before + 20'000'000);
    CHECK(pacer.advance(FrameRate{50, 1}) == before + 40'000'000);
  }
  SUBCASE("reset starts a new segment") {
    pacer.reset(5);
    CHECK(pacer.advance(ntsc) == 5 + 16'683'333);
  }
  SUBCASE("an invalid rate is treated as 60 Hz") {
    pacer.reset(0);
    CHECK(pacer.advance(FrameRate{0, 1}) == 16'666'666);
  }
}

TEST_CASE("letterbox keeps the aspect and centres the frame") {
  CHECK(letterbox(1280, 720, Aspect{4, 3}) == PixelRect{160, 0, 960, 720});
  CHECK(letterbox(1280, 720, Aspect{16, 9}) == PixelRect{0, 0, 1280, 720});
  CHECK(letterbox(800, 800, Aspect{16, 9}) == PixelRect{0, 175, 800, 450});
  CHECK(letterbox(0, 720, Aspect{4, 3}) == PixelRect{});
  CHECK(display_aspect(false).num == 4);
  CHECK(display_aspect(false).den == 3);
  CHECK(display_aspect(true).num == 16);
  // A 640x448 frame is still shown 4:3.
  CHECK(letterbox(1280, 720, display_aspect(false)) == PixelRect{160, 0, 960, 720});
}

TEST_CASE("save slots: paths, atomic writes and round trips") {
  const std::filesystem::path root = scratch_dir("saves");
  CHECK(save_slot_path(root, TitleId::mp4, "USA") == root / "mp4" / "USA" / "slot0.mptsave");
  CHECK(save_slot_path(root, TitleId::mp4, "../evil") == root / "mp4" / "unknown" / "slot0.mptsave");
  CHECK(save_slot_path(root, TitleId::mp4, "") == root / "mp4" / "unknown" / "slot0.mptsave");

  const std::filesystem::path slot = save_slot_path(root, TitleId::mp4, "USA");
  std::string error;
  CHECK_FALSE(load_save_media(slot, error).has_value());
  CHECK(error.empty());  // missing file = blank card, not an error

  const SaveMedia card = test::FakeRuntime::make_save_media(7);
  REQUIRE(store_save_media(slot, card).ok());
  CHECK(load_save_media(slot, error) == card);
  const SaveMedia newer = test::FakeRuntime::make_save_media(8);
  REQUIRE(store_save_media(slot, newer).ok());  // replaces the existing file
  CHECK(load_save_media(slot, error) == newer);
  CHECK_FALSE(std::filesystem::exists(slot.string() + ".tmp"));

  REQUIRE(write_file_atomic(slot, std::vector<std::byte>(2)).ok());
  CHECK_FALSE(load_save_media(slot, error).has_value());
  CHECK_FALSE(error.empty());

  // No temporary files are left behind.
  for (const auto& entry : std::filesystem::directory_iterator(slot.parent_path())) {
    CHECK(display_path(entry.path().filename()) == "slot0.mptsave");
  }
}

TEST_CASE("an unreadable save slot is kept, never overwritten") {
  const std::filesystem::path root = scratch_dir("unreadable");
  const std::filesystem::path slot = save_slot_path(root, TitleId::mp4, "USA");

  SUBCASE("missing: blank card, saves allowed") {
    const SlotOpen opened = open_save_slot(slot);
    CHECK_FALSE(opened.initial.has_value());
    CHECK(opened.persist);
    CHECK(opened.warning.empty());
  }
  SUBCASE("readable: loaded") {
    REQUIRE(store_save_media(slot, test::FakeRuntime::make_save_media(3)).ok());
    const SlotOpen opened = open_save_slot(slot);
    CHECK(opened.initial == test::FakeRuntime::make_save_media(3));
    CHECK(opened.persist);
  }
  SUBCASE("unreadable: moved to .bak, then .bak2, and the session starts blank") {
    const std::vector<std::byte> damaged{std::byte{1}, std::byte{2}};
    REQUIRE(write_file_atomic(slot, damaged).ok());
    SlotOpen opened = open_save_slot(slot);
    CHECK_FALSE(opened.initial.has_value());
    CHECK(opened.persist);
    CHECK(opened.warning.find("slot0.mptsave.bak") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(slot));
    CHECK(std::filesystem::file_size(slot.string() + ".bak") == 2);

    REQUIRE(write_file_atomic(slot, damaged).ok());
    opened = open_save_slot(slot);
    CHECK(std::filesystem::exists(slot.string() + ".bak2"));
    CHECK(std::filesystem::file_size(slot.string() + ".bak") == 2);  // the first backup is untouched
  }
}

TEST_CASE("save files work under a folder whose name is not ASCII") {
  const std::filesystem::path root = scratch_dir("unicode") / std::filesystem::path(u8"Jos\u00e9 \u5c71\u7530");
  const std::filesystem::path slot = save_slot_path(root, TitleId::mp4, "USA");
  REQUIRE(store_save_media(slot, test::FakeRuntime::make_save_media(9)).ok());
  std::string error;
  CHECK(load_save_media(slot, error) == test::FakeRuntime::make_save_media(9));
  CHECK(display_path(slot).find("Jos\xc3\xa9") != std::string::npos);  // UTF-8, no exception
}

TEST_CASE("FrameLoop does not sample or step while paused or while the UI has focus") {
  const TitleRegistry registry = fake_registry();
  auto session = start_ok(registry, fake_config());
  int sampled = 0;
  FrameLoopHooks hooks;
  hooks.sample = [&](std::uint64_t f) -> std::optional<InputFrame> {
    ++sampled;
    return test::fake_party_script(f);
  };
  FrameLoop loop(*session, std::move(hooks));
  CHECK(loop.tick(true) == FrameLoop::Tick::ui_focus);
  loop.pause();
  CHECK(loop.tick(false) == FrameLoop::Tick::paused);
  CHECK(sampled == 0);
  CHECK(session->frame() == 0);
  loop.resume();
  CHECK(loop.tick(false) == FrameLoop::Tick::stepped);
  CHECK(sampled == 1);
  CHECK(session->frame() == 1);
}

TEST_CASE("FrameLoop persists save writes only for non-ephemeral sessions") {
  const TitleRegistry registry = fake_registry();
  for (bool ephemeral : {false, true}) {
    CAPTURE(ephemeral);
    auto session = start_ok(registry, fake_config());
    std::vector<SaveMedia> persisted;
    FrameLoopHooks hooks;
    hooks.sample = [](std::uint64_t f) -> std::optional<InputFrame> {
      if (f >= 320) return std::nullopt;
      return test::fake_party_script(f);  // saves at frame 300
    };
    if (!ephemeral) hooks.persist = [&](const SaveMedia& m) { persisted.push_back(m); };
    FrameLoop loop(*session, std::move(hooks));
    while (loop.tick(false) == FrameLoop::Tick::stepped) {
    }
    CHECK(persisted.size() == (ephemeral ? 0u : 1u));
    if (!ephemeral) CHECK(persisted[0] == test::FakeRuntime::make_save_media(1));
    CHECK_FALSE(session->takeSaveMedia().has_value());  // drained either way
  }
}

TEST_CASE("FrameLoop reports minigame changes for the input mapper") {
  const TitleRegistry registry = fake_registry();
  auto session = start_ok(registry, fake_config());
  std::vector<std::string> changes;
  FrameLoopHooks hooks;
  hooks.sample = [](std::uint64_t f) -> std::optional<InputFrame> {
    if (f >= kFrames) return std::nullopt;
    return test::fake_party_script(f);
  };
  hooks.minigame_changed = [&](const std::optional<MinigameId>& id) { changes.push_back(id ? id->str() : "-"); };
  FrameLoop loop(*session, std::move(hooks));
  while (loop.tick(false) == FrameLoop::Tick::stepped) {
  }
  // Three minigames (turns 1-3), each entered and left.
  REQUIRE(changes.size() == 6);
  for (std::size_t i = 0; i < changes.size(); i += 2) {
    CHECK(changes[i].starts_with("mp4.minigame.fake_"));
    CHECK(changes[i + 1] == "-");
  }
}

TEST_CASE("a replay's trail is the same with the UI on or off and across a pause (ADR 0010 §3.5)") {
  const Replay replay = record_party();
  REQUIRE(replay.frame_count == kFrames);

  const std::vector<ReplayCheckpoint> plain = play(replay, nullptr, std::nullopt);
  CHECK(plain == replay.checkpoints);

  StubOverlay overlay(/*open_at=*/120, /*close_at=*/170);  // the UI holds focus mid-run
  CHECK(play(replay, &overlay, std::nullopt) == plain);

  StubOverlay later(/*open_at=*/400, /*close_at=*/420);
  CHECK(play(replay, &later, std::make_pair<std::uint64_t, std::uint64_t>(250, 300)) == plain);
}
