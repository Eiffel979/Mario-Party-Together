// mpt_make_fake_replay OUT.mptr
//
// Records the whole-party FakeRuntime script through Session and ReplayRecorder with the
// settings of tests/golden/fake_runtime_v1_party.trail.txt (seed 1234, a save card
// holding counter 5, 600 frames, checkpoints every 30). The shell smoke test then checks
// that `mpt_shell_fake --verify-replay OUT.mptr` prints exactly that golden trail.
// The card image is embedded, so the replay is self-contained (it is a build artifact,
// never a committed fixture).

#include <cstdio>
#include <memory>
#include <variant>

#include "../support/fake_party_script.h"
#include "fake_title.h"
#include "mpt/core/replay.h"
#include "mpt/core/session.h"
#include "save_store.h"

int main(int argc, char** argv) {
  using namespace mpt;
  if (argc != 2) {
    std::fprintf(stderr, "usage: mpt_make_fake_replay OUT.mptr\n");
    return 2;
  }
  TitleRegistry registry;
  const shell::ShellTitle title = test::fake_shell_title();
  if (!registry.add(title.id, title.runtime, title.adapter)) return 1;

  SessionConfig config;
  config.dump = test::fake_shell_dump();
  config.runtime.seed = 1234;
  config.runtime.save_media = test::FakeRuntime::make_save_media(5);

  auto header = make_replay_header(config, /*embed_save_media=*/true, 30);
  if (const Result* failure = std::get_if<Result>(&header)) {
    std::fprintf(stderr, "%s\n", failure->error().c_str());
    return 1;
  }
  ReplayRecorder recorder(std::get<ReplayHeader>(std::move(header)));
  auto started = Session::start(registry, config);
  if (const Result* failure = std::get_if<Result>(&started)) {
    std::fprintf(stderr, "%s\n", failure->error().c_str());
    return 1;
  }
  Session& session = *std::get<std::unique_ptr<Session>>(started);
  for (std::uint64_t f = 0; f < 600; ++f) {
    const InputFrame in = test::fake_party_script(f);
    if (!session.step(in) || !recorder.record(in, session)) return 1;
  }
  if (Result written = shell::write_file_atomic(argv[1], encode_replay(recorder.finish())); !written) {
    std::fprintf(stderr, "%s\n", written.error().c_str());
    return 1;
  }
  std::printf("wrote %s\n", argv[1]);
  return 0;
}
