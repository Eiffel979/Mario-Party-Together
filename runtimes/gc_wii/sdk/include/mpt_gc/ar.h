/* ARAM (AR) and the ARAM queue (ARQ). Emulated ARAM is a region of the single
 * arena (ADR 0008 §7). DMA completes synchronously inside the call; callbacks
 * run at the next SDK call boundary (ADR 0007 §4).
 */
#ifndef MPT_GC_AR_H
#define MPT_GC_AR_H

#include "mpt_gc/types.h"

MPT_GC_EXTERN_C_BEGIN

#define ARAM_DIR_MRAM_TO_ARAM 0u
#define ARAM_DIR_ARAM_TO_MRAM 1u
/* The bottom of ARAM is reserved for the OS/DSP, as on the console. */
#define AR_RESERVED_BYTES 0x4000u

typedef void (*ARCallback)(void);

u32 ARInit(u32* stack_index_addr, u32 num_entries);
BOOL ARCheckInit(void);
void ARReset(void);
u32 ARAlloc(u32 length);
u32 ARFree(u32* length);
u32 ARGetBaseAddress(void);
u32 ARGetSize(void);
u32 ARGetInternalSize(void);
ARCallback ARRegisterDMACallback(ARCallback callback);
/* `mainmem_addr` is a host pointer into main RAM, passed as an integer like the SDK. */
void ARStartDMA(u32 type, uintptr_t mainmem_addr, u32 aram_addr, u32 length);
u32 ARGetDMAStatus(void);

#define ARQ_TYPE_MRAM_TO_ARAM ARAM_DIR_MRAM_TO_ARAM
#define ARQ_TYPE_ARAM_TO_MRAM ARAM_DIR_ARAM_TO_MRAM
#define ARQ_PRIORITY_LOW 0u
#define ARQ_PRIORITY_HIGH 1u

typedef struct ARQRequest ARQRequest;
typedef void (*ARQCallback)(uintptr_t request);

/* ARQPostRequest writes only next, owner, type and priority: the pinned aurora headers declare 32-bit
 * source and dest fields (a 40-byte request), so nothing past priority has the same place in the
 * game's struct. The transfer completes inside the call and the callback is kept in the event. */

struct ARQRequest {
  ARQRequest* next;
  u32 owner;
  u32 type;
  u32 priority;
  uintptr_t source;
  uintptr_t dest;
  u32 length;
  ARQCallback callback;
};

void ARQInit(void);
void ARQReset(void);
/* For MRAM_TO_ARAM `source` is a host pointer and `dest` an ARAM address; the other way round for ARAM_TO_MRAM. */
void ARQPostRequest(ARQRequest* request, u32 owner, u32 type, u32 priority, uintptr_t source, uintptr_t dest,
                    u32 length, ARQCallback callback);
void ARQRemoveRequest(ARQRequest* request);
void ARQRemoveOwnerRequest(u32 owner);
void ARQFlushQueue(void);
void ARQSetChunkSize(u32 size);
u32 ARQGetChunkSize(void);
BOOL ARQCheckInit(void);

MPT_GC_EXTERN_C_END

#endif /* MPT_GC_AR_H */
