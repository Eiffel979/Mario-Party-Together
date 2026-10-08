#pragma once

#include <cstdint>

namespace mpt {

// Deterministic RNG for anything outside a title's own game code (Party Director,
// rules, cross-title minigame picks). Seeded by the host, identical on every peer.
// SplitMix64: tiny state, fully specified, same output on every platform.
class DeterministicRng {
 public:
  explicit DeterministicRng(std::uint64_t seed) : state_(seed) {}

  std::uint64_t next() {
    std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
  }

  // Uniform value in [0, bound) without modulo bias. `bound` must be > 0.
  std::uint64_t below(std::uint64_t bound) {
    const std::uint64_t limit = (0 - bound) % bound;  // == 2^64 mod bound
    for (;;) {
      const std::uint64_t r = next();
      if (r >= limit) return r % bound;
    }
  }

  std::uint64_t state() const { return state_; }

 private:
  std::uint64_t state_;
};

}  // namespace mpt
