#include <doctest/doctest.h>

#include <string>

#include "mpt/core/content_id.h"
#include "mpt/core/rng.h"
#include "mpt/core/state.h"
#include "mpt/core/title_registry.h"
#include "../support/fake_runtime.h"

using namespace mpt;

TEST_CASE("title IDs round-trip through their short names") {
  for (TitleId id : kAllTitles) CHECK(parse_title_id(to_string(id)) == id);
  CHECK_FALSE(parse_title_id("mp11").has_value());
  CHECK_FALSE(parse_title_id("MP4").has_value());
}

TEST_CASE("content IDs follow <title>.<kind>.<snake_name>") {
  const auto id = ContentId::parse("mp3.minigame.hand_line_and_sinker");
  REQUIRE(id.has_value());
  CHECK(id->title() == TitleId::mp3);
  CHECK(id->kind() == "minigame");
  CHECK(id->name() == "hand_line_and_sinker");
  CHECK(id->str() == "mp3.minigame.hand_line_and_sinker");

  CHECK(ContentId::parse("mp4.board.toads_midway_madness").has_value());
  CHECK(ContentId::parse("mpss.minigame.bumper_balls2").has_value());

  for (const char* bad : {"", "mp3", "mp3.minigame", "mp3.minigame.", "mp99.board.x", "mp3.Minigame.x",
                          "mp3.minigame.Hand", "mp3.minigame.a.b", "mp3.minigame._x", "mp3.minigame.x_",
                          "mp3.minigame.a__b", "mp3.minigame.1st", "mp3..x"}) {
    CAPTURE(std::string(bad));
    CHECK_FALSE(ContentId::parse(bad).has_value());
  }
}

TEST_CASE("StateHasher is 64-bit FNV-1a") {
  CHECK(StateHasher{}.digest() == 0xcbf29ce484222325ull);
  StateHasher h;
  const std::byte a[] = {std::byte{'a'}};
  h.update(a);
  CHECK(h.digest() == 0xaf63dc4c8601ec8cull);
}

TEST_CASE("DeterministicRng is SplitMix64") {
  DeterministicRng rng(0);
  CHECK(rng.next() == 0xe220a8397b1dcdafull);
  CHECK(rng.next() == 0x6e789e6aa1b965f4ull);

  DeterministicRng bounded(42);
  for (int i = 0; i < 1000; ++i) CHECK(bounded.below(6) < 6);
}

TEST_CASE("TitleRegistry builds runtimes by title and rejects duplicates") {
  TitleRegistry registry;
  auto runtime_factory = [] { return std::make_unique<test::FakeRuntime>(); };
  auto adapter_factory = [](ITitleRuntime&) -> std::unique_ptr<ITitleAdapter> { return nullptr; };

  CHECK(registry.add(TitleId::mp4, runtime_factory, adapter_factory).ok());
  CHECK_FALSE(registry.add(TitleId::mp4, runtime_factory, adapter_factory).ok());
  CHECK_FALSE(registry.add(TitleId::mp1, nullptr, adapter_factory).ok());

  CHECK(registry.contains(TitleId::mp4));
  CHECK_FALSE(registry.contains(TitleId::mp1));
  CHECK(registry.titles() == std::vector<TitleId>{TitleId::mp4});
  CHECK(registry.createRuntime(TitleId::mp4) != nullptr);
  CHECK(registry.createRuntime(TitleId::mp1) == nullptr);
}
