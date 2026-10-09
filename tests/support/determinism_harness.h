#pragma once

// Sequential and cross-process determinism harness (ADR 0002 rule 10 follow-up, P1-20b1).
//
// Real runtimes may only exist one at a time per process (ADR 0002 rule 10), so the
// side-by-side comparison in replay_harness.h does not carry over to them. This harness
// instead runs fresh instances one after another, each destroyed before the next is
// created, and records each run's hash trail so it can be compared with another run in
// the same process or written out and compared with a run in a different process
// (see determinism_child.cpp and cross_process_determinism.cmake).
//
// Running sequentially catches state that leaks between instances (globals and statics
// a runtime fails to reset on boot). Running in separate processes also catches hashes
// that depend on addresses or on process-wide state.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "mpt/core/title_runtime.h"
#include "replay_harness.h"

namespace mpt::test {

// One hash checkpoint: stateHash() after `frame` frames have been stepped.
struct Checkpoint {
  std::uint64_t frame = 0;
  std::uint64_t hash = 0;
  bool operator==(const Checkpoint&) const = default;
};

using HashTrail = std::vector<Checkpoint>;

// Creates a new, un-booted runtime. Called once per instance.
using RuntimeFactory = std::function<std::unique_ptr<ITitleRuntime>()>;

// Everything that determines a trail: the determinism inputs of ADR 0002 rule 2 plus
// the run length and checkpoint spacing.
struct TrailSpec {
  VerifiedDump dump;
  RuntimeConfig config;
  InputSource inputs;
  std::uint64_t frames = 0;
  std::uint64_t interval = 60;
};

struct TrailResult {
  Result status = Result::success();
  HashTrail trail;
};

// Synthetic input script shared by the in-process tests and the child process, so both
// sides of a cross-process comparison feed identical inputs. Mashes A and sweeps the
// stick, differently per player.
inline InputFrame scripted_input(std::uint64_t frame) {
  InputFrame in;
  in.frame = frame;
  for (std::size_t p = 0; p < kMaxPlayers; ++p) {
    in.pads[p].connected = 1;
    if ((frame + p) % 3 == 0) in.pads[p].buttons |= kButtonA;
    in.pads[p].stick_x = static_cast<std::int16_t>(static_cast<std::int64_t>((frame * 997 + p * 31) % 65536) - 32768);
  }
  return in;
}

// Steps an already booted runtime and records a checkpoint every 'interval' frames.
inline HashTrail record_trail(ITitleRuntime& runtime, const InputSource& inputs, std::uint64_t frames,
                              std::uint64_t interval) {
  HashTrail trail;
  for (std::uint64_t f = 0; f < frames; ++f) {
    runtime.stepFrame(inputs(f));
    if (interval != 0 && (f + 1) % interval == 0) trail.push_back(Checkpoint{f + 1, runtime.stateHash()});
  }
  return trail;
}

// Creates one runtime, boots it, records its trail and destroys it before returning.
inline TrailResult run_fresh_instance(const RuntimeFactory& factory, const TrailSpec& spec) {
  TrailResult result;
  std::unique_ptr<ITitleRuntime> runtime = factory();
  if (!runtime) {
    result.status = Result::failure("runtime factory returned null");
    return result;
  }
  result.status = runtime->boot(spec.dump, spec.config);
  if (result.status.ok()) result.trail = record_trail(*runtime, spec.inputs, spec.frames, spec.interval);
  return result;  // the runtime is destroyed here, before the caller can create another
}

// Runs 'instances' fresh runtimes strictly one after another: each is destroyed before the
// next is created, so at most one is ever alive (ADR 0002 rule 10). Before every instance
// after the first, a heap block is allocated and kept until the end, so that later
// instances are unlikely to reuse the addresses of earlier ones. That is best effort only:
// the cross-process comparison is the real check for address-dependent hashes.
inline std::vector<TrailResult> run_sequential(const RuntimeFactory& factory, const TrailSpec& spec,
                                               std::size_t instances) {
  std::vector<TrailResult> results;
  std::vector<std::unique_ptr<std::byte[]>> skew;
  for (std::size_t i = 0; i < instances; ++i) {
    if (i != 0) {
      const std::size_t bytes = 4096 * i + 64;
      skew.push_back(std::make_unique<std::byte[]>(bytes));
      std::memset(skew.back().get(), 0xa5, bytes);
    }
    results.push_back(run_fresh_instance(factory, spec));
  }
  return results;
}

// Index of the first checkpoint where the trails differ, or nullopt if they are equal.
// A trail that is a strict prefix of the other differs at the length of the shorter one.
inline std::optional<std::size_t> first_divergence(const HashTrail& a, const HashTrail& b) {
  const std::size_t common = a.size() < b.size() ? a.size() : b.size();
  for (std::size_t i = 0; i < common; ++i) {
    if (!(a[i] == b[i])) return i;
  }
  if (a.size() != b.size()) return common;
  return std::nullopt;
}

inline std::string to_hex64(std::uint64_t value) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out(16, '0');
  for (std::size_t i = 0; i < 16; ++i) out[15 - i] = kDigits[(value >> (4 * i)) & 0xf];
  return out;
}

// Human-readable description of how two trails differ, for test failure messages.
inline std::string describe_divergence(const HashTrail& a, const HashTrail& b) {
  const std::optional<std::size_t> at = first_divergence(a, b);
  if (!at) return "trails are identical";
  const auto describe = [](const HashTrail& trail, std::size_t i) {
    return i < trail.size() ? std::to_string(trail[i].frame) + " " + to_hex64(trail[i].hash) : std::string("(end)");
  };
  return "trails diverge at checkpoint " + std::to_string(*at) + ": " + describe(a, *at) + " vs " + describe(b, *at);
}

// Text form: one "<frame> <16 lowercase hex digits>" line per checkpoint, the layout of the
// .trail.txt sidecars in ADR 0006.
inline std::string format_trail(const HashTrail& trail) {
  std::string out;
  for (const Checkpoint& c : trail) out += std::to_string(c.frame) + " " + to_hex64(c.hash) + "\n";
  return out;
}

// Parses format_trail() output. Blank lines and lines starting with '#' are ignored and a
// trailing CR is tolerated. Any malformed line, uppercase hex, or frame numbers that do
// not strictly increase give nullopt.
inline std::optional<HashTrail> parse_trail(std::string_view text) {
  HashTrail trail;
  while (!text.empty()) {
    const std::size_t eol = text.find('\n');
    std::string_view line = text.substr(0, eol);
    text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty() || line.front() == '#') continue;

    const std::size_t space = line.find(' ');
    if (space == std::string_view::npos || space == 0) return std::nullopt;
    const std::string_view frame_text = line.substr(0, space);
    const std::string_view hash_text = line.substr(space + 1);
    if (hash_text.size() != 16) return std::nullopt;
    for (char ch : hash_text) {
      if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return std::nullopt;
    }

    Checkpoint c;
    const char* frame_end = frame_text.data() + frame_text.size();
    const auto frame_parse = std::from_chars(frame_text.data(), frame_end, c.frame);
    if (frame_parse.ec != std::errc{} || frame_parse.ptr != frame_end) return std::nullopt;
    const char* hash_end = hash_text.data() + hash_text.size();
    const auto hash_parse = std::from_chars(hash_text.data(), hash_end, c.hash, 16);
    if (hash_parse.ec != std::errc{} || hash_parse.ptr != hash_end) return std::nullopt;

    if (!trail.empty() && c.frame <= trail.back().frame) return std::nullopt;
    trail.push_back(c);
  }
  return trail;
}

}  // namespace mpt::test
