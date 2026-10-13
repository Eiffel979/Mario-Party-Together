#pragma once

// Host-side frame pacing from ITitleRuntime::frameRate() (ADR 0003 rule 2). Pure integer
// arithmetic: no clock is read here, the shell passes host time in. Pacing never reaches
// game logic; lockstep and replays are frame-indexed.

#include <cstdint>

#include "mpt/core/title_runtime.h"

namespace mpt::shell {

class FramePacer {
 public:
  static constexpr std::uint64_t kNsPerSecond = 1'000'000'000ull;

  explicit FramePacer(std::uint64_t now_ns = 0) { reset(now_ns); }

  // Starts a new segment at `now_ns` (start of play, after a pause, or after falling too
  // far behind). Keeps the current rate.
  void reset(std::uint64_t now_ns) {
    t0_ = now_ns;
    k_ = 0;
    last_deadline_ = now_ns;
  }

  // Call once per stepped frame with the rate read after that step. Returns the host
  // time at which the next frame is due. A rate change starts a new segment at the
  // previous deadline, so deadline k of a segment is exactly
  // t0 + floor(k * 1e9 * denominator / numerator), with no drift.
  std::uint64_t advance(FrameRate rate) {
    if (rate.numerator == 0 || rate.denominator == 0) rate = FrameRate{};  // invalid: treat as 60 Hz
    if (!has_rate_ || !(rate == rate_)) {
      t0_ = last_deadline_;
      k_ = 0;
      rate_ = rate;
      has_rate_ = true;
    }
    ++k_;
    last_deadline_ = t0_ + offset_ns(k_, rate_);
    return last_deadline_;
  }

  std::uint64_t last_deadline() const { return last_deadline_; }

  // floor(k * 1e9 * den / num) without 128-bit arithmetic: split 1e9 * den / num into a
  // quotient and a remainder. Exact while k * num fits in 64 bits (about 9e9 hours at
  // 60000/1001).
  static std::uint64_t offset_ns(std::uint64_t k, FrameRate rate) {
    const std::uint64_t period_num = kNsPerSecond * rate.denominator;
    const std::uint64_t q = period_num / rate.numerator;
    const std::uint64_t r = period_num % rate.numerator;
    return k * q + (k * r) / rate.numerator;
  }

 private:
  std::uint64_t t0_ = 0;
  std::uint64_t k_ = 0;
  std::uint64_t last_deadline_ = 0;
  FrameRate rate_{};
  bool has_rate_ = false;
};

}  // namespace mpt::shell
