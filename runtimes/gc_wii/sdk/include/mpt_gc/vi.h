/* VI: video timing. One frame step is one VI field (ADR 0008 §1). The retrace
 * interrupt is frame-driven: it fires once at the start of every frame step
 * after the first, at virtual time epoch + n * field period (60000/1001 Hz for
 * NTSC), never because a time query ran past it.
 */
#ifndef MPT_GC_VI_H
#define MPT_GC_VI_H

#include "mpt_gc/types.h"

MPT_GC_EXTERN_C_BEGIN

#define VI_NTSC 0
#define VI_PAL 1
#define VI_MPAL 2
#define VI_EURGB60 5

#define VI_FIELD_ABOVE 0
#define VI_FIELD_BELOW 1

typedef void (*VIRetraceCallback)(u32 retraceCount);

/* Fields the SDK reads from a render mode. Layout as in the public SDK. */
typedef struct GXRenderModeObj {
  u32 viTVmode;
  u16 fbWidth;
  u16 efbHeight;
  u16 xfbHeight;
  u16 viXOrigin;
  u16 viYOrigin;
  u16 viWidth;
  u16 viHeight;
  u32 xFBmode;
  u8 field_rendering;
  u8 aa;
  u8 sample_pattern[12][2];
  u8 vfilter[7];
} GXRenderModeObj;

void VIInit(void);
void VIConfigure(const GXRenderModeObj* rm);
void VIConfigurePan(u16 xOrg, u16 yOrg, u16 width, u16 height);
void VIFlush(void);
/* Blocks the calling thread until the next retrace (the next frame step). */
void VIWaitForRetrace(void);
u32 VIGetRetraceCount(void);
VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback callback);
VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback callback);
void VISetNextFrameBuffer(void* fb);
void* VIGetNextFrameBuffer(void);
void* VIGetCurrentFrameBuffer(void);
void VISetBlack(BOOL black);
u32 VIGetNextField(void);
u32 VIGetCurrentLine(void);
u32 VIGetTvFormat(void);
u32 VIGetDTVStatus(void);

MPT_GC_EXTERN_C_END

#endif /* MPT_GC_VI_H */
