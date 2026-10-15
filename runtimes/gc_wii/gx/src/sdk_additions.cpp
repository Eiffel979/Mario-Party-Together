// GX entry points that aurora lacks at the pin, with the semantics of ADR 0007 section 5 (P1-14).
// Front end: compiled into the game module with the other GX code.
//
// - GXSetDrawSync / GXSetDrawSyncCallback: the callback fires synchronously, inside GXSetDrawSync.
//   There is no GPU to wait for, so "the GPU reached the token" is immediate and deterministic.
// - GXWaitDrawDone, GXResetWriteGatherPipe: no-ops.
// - Performance metrics: always 0.
// - GXNtsc480Prog: the SDK's progressive NTSC render mode object. Values as in the pinned game source's
//   SDK (Party Board 98ac07f, libs/dolphin/src/gx/GXFrameBuf.c, CC0): tvmode 2 (NTSC progressive),
//   640x480 EFB and XFB, single-field XFB, no AA, the default sample pattern and vertical filter.
#include <dolphin/gx.h>
#include <dolphin/vi.h>

#include "gx/gx.hpp"
#include "mpt/gx/renderer.h"

namespace {
GXDrawSyncCallback s_drawSyncCallback = nullptr;
u16 s_lastDrawSyncToken = 0;
}  // namespace

extern "C" {

GXDrawSyncCallback GXSetDrawSyncCallback(GXDrawSyncCallback cb) {
  const GXDrawSyncCallback old = s_drawSyncCallback;
  s_drawSyncCallback = cb;
  return old;
}

void GXSetDrawSync(u16 token) {
  s_lastDrawSyncToken = token;
  if (s_drawSyncCallback != nullptr) {
    s_drawSyncCallback(token);
  }
}

u16 GXReadDrawSync(void) { return s_lastDrawSyncToken; }

void GXWaitDrawDone(void) {}
void GXResetWriteGatherPipe(void) {}

// Performance metrics (ADR 0007 section 5: they return 0). Enum parameters are passed as u32,
// which has the same calling convention as the SDK's enum types.
void GXSetGPMetric(u32 perf0, u32 perf1) {
  (void)perf0;
  (void)perf1;
}
void GXClearGPMetric(void) {}
void GXReadGPMetric(u32* cnt0, u32* cnt1) {
  if (cnt0 != nullptr) *cnt0 = 0;
  if (cnt1 != nullptr) *cnt1 = 0;
}
u32 GXReadGP0Metric(void) { return 0; }
u32 GXReadGP1Metric(void) { return 0; }
void GXReadMemMetric(u32* cp_req, u32* tc_req, u32* cpu_rd_req, u32* cpu_wr_req, u32* dsp_req, u32* io_req,
                     u32* vi_req, u32* pe_req, u32* rf_req, u32* fi_req) {
  for (u32* p : {cp_req, tc_req, cpu_rd_req, cpu_wr_req, dsp_req, io_req, vi_req, pe_req, rf_req, fi_req}) {
    if (p != nullptr) *p = 0;
  }
}
void GXClearMemMetric(void) {}
void GXReadPixMetric(u32* top_pixels_in, u32* top_pixels_out, u32* bot_pixels_in, u32* bot_pixels_out,
                     u32* clr_pixels_in, u32* copy_clks) {
  for (u32* p : {top_pixels_in, top_pixels_out, bot_pixels_in, bot_pixels_out, clr_pixels_in, copy_clks}) {
    if (p != nullptr) *p = 0;
  }
}
void GXClearPixMetric(void) {}
void GXSetVCacheMetric(u32 attr) { (void)attr; }
void GXReadVCacheMetric(u32* check, u32* miss, u32* stall) {
  for (u32* p : {check, miss, stall}) {
    if (p != nullptr) *p = 0;
  }
}
void GXClearVCacheMetric(void) {}
void GXReadXfRasMetric(u32* xf_wait_in, u32* xf_wait_out, u32* ras_busy, u32* clocks) {
  for (u32* p : {xf_wait_in, xf_wait_out, ras_busy, clocks}) {
    if (p != nullptr) *p = 0;
  }
}
void GXInitXfRasMetric(void) {}
u32 GXReadClksPerVtx(void) { return 0; }

GXRenderModeObj GXNtsc480Prog = {
    VI_TVMODE_NTSC_PROG,
    640,
    480,
    480,
    40,
    0,
    640,
    480,
    VI_XFBMODE_SF,
    0,
    0,
    {{6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}},
    {0, 0, 21, 22, 21, 0, 0},
};

}  // extern "C"

namespace mpt::gx {

ObjectIdCounters object_id_counters() {
  const auto c = aurora::gx::get_obj_id_counters();
  return {c.nextTexObjId, c.nextTlutObjId};
}

void set_object_id_counters(const ObjectIdCounters& counters) {
  aurora::gx::set_obj_id_counters({counters.next_tex_obj_id, counters.next_tlut_obj_id});
}

void set_copy_tex_observer(CopyTexObserver observer) { aurora::gx::g_copyTexObserver = observer; }

}  // namespace mpt::gx
