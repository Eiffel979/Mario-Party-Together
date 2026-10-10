// ARAM in the arena. DMA is a synchronous copy; callbacks are interrupts.
#include <cstring>

#include "sdk_internal.h"

using namespace mpt::gc;
using namespace mpt::gc::detail;

namespace {

void copy(u32 type, uintptr_t mainmem, u32 aram_addr, u32 length, const char* what) {
  if (std::uint64_t{aram_addr} + length > kAramBytes) fatal(std::string(what) + ": ARAM address out of range");
  if (length == 0) return;
  auto* ram = reinterpret_cast<std::byte*>(mainmem);
  std::byte* ar = impl().aramBase() + aram_addr;
  if (type == ARAM_DIR_MRAM_TO_ARAM) {
    std::memmove(ar, ram, length);
  } else if (type == ARAM_DIR_ARAM_TO_MRAM) {
    std::memmove(ram, ar, length);
  } else {
    fatal(std::string(what) + ": bad DMA direction");
  }
}

}  // namespace

namespace mpt::gc::detail {

void arDispatch(const Event& e) {
  if (e.kind == EventKind::ar_dma) {
    reinterpret_cast<ARCallback>(e.b)();
  } else {
    reinterpret_cast<ARQCallback>(e.b)(reinterpret_cast<uintptr_t>(e.a));
  }
}

}  // namespace mpt::gc::detail

extern "C" {

u32 ARInit(u32* stack_index_addr, u32 num_entries) {
  (void)stack_index_addr;
  (void)num_entries;
  boundary();
  auto& s = st();
  if (!s.ar_initialized) {
    s.ar_initialized = 1;
    s.ar_top = AR_RESERVED_BYTES;
    s.ar_stack_depth = 0;
  }
  return AR_RESERVED_BYTES;
}

BOOL ARCheckInit(void) {
  boundary();
  return st().ar_initialized;
}

void ARReset(void) {
  boundary();
  st().ar_initialized = 0;
}

u32 ARAlloc(u32 length) {
  boundary();
  auto& s = st();
  if (!s.ar_initialized) fatal("ARAlloc before ARInit");
  if (s.ar_stack_depth >= kMaxArStack) fatal("ARAlloc: allocation stack full");
  if (std::uint64_t{s.ar_top} + length > kAramBytes) fatal("ARAlloc: ARAM exhausted");
  const u32 addr = s.ar_top;
  s.ar_stack[s.ar_stack_depth++] = length;
  s.ar_top += length;
  return addr;
}

u32 ARFree(u32* length) {
  boundary();
  auto& s = st();
  if (s.ar_stack_depth == 0) fatal("ARFree with nothing allocated");
  const u32 len = s.ar_stack[--s.ar_stack_depth];
  s.ar_stack[s.ar_stack_depth] = 0;
  s.ar_top -= len;
  if (length) *length = len;
  return s.ar_top;
}

u32 ARGetBaseAddress(void) {
  boundary();
  return AR_RESERVED_BYTES;
}

u32 ARGetSize(void) {
  boundary();
  return static_cast<u32>(kAramBytes);
}

u32 ARGetInternalSize(void) {
  boundary();
  return static_cast<u32>(kAramBytes);
}

ARCallback ARRegisterDMACallback(ARCallback callback) {
  boundary();
  auto& s = st();
  ARCallback old = s.ar_dma_callback;
  s.ar_dma_callback = callback;
  return old;
}

void ARStartDMA(u32 type, uintptr_t mainmem_addr, u32 aram_addr, u32 length) {
  boundary();
  copy(type, mainmem_addr, aram_addr, length, "ARStartDMA");
  if (ARCallback cb = st().ar_dma_callback)
    completeIo(Source::aram, EventKind::ar_dma, nullptr, reinterpret_cast<void*>(cb), 0);
}

u32 ARGetDMAStatus(void) {
  boundary();
  return 0;  // never busy
}

void ARQInit(void) {
  boundary();
  st().arq_initialized = 1;
}

void ARQReset(void) {
  boundary();
  st().arq_initialized = 0;
}

void ARQPostRequest(ARQRequest* request, u32 owner, u32 type, u32 priority, uintptr_t source, uintptr_t dest,
                    u32 length, ARQCallback callback) {
  boundary();
  request->next = nullptr;
  request->owner = owner;
  request->type = type;
  request->priority = priority;
  request->source = source;
  request->dest = dest;
  request->length = length;
  request->callback = callback;
  if (type == ARQ_TYPE_MRAM_TO_ARAM) {
    copy(ARAM_DIR_MRAM_TO_ARAM, source, static_cast<u32>(dest), length, "ARQPostRequest");
  } else {
    copy(ARAM_DIR_ARAM_TO_MRAM, dest, static_cast<u32>(source), length, "ARQPostRequest");
  }
  if (callback) completeIo(Source::aram, EventKind::arq, request, reinterpret_cast<void*>(callback), 0);
}

void ARQRemoveRequest(ARQRequest* request) {
  boundary();
  cancelEvents(EventKind::arq, request);
}

void ARQRemoveOwnerRequest(u32 owner) {
  boundary();
  for (Event& e : st().events)
    if (e.used && e.kind == EventKind::arq && static_cast<ARQRequest*>(e.a)->owner == owner)
      cancelEvents(EventKind::arq, e.a);
}

void ARQFlushQueue(void) { boundary(); }

void ARQSetChunkSize(u32 size) {
  boundary();
  st().arq_chunk_size = size;
}

u32 ARQGetChunkSize(void) {
  boundary();
  return st().arq_chunk_size;
}

BOOL ARQCheckInit(void) {
  boundary();
  return st().arq_initialized;
}

}  // extern "C"
