/* PAD: the four GameCube controller ports, fed from the frame's InputFrame.
 * The InputFrame is latched at the VI post-retrace point of each frame step
 * (ADR 0007 §4); PADRead returns the latched state until the next retrace.
 */
#ifndef MPT_GC_PAD_H
#define MPT_GC_PAD_H

#include "mpt_gc/types.h"

MPT_GC_EXTERN_C_BEGIN

#define PAD_MAX_CONTROLLERS 4

#define PAD_BUTTON_LEFT 0x0001u
#define PAD_BUTTON_RIGHT 0x0002u
#define PAD_BUTTON_DOWN 0x0004u
#define PAD_BUTTON_UP 0x0008u
#define PAD_TRIGGER_Z 0x0010u
#define PAD_TRIGGER_R 0x0020u
#define PAD_TRIGGER_L 0x0040u
#define PAD_USE_ORIGIN 0x0080u
#define PAD_BUTTON_A 0x0100u
#define PAD_BUTTON_B 0x0200u
#define PAD_BUTTON_X 0x0400u
#define PAD_BUTTON_Y 0x0800u
#define PAD_BUTTON_MENU 0x1000u
#define PAD_BUTTON_START PAD_BUTTON_MENU

#define PAD_ERR_NONE 0
#define PAD_ERR_NO_CONTROLLER -1
#define PAD_ERR_NOT_READY -2
#define PAD_ERR_TRANSFER -3

#define PAD_CHAN0_BIT 0x80000000u
#define PAD_CHAN1_BIT 0x40000000u
#define PAD_CHAN2_BIT 0x20000000u
#define PAD_CHAN3_BIT 0x10000000u

#define PAD_MOTOR_STOP 0u
#define PAD_MOTOR_RUMBLE 1u
#define PAD_MOTOR_STOP_HARD 2u

/* The layout of the pinned aurora headers that game code compiles against (ADR 0007 section 6): the
 * console's 12 bytes plus aurora's extButton, so PADRead's array has a 16-byte stride. The MP4 module
 * asserts it against those headers. */
typedef struct PADStatus {
  u16 button;
  s8 stickX;
  s8 stickY;
  s8 substickX;
  s8 substickY;
  u8 triggerL;
  u8 triggerR;
  u8 analogA;
  u8 analogB;
  s8 err;
  u32 extButton; /* aurora extension; always 0 */
} PADStatus;

BOOL PADInit(void);
/* Fills four statuses. Returns the PAD_CHANn_BIT mask of connected ports. */
u32 PADRead(PADStatus* status);
/* The SDK's octagonal stick clamp and trigger clamp. */
void PADClamp(PADStatus* status);
void PADClampCircle(PADStatus* status);
BOOL PADReset(u32 mask);
BOOL PADRecalibrate(u32 mask);
void PADControlMotor(s32 chan, u32 command);
void PADSetSpec(u32 spec);
void PADSetAnalogMode(u32 mode);
BOOL PADSync(void);

MPT_GC_EXTERN_C_END

#endif /* MPT_GC_PAD_H */
