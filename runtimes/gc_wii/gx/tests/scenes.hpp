#pragma once
// Synthetic GX scenes (generated data only) for the GX layer's tests.
#include <utility>
#include <vector>

struct Scene {
  const char* name;
  void (*draw)();
  std::vector<std::pair<int, int>> probes;  // logical pixel coordinates
};
const std::vector<Scene>& scenes();
// The arena that holds everything the GX front end sees, at a fixed address on every platform, so the
// FIFO stream (which carries pointers) is the same in every process.
void arena_init();
void arena_reset();
