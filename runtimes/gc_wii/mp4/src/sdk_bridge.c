/* Calls into the SDK through its own declarations (the mpt_gc headers), for the few functions whose aurora
 * TARGET_PC declarations differ from the console SDK's. This file must not include aurora headers. */
#include "mpt_gc/ai.h"
#include "mpt_gc/card.h"

#include "mpt_mp4_port.h"

void mpt_mp4_CARDInit(void) { CARDInit(); }

/* AI DMA with host pointers: aurora declares 32-bit addresses, which would truncate them. */
void mpt_mp4_AIInitDMA(void* start, u32 length) { AIInitDMA((uintptr_t)start, length); }
void* mpt_mp4_AIGetDMAStartAddr(void) { return (void*)AIGetDMAStartAddr(); }
