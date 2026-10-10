#pragma once

// Runtime conformance suite (P1-20c): title-agnostic checks of ITitleRuntime, and
// optionally ITitleAdapter, against core contract v1 (ADRs 0002-0005).
//
// Each check builds whole runtimes one after another, never two alive at once (ADR 0002
// rule 10), and returns the problems it found as text: an empty list means the target
// conforms. conformance_tests.h turns the checks into doctest cases for one target.
// FakeRuntime and FakeAdapter run in CI (test_conformance_fake.cpp). Mp4Runtime runs the
// same checks on the dump lane once it exists (P1-16, P1-20d).
//
// The checks need only core headers and the shared test support headers, so a runtime
// built in another CMake project (runtimes/gc_wii, Clang) can include them as well.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../support/adapter_checks.h"
#include "../support/determinism_harness.h"
#include "mpt/core/title_adapter.h"
#include "mpt/core/title_runtime.h"

namespace mpt::conformance {

using AdapterFactory = std::function<std::unique_ptr<ITitleAdapter>(ITitleRuntime&)>;
using Problems = std::vector<std::string>;

// What to check and how to drive it. The inputs should exercise the title: for real
// titles a recorded replay, for FakeRuntime its party script.
struct Target {
  std::string name;
  test::RuntimeFactory runtime;
  AdapterFactory adapter{};          // empty: the adapter checks are skipped
  VerifiedDump dump{};
  RuntimeConfig config{};            // seed and initial save media; the suite varies presentation
  test::InputSource inputs{};
  std::uint64_t frames = 600;
  std::uint64_t interval = 60;        // checkpoint spacing for trail comparisons
  std::uint64_t save_frame = 300;     // where the save/load checks split the run (0 < save_frame < frames)
  std::optional<SaveMedia> other_save_media{};  // a different initial image: its trail must differ
  bool expect_save_write = false;     // the inputs make the game commit at least one save-media write
  bool expect_minigame_phase = false; // with an adapter: the inputs reach phase == minigame
};

namespace detail {

inline constexpr std::size_t kMaxProblemsPerCheck = 8;

inline void add(Problems& problems, std::string text) {
  if (problems.size() < kMaxProblemsPerCheck) problems.push_back(std::move(text));
  else if (problems.size() == kMaxProblemsPerCheck) problems.push_back("(further problems not listed)");
}

inline std::string at(std::uint64_t frame, const std::string& what) {
  return "frame " + std::to_string(frame) + ": " + what;
}

inline bool rate_valid(const FrameRate& r) { return r.numerator > 0 && r.denominator > 0; }

inline std::string rate_text(const FrameRate& r) {
  return std::to_string(r.numerator) + "/" + std::to_string(r.denominator);
}

// The input for 'frame', with InputFrame::frame set as ADR 0002 rule 3 requires.
inline InputFrame input_at(const test::InputSource& inputs, std::uint64_t frame) {
  InputFrame in = inputs(frame);
  in.frame = frame;
  return in;
}

// One booted runtime and its adapter. The adapter is declared last so it is destroyed
// first: it must not outlive its runtime (ADR 0002 rule 10).
struct Instance {
  std::unique_ptr<ITitleRuntime> runtime;
  std::unique_ptr<ITitleAdapter> adapter;
  explicit operator bool() const { return runtime != nullptr; }
};

// Creates and boots a runtime with 'config' (and an adapter if the target has one).
// On failure it adds a problem and returns an empty instance.
inline Instance boot(const Target& t, const RuntimeConfig& config, Problems& problems, bool with_adapter = true) {
  Instance instance;
  instance.runtime = t.runtime();
  if (!instance.runtime) {
    add(problems, "the runtime factory returned null");
    return {};
  }
  if (Result r = instance.runtime->boot(t.dump, config); !r) {
    add(problems, "boot() failed: " + r.error());
    return {};
  }
  if (with_adapter && t.adapter) {
    instance.adapter = t.adapter(*instance.runtime);
    if (!instance.adapter) add(problems, "the adapter factory returned null");
  }
  return instance;
}

// Steps [from, to) and records stateHash() after every frame that is a multiple of
// t.interval.
inline test::HashTrail step(const Target& t, ITitleRuntime& runtime, const test::InputSource& inputs,
                            std::uint64_t from, std::uint64_t to) {
  test::HashTrail trail;
  for (std::uint64_t f = from; f < to; ++f) {
    runtime.stepFrame(input_at(inputs, f));
    if ((f + 1) % t.interval == 0) trail.push_back(test::Checkpoint{f + 1, runtime.stateHash()});
  }
  return trail;
}

// A whole run with 'config' and 'inputs', from a fresh instance (destroyed on return).
inline std::optional<test::HashTrail> run(const Target& t, const RuntimeConfig& config,
                                          const test::InputSource& inputs, Problems& problems) {
  Instance instance = boot(t, config, problems, false);
  if (!instance) return std::nullopt;
  return step(t, *instance.runtime, inputs, 0, t.frames);
}

inline void expect_same(const test::HashTrail& a, const test::HashTrail& b, const std::string& what,
                        Problems& problems) {
  if (test::first_divergence(a, b)) add(problems, what + ": " + test::describe_divergence(a, b));
}

}  // namespace detail

// frameRate() is valid after boot() and after every stepFrame() (ADR 0003 rule 1).
inline Problems check_frame_rate(const Target& t) {
  Problems problems;
  detail::Instance instance = detail::boot(t, t.config, problems, false);
  if (!instance) return problems;
  ITitleRuntime& runtime = *instance.runtime;
  if (!detail::rate_valid(runtime.frameRate())) {
    detail::add(problems, "after boot(): invalid frame rate " + detail::rate_text(runtime.frameRate()));
  }
  for (std::uint64_t f = 0; f < t.frames; ++f) {
    runtime.stepFrame(detail::input_at(t.inputs, f));
    if (!detail::rate_valid(runtime.frameRate())) {
      detail::add(problems, detail::at(f, "invalid frame rate " + detail::rate_text(runtime.frameRate())));
    }
  }
  return problems;
}

// Rule 3: inputs.frame counts the frames stepped so far, and SaveState::frame says how
// many had been stepped when the state was taken. Fresh runs are deterministic, also
// when instances run one after another in one process (rules 2 and 10).
inline Problems check_frame_numbering(const Target& t) {
  Problems problems;
  std::optional<test::HashTrail> first;
  {
    detail::Instance instance = detail::boot(t, t.config, problems, false);
    if (!instance) return problems;
    ITitleRuntime& runtime = *instance.runtime;
    if (runtime.saveState().frame != 0) detail::add(problems, "SaveState::frame is not 0 right after boot()");
    first.emplace();
    for (std::uint64_t f = 0; f < t.frames; ++f) {
      runtime.stepFrame(detail::input_at(t.inputs, f));
      if ((f + 1) % t.interval != 0) continue;
      const SaveState state = runtime.saveState();
      if (state.frame != f + 1) {
        detail::add(problems, detail::at(f + 1, "SaveState::frame is " + std::to_string(state.frame)));
      }
      if (state.title != runtime.id()) detail::add(problems, detail::at(f + 1, "SaveState::title is not id()"));
      first->push_back(test::Checkpoint{f + 1, runtime.stateHash()});
    }
  }
  const std::optional<test::HashTrail> second = detail::run(t, t.config, t.inputs, problems);
  if (second) detail::expect_same(*first, *second, "a second fresh instance differs", problems);
  return problems;
}

// Rules 3 and 5, ADR 0003 and ADR 0004 rule 9: a state saved at save_frame and loaded
// into a fresh runtime (booted with another seed and blank media) restores stateHash(),
// frameRate(), saveMedia() and saveMediaGeneration(), and the adapter's snapshot. Stepping
// on from inputs.frame == save_frame then gives the same trail as the uninterrupted run.
inline Problems check_save_load(const Target& t) {
  Problems problems;
  SaveState saved;
  std::uint64_t hash_at_save = 0;
  FrameRate rate_at_save;
  SaveMedia media_at_save;
  std::uint64_t generation_at_save = 0;
  std::optional<PartySnapshot> party_at_save;
  test::HashTrail straight_tail;
  {
    detail::Instance producer = detail::boot(t, t.config, problems);
    if (!producer) return problems;
    detail::step(t, *producer.runtime, t.inputs, 0, t.save_frame);
    saved = producer.runtime->saveState();
    hash_at_save = producer.runtime->stateHash();
    rate_at_save = producer.runtime->frameRate();
    media_at_save = producer.runtime->saveMedia();
    generation_at_save = producer.runtime->saveMediaGeneration();
    if (producer.adapter) party_at_save = producer.adapter->readParty();
    straight_tail = detail::step(t, *producer.runtime, t.inputs, t.save_frame, t.frames);
  }  // destroyed before the consumer exists (rule 10)

  if (saved.frame != t.save_frame) {
    detail::add(problems, "SaveState::frame is " + std::to_string(saved.frame) + ", expected " +
                              std::to_string(t.save_frame));
  }
  RuntimeConfig other = t.config;
  other.seed = t.config.seed + 1;
  other.save_media.reset();
  detail::Instance consumer = detail::boot(t, other, problems);
  if (!consumer) return problems;
  ITitleRuntime& runtime = *consumer.runtime;
  if (Result r = runtime.loadState(saved); !r) {
    detail::add(problems, "loadState() of a state from the same build failed: " + r.error());
    return problems;
  }
  if (runtime.stateHash() != hash_at_save) detail::add(problems, "stateHash() after loadState() differs (rule 5)");
  if (!(runtime.frameRate() == rate_at_save)) {
    detail::add(problems, "frameRate() after loadState() is " + detail::rate_text(runtime.frameRate()) + ", was " +
                              detail::rate_text(rate_at_save));
  }
  if (!(runtime.saveMedia() == media_at_save)) detail::add(problems, "saveMedia() after loadState() differs (ADR 0004 rule 9)");
  if (runtime.saveMediaGeneration() != generation_at_save) {
    detail::add(problems, "saveMediaGeneration() after loadState() is " + std::to_string(runtime.saveMediaGeneration()) +
                              ", was " + std::to_string(generation_at_save) + " (ADR 0004 rule 9)");
  }
  if (consumer.adapter && party_at_save && !(consumer.adapter->readParty() == *party_at_save)) {
    detail::add(problems, "readParty() after loadState() differs from the producer's snapshot");
  }
  const test::HashTrail resumed_tail = detail::step(t, runtime, t.inputs, t.save_frame, t.frames);
  detail::expect_same(straight_tail, resumed_tail, "the run resumed after loadState() differs", problems);
  return problems;
}

// Rules 6-8: video() and audio() describe well-formed buffers, cpu_rgba8 only (no GPU
// handle until an ADR defines the hand-off), and their views stay valid and unchanged
// through the read calls until the next stepFrame(). Reading them does not change
// stateHash(), and neither do the other read calls.
inline Problems check_views(const Target& t) {
  Problems problems;
  detail::Instance instance = detail::boot(t, t.config, problems, false);
  if (!instance) return problems;
  ITitleRuntime& runtime = *instance.runtime;
  for (std::uint64_t f = 0; f < t.frames; ++f) {
    runtime.stepFrame(detail::input_at(t.inputs, f));
    if (f % 7 != 0 && f + 1 != t.frames) continue;  // a spread of frames keeps this cheap for real titles

    const std::uint64_t hash_before = runtime.stateHash();
    const FrameOutput video = runtime.video();
    const AudioChunk audio = runtime.audio();
    if (video.kind == FrameOutput::Kind::gpu_texture) {
      detail::add(problems, detail::at(f, "video() returned a GPU texture; contract v1 allows cpu_rgba8 only (rule 8)"));
    }
    if (video.kind == FrameOutput::Kind::cpu_rgba8 &&
        (video.width == 0 || video.height == 0 ||
         video.pixels.size() != std::size_t{video.width} * std::size_t{video.height})) {
      detail::add(problems, detail::at(f, "cpu_rgba8 frame size does not match width * height (rule 7)"));
    }
    if (!audio.samples.empty() && (audio.channels == 0 || audio.sample_rate == 0 ||
                                   audio.samples.size() % audio.channels != 0)) {
      detail::add(problems, detail::at(f, "audio chunk is not whole interleaved frames at a non-zero rate"));
    }
    const std::vector<std::uint32_t> pixels(video.pixels.begin(), video.pixels.end());
    const std::vector<std::int16_t> samples(audio.samples.begin(), audio.samples.end());

    // Read calls between two stepFrame()s.
    (void)runtime.frameRate();
    (void)runtime.saveMediaGeneration();
    (void)runtime.saveMedia();
    (void)runtime.saveState();
    const FrameOutput again = runtime.video();
    if (runtime.stateHash() != hash_before) detail::add(problems, detail::at(f, "a read call changed stateHash()"));
    if (!std::equal(video.pixels.begin(), video.pixels.end(), pixels.begin(), pixels.end()) ||
        !std::equal(again.pixels.begin(), again.pixels.end(), pixels.begin(), pixels.end())) {
      detail::add(problems, detail::at(f, "video pixels changed before the next stepFrame() (rule 6)"));
    }
    if (!std::equal(audio.samples.begin(), audio.samples.end(), samples.begin(), samples.end())) {
      detail::add(problems, detail::at(f, "audio samples changed before the next stepFrame() (rule 6)"));
    }
  }
  return problems;
}

// Rule 2: internal_scale and widescreen are local presentation options and never reach
// stateHash().
inline Problems check_presentation_options(const Target& t) {
  Problems problems;
  RuntimeConfig plain = t.config;
  plain.internal_scale = 1;
  plain.widescreen = false;
  RuntimeConfig fancy = t.config;
  fancy.internal_scale = 3;
  fancy.widescreen = true;
  const std::optional<test::HashTrail> a = detail::run(t, plain, t.inputs, problems);
  const std::optional<test::HashTrail> b = detail::run(t, fancy, t.inputs, problems);
  if (a && b) detail::expect_same(*a, *b, "internal_scale 3 and widescreen change the trail", problems);
  return problems;
}

// ADR 0004: the generation is 0 after boot(), never decreases while stepping, and changes
// on the same frames in every run (rule 4). The initial image is a determinism input
// (rule 3), and booting with the image a run produced gives that image back.
inline Problems check_save_media(const Target& t) {
  Problems problems;
  const auto generation_changes = [&](std::optional<SaveMedia>& final_image) {
    std::vector<std::uint64_t> changes;
    detail::Instance instance = detail::boot(t, t.config, problems, false);
    if (!instance) return changes;
    ITitleRuntime& runtime = *instance.runtime;
    std::uint64_t generation = runtime.saveMediaGeneration();
    if (generation != 0) detail::add(problems, "saveMediaGeneration() is not 0 after boot()");
    for (std::uint64_t f = 0; f < t.frames; ++f) {
      runtime.stepFrame(detail::input_at(t.inputs, f));
      const std::uint64_t now = runtime.saveMediaGeneration();
      if (now < generation) detail::add(problems, detail::at(f, "saveMediaGeneration() went down while stepping"));
      if (now != generation) changes.push_back(f);
      generation = now;
    }
    final_image = runtime.saveMedia();
    return changes;
  };
  std::optional<SaveMedia> image_a;
  std::optional<SaveMedia> image_b;
  const std::vector<std::uint64_t> a = generation_changes(image_a);
  const std::vector<std::uint64_t> b = generation_changes(image_b);
  if (a != b) detail::add(problems, "the generation changed on different frames in two runs (rule 4)");
  if (t.expect_save_write && a.empty()) detail::add(problems, "the inputs were expected to commit a save, but none did");
  if (image_a && image_b && !(*image_a == *image_b)) detail::add(problems, "two runs ended with different images");

  if (image_a) {
    RuntimeConfig reboot = t.config;
    reboot.save_media = *image_a;
    detail::Instance instance = detail::boot(t, reboot, problems, false);
    if (instance) {
      if (!(instance.runtime->saveMedia() == *image_a)) detail::add(problems, "boot() with a produced image does not give it back");
      if (instance.runtime->saveMediaGeneration() != 0) detail::add(problems, "saveMediaGeneration() is not 0 after boot()");
    }
  }
  if (t.other_save_media) {
    RuntimeConfig other = t.config;
    other.save_media = t.other_save_media;
    const std::optional<test::HashTrail> base = detail::run(t, t.config, t.inputs, problems);
    const std::optional<test::HashTrail> changed = detail::run(t, other, t.inputs, problems);
    if (base && changed && !test::first_divergence(*base, *changed)) {
      detail::add(problems, "a different initial save image gives the same trail (ADR 0004 rule 3)");
    }
  }
  return problems;
}

// ADR 0002 amendment 12: a stick axis at -32768 is treated exactly as -32767. Every stick
// axis of every connected pad is forced to one of the two values on a spread of frames.
inline Problems check_stick_folding(const Target& t) {
  Problems problems;
  const auto forced = [&t](std::int16_t value) {
    return test::InputSource([&t, value](std::uint64_t frame) {
      InputFrame in = t.inputs(frame);
      if (frame % 5 == 2) {
        for (PadState& pad : in.pads) {
          if (!pad.connected) continue;
          pad.stick_x = value;
          pad.stick_y = value;
          pad.c_stick_x = value;
          pad.c_stick_y = value;
        }
      }
      return in;
    });
  };
  const std::optional<test::HashTrail> low = detail::run(t, t.config, forced(-32768), problems);
  const std::optional<test::HashTrail> folded = detail::run(t, t.config, forced(-32767), problems);
  if (low && folded) detail::expect_same(*low, *folded, "-32768 and -32767 on the sticks give different trails", problems);
  return problems;
}

// ADR 0005 and ADR 0002 rule 9, with an adapter: the catalog is well formed, and after
// every stepFrame() readParty() is a pure read whose snapshot passes check_snapshot
// (current_minigame set exactly when phase == minigame).
inline Problems check_adapter(const Target& t) {
  Problems problems;
  if (!t.adapter) return problems;
  detail::Instance instance = detail::boot(t, t.config, problems);
  if (!instance || !instance.adapter) return problems;
  ITitleRuntime& runtime = *instance.runtime;
  ITitleAdapter& adapter = *instance.adapter;
  const Catalog catalog = adapter.catalog();
  if (const std::string p = test::check_catalog(catalog, runtime.id()); !p.empty()) detail::add(problems, "catalog: " + p);
  if (const std::string p = test::check_snapshot(adapter.readParty(), catalog); !p.empty()) {
    detail::add(problems, "after boot(): " + p);
  }
  bool saw_minigame = false;
  for (std::uint64_t f = 0; f < t.frames; ++f) {
    runtime.stepFrame(detail::input_at(t.inputs, f));
    if (const std::string p = test::check_read_party_is_pure(runtime, adapter); !p.empty()) detail::add(problems, detail::at(f, p));
    const PartySnapshot snapshot = adapter.readParty();
    if (const std::string p = test::check_snapshot(snapshot, catalog); !p.empty()) detail::add(problems, detail::at(f, p));
    saw_minigame = saw_minigame || snapshot.phase == PartyPhase::minigame;
  }
  if (t.expect_minigame_phase && !saw_minigame) {
    detail::add(problems, "the inputs were expected to reach the minigame phase, but never did");
  }
  return problems;
}

}  // namespace mpt::conformance
