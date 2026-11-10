// A synthetic stub module that crashes on purpose, for the dump lane's crash report (M4-04,
// tests/dump_lane/supervise.h `--crash-selftest`). No game code: one export, three ways to die.
//
//   kind 0  a stack buffer overrun: `bytes` bytes written into a 16-byte local array, past its end and
//           over the stack cookie, so the function's /GS check (Windows) or stack protector (Linux,
//           built with -fstack-protector-all) fails on return. This is the M4-04 acceptance case.
//   kind 1  a write through a null pointer (an access violation the lane does not handle)
//   kind 2  abort()
//
// The overrun's length comes from the caller and every byte is written through a volatile pointer, so
// the compiler can neither see the overrun at compile time nor drop the writes. A byte loop, not memset:
// with _FORTIFY_SOURCE (Ubuntu's GCC enables it by default) memset becomes __memset_chk, which would
// catch the overrun itself (__chk_fail) before the stack protector runs on return.
#include <cstdlib>

#if defined(_WIN32)
#define MPT_OVERRUN_STUB_EXPORT extern "C" __declspec(dllexport)
#define MPT_OVERRUN_STUB_NOINLINE __declspec(noinline)
#else
#define MPT_OVERRUN_STUB_EXPORT extern "C" __attribute__((visibility("default")))
#define MPT_OVERRUN_STUB_NOINLINE __attribute__((noinline))
#endif

namespace {

volatile int* volatile g_null = nullptr;

MPT_OVERRUN_STUB_NOINLINE int overrun_stub_smash(int bytes) {
  char buffer[16];
  volatile char* write = buffer;
  for (int i = 0; i < bytes; ++i) write[i] = 'x';  // past the end on purpose
  return write[0] + write[15];
}

}  // namespace

MPT_OVERRUN_STUB_EXPORT int mpt_overrun_stub_crash(int kind, int bytes) {
  switch (kind) {
    case 0:
      return overrun_stub_smash(bytes);
    case 1:
      *g_null = bytes;
      return 1;
    case 2:
      std::abort();
    default:
      return -1;
  }
}
