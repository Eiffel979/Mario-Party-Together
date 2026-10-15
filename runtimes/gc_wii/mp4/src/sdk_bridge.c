/* Calls into the SDK through its own declarations (mpt_gc/*.h), for the few functions whose aurora
 * TARGET_PC declarations differ from the console SDK's. This file must not include aurora headers. */
#include "mpt_gc/card.h"

#include "mpt_mp4_port.h"

void mpt_mp4_CARDInit(void) { CARDInit(); }
