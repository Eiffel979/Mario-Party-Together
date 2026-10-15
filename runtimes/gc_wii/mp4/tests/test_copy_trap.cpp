// The copy-destination debug trap (src/copy_trap.h) through the real GXCopyTex on the null renderer:
// a CPU read of a destination is reported, the allow-listed overlay may read it, and a CPU write
// releases the range. Synthetic memory only.
#include <cstdio>
#include <cstring>
#include <string>

#include <dolphin/gx.h>

#include "../src/copy_trap.h"
#include "mpt/gx/renderer.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace {
const char* g_overlay = "m401Dll";
int g_failures = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                       \
    }                                                                     \
  } while (0)

unsigned char* pages(std::size_t size) {
#if defined(_WIN32)
  return static_cast<unsigned char*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
  void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p == MAP_FAILED ? nullptr : static_cast<unsigned char*>(p);
#endif
}

void copyTo(void* dest) {
  GXSetTexCopySrc(0, 0, 128, 128);
  GXSetTexCopyDst(128, 128, GX_TF_RGBA8, GX_FALSE);  // 64 KiB on the CPU side
  GXCopyTex(dest, GX_FALSE);
}

volatile unsigned char g_sink;
}  // namespace

extern "C" const char* mpt_mp4_current_overlay_name(void) { return g_overlay; }

int main() {
  std::string error;
  mpt::gx::RendererConfig config;
  auto renderer = mpt::gx::create_renderer(config, error);
  CHECK(renderer != nullptr);
  if (!renderer) return 1;
  GXInit(nullptr, 0);
  unsigned char* buf = pages(256 * 1024);
  CHECK(buf != nullptr);
  if (!buf) return 1;

  mpt::mp4::copy_trap_enable();
  renderer->begin_frame();
  copyTo(buf + 100);  // not page aligned: only the whole pages inside are trapped

  // A read by an overlay that is not allow-listed is reported once; execution continues.
  mpt::mp4::copy_trap_arm();
  g_sink = buf[100 + 8192];
  g_sink = buf[100 + 8193];
  mpt::mp4::copy_trap_disarm();
  const std::string v = mpt::mp4::copy_trap_take_violation();
  CHECK(v.find("CPU read of a GXCopyTex destination") != std::string::npos);
  CHECK(v.find("m401Dll") != std::string::npos);
  CHECK(mpt::mp4::copy_trap_take_violation().empty());

  // Memory outside the destination is never trapped, nor is the partial first page.
  copyTo(buf + 100);
  mpt::mp4::copy_trap_arm();
  g_sink = buf[50];
  g_sink = buf[200 * 1024];
  mpt::mp4::copy_trap_disarm();
  CHECK(mpt::mp4::copy_trap_take_violation().empty());

  // The allow-listed overlay (m415, Stamp Out!) reads the zeros it gets.
  g_overlay = "m415Dll";
  mpt::mp4::copy_trap_arm();
  g_sink = buf[100 + 16384];
  mpt::mp4::copy_trap_disarm();
  CHECK(mpt::mp4::copy_trap_take_violation().empty());

  // A write releases the range: later reads of it are not reported.
  g_overlay = "m401Dll";
  copyTo(buf + 4096 * 20);
  mpt::mp4::copy_trap_arm();
  std::memset(buf + 4096 * 20, 0, 4096);
  g_sink = buf[4096 * 21];
  mpt::mp4::copy_trap_disarm();
  CHECK(mpt::mp4::copy_trap_take_violation().empty());

  mpt::gx::FrameImage image;
  renderer->end_frame(&image);
  mpt::mp4::copy_trap_disable();
  if (g_failures == 0) std::printf("copy-destination trap: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
