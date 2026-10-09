// Every public member of a core contract struct has a default member initializer
// (ADR 0002 amendment 15), so designated initializers that name only some fields build
// cleanly under GCC/Clang -Wextra -Werror (-Wmissing-field-initializers). This file
// fails to compile on the Linux CI job if a member without one is added.

#include <doctest/doctest.h>

#include "mpt/core/title_adapter.h"
#include "mpt/core/title_runtime.h"

using namespace mpt;

TEST_CASE("contract structs accept partial designated initializers") {
  const RuntimeConfig config{.seed = 1};
  const VerifiedDump dump{.title = TitleId::mp4};
  const FrameOutput video{.width = 1};
  const AudioChunk audio{.sample_rate = 1};
  const PlayerState player{.coins = 1};
  const PartySnapshot party{.turn = 1};
  const MinigameSetup setup{.seed = 1};
  const ResolvedRuleset rules{.seed = 1};
  const SaveMedia media{.format_version = 1};
  const SaveState state{.frame = 1};
  const PadState pad{.buttons = 1};
  const InputFrame input{.frame = 1};
  const FrameRate rate{.numerator = 50};

  CHECK(config.seed == 1);
  CHECK_FALSE(config.save_media.has_value());
  CHECK(dump.region.empty());
  CHECK(video.pixels.empty());
  CHECK(audio.samples.empty());
  CHECK_FALSE(player.character.has_value());
  CHECK_FALSE(party.current_minigame.has_value());
  CHECK_FALSE(setup.time_limit_frames.has_value());
  CHECK(rules.minigame_pool.empty());
  CHECK(media.data.empty());
  CHECK(state.data.empty());
  CHECK(pad.buttons == 1);
  CHECK(input.frame == 1);
  CHECK(rate.denominator == 1);
}
