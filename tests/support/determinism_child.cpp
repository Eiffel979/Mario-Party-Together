// Child process for the cross-process determinism test (P1-20b1, ADR 0002 rule 10).
//
// Runs FakeRuntime on the shared synthetic input script and prints its hash trail, so
// cross_process_determinism.cmake can run it in several processes and compare the output
// byte for byte. Nothing printed may depend on the process: no addresses, process IDs,
// times or paths.
//
// Usage: mpt_determinism_child [--seed N] [--frames N] [--interval N] [--instances N]
//                              [--heap-skew BYTES] [--save-counter N]
//                              [--party-script]
//   --instances N   run N fresh instances one after another in this process, and exit
//                   with 3 unless all their trails agree
//   --heap-skew B   allocate and keep B bytes before the first instance, so this process
//                   lays out its heap differently from a run without it
//   --party-script    use fake_party_script (a whole party, P1-20b2) instead of the
//                     plain synthetic script
//   --save-counter N  boot with a FakeRuntime save image holding N instead of blank
//                     media (the initial image is a determinism input, ADR 0004)

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "determinism_harness.h"
#include "fake_party_script.h"
#include "fake_runtime.h"

namespace {

bool parse_u64(std::string_view text, std::uint64_t& out) {
  const char* end = text.data() + text.size();
  const auto parsed = std::from_chars(text.data(), end, out);
  return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == end;
}

int usage() {
  std::fputs(
      "usage: mpt_determinism_child [--seed N] [--frames N] [--interval N] [--instances N] [--heap-skew BYTES] "
      "[--save-counter N] [--party-script]\n",
      stderr);
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::uint64_t seed = 1234;
  std::uint64_t frames = 600;
  std::uint64_t interval = 60;
  std::uint64_t instances = 1;
  std::uint64_t heap_skew = 0;
  std::uint64_t save_counter = 0;
  bool has_save_media = false;
  bool party_script = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--party-script") {
      party_script = true;
      continue;
    }
    std::uint64_t* value = nullptr;
    if (arg == "--seed") value = &seed;
    else if (arg == "--frames") value = &frames;
    else if (arg == "--interval") value = &interval;
    else if (arg == "--instances") value = &instances;
    else if (arg == "--heap-skew") value = &heap_skew;
    else if (arg == "--save-counter") {
      value = &save_counter;
      has_save_media = true;
    }
    else return usage();
    if (i + 1 >= argc || !parse_u64(argv[++i], *value)) return usage();
  }
  if (interval == 0 || instances == 0) return usage();

  // Kept alive until exit. The volatile read stops the allocation from being optimized out.
  std::vector<std::byte> skew(static_cast<std::size_t>(heap_skew), std::byte{0x5a});
  if (!skew.empty()) {
    volatile std::byte sink = skew[skew.size() / 2];
    (void)sink;
  }

  mpt::test::TrailSpec spec;
  spec.config.seed = seed;
  // Blank media unless --save-counter is given. Spelled out, not left to a default,
  // because the initial image is a determinism input (ADR 0004 rule 3).
  spec.config.save_media = has_save_media
                               ? std::optional<mpt::SaveMedia>(mpt::test::FakeRuntime::make_save_media(save_counter))
                               : std::nullopt;
  spec.inputs = party_script ? mpt::test::InputSource(mpt::test::fake_party_script)
                             : mpt::test::InputSource(mpt::test::scripted_input);
  spec.frames = frames;
  spec.interval = interval;
  const auto factory = []() -> std::unique_ptr<mpt::ITitleRuntime> { return std::make_unique<mpt::test::FakeRuntime>(); };
  const std::vector<mpt::test::TrailResult> results =
      mpt::test::run_sequential(factory, spec, static_cast<std::size_t>(instances));

  for (std::size_t i = 0; i < results.size(); ++i) {
    if (!results[i].status.ok()) {
      std::fprintf(stderr, "instance %zu failed to boot: %s\n", i, results[i].status.error().c_str());
      return 1;
    }
    if (mpt::test::first_divergence(results[0].trail, results[i].trail)) {
      const std::string why = mpt::test::describe_divergence(results[0].trail, results[i].trail);
      std::fprintf(stderr, "instance %zu differs from instance 0 in the same process: %s\n", i, why.c_str());
      return 3;
    }
  }

  const std::string header = "# mpt determinism trail: FakeRuntime seed=" + std::to_string(seed) +
                             " frames=" + std::to_string(frames) + " interval=" + std::to_string(interval) +
                             " inputs=" + (party_script ? "party" : "scripted") +
                             " save_media=" + (has_save_media ? "counter:" + std::to_string(save_counter) : "blank") +
                             "\n";
  std::fputs(header.c_str(), stdout);
  std::fputs(mpt::test::format_trail(results[0].trail).c_str(), stdout);
  return std::fflush(stdout) == 0 ? 0 : 1;
}
