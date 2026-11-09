// A stub MP4 game module for the host loader test (test_module_loader.cpp): no game code, no SDK and
// no dump, only the module's one export. It is linked exactly like the real module, at the fixed
// image base, through mpt_mp4_fixed_base_link (runtimes/gc_wii/mp4/layout.cmake), so public CI checks
// the fixed base on Windows (/BASE /DYNAMICBASE:NO /FIXED) and on Linux (lld --image-base, glibc's
// loader) without Party Board.
//
// Variants, by compile definition:
//   MPT_STUB_API_SIZE   the table's struct_size: MPT_MP4_API_SIZE_V1_1 (status set) or _V1_0 (no status)
//   MPT_STUB_NO_EXPORT  export no mpt_mp4_get_api at all
//   MPT_STUB_BIG_IMAGE  a 256 KiB zero-filled global, so an image linked 64 KiB below a 4 GiB boundary
//                       crosses it
#include "mpt/mp4/game_abi.h"

#ifndef MPT_STUB_NO_EXPORT

namespace {

MptMp4StepResult stubStatus(const MptMp4Game*) { return MPT_MP4_STEP_RUNNING; }

MptMp4Api g_api = {};  // module data: its address shows where the module was mapped

#ifdef MPT_STUB_BIG_IMAGE
volatile unsigned char g_big[256 * 1024];  // zero-filled: part of the image size, not of the file
#endif

}  // namespace

extern "C" MPT_MP4_EXPORT const MptMp4Api* mpt_mp4_get_api(uint32_t abi_version) {
  if (abi_version != MPT_MP4_ABI_VERSION) return nullptr;
#ifdef MPT_STUB_BIG_IMAGE
  g_big[sizeof g_big - 1] = 1;  // referenced, so the linker keeps it
#endif
  g_api.struct_size = MPT_STUB_API_SIZE;
  g_api.abi_version = MPT_MP4_ABI_VERSION;
  g_api.build_id = MPT_STUB_API_SIZE >= MPT_MP4_API_SIZE_V1_1 ? "stub v1.1" : "stub v1.0";
  g_api.status = MPT_STUB_API_SIZE >= MPT_MP4_API_SIZE_V1_1 ? &stubStatus : nullptr;
  return &g_api;
}

#else

// Something to export, so the image is an ordinary DLL or shared object, just without the entry point.
extern "C" MPT_MP4_EXPORT int mpt_mp4_stub_without_get_api(void) { return 1; }

#endif
