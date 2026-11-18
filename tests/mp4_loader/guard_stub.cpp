// A synthetic stub module for the E5 module-reload check (M4-16; ADR 0008, "Stack guard values in save
// states"; tests/dump_lane `--guard-reload-selftest`). No game code: one export with a /GS-guarded frame
// that stays suspended while the caller unloads and reloads this module, as a game function suspended
// on a coroutine stack is during an in-process save-state round trip.
//
// The function fills a local array through an index the caller bounds (so /GS guards its frame),
// calls `yield`, which switches away and back, and returns. Built at a fixed base (layout.cmake's mpt_mp4_fixed_base_link) so a reload maps
// it where the suspended frame's return address expects it, like the game module.
#if defined(_WIN32)
#define MPT_GUARD_STUB_EXPORT extern "C" __declspec(dllexport)
#else
#define MPT_GUARD_STUB_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// MSVC guards only frames it cannot prove safe; strict_gs_check guards every frame with a local array.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma strict_gs_check(on)
#endif

// `n` is 64; it comes from the caller so the compiler cannot see the array's bounds are kept, and
// guards the frame.
MPT_GUARD_STUB_EXPORT int mpt_guard_stub_suspend(void (*yield)(void*), void* user, int n) {
  volatile char buffer[64];
  for (int i = 0; i < n && i < 64; ++i) buffer[i] = static_cast<char>(i);
  yield(user);
  return buffer[0] + buffer[63];  // 63
}
