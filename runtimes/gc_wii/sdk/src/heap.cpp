// OS arena bounds and the OSAlloc heaps, all inside the single arena (ADR 0008 §7).
//
// Block headers sit in game memory like the SDK's, but link by arena offset
// instead of pointer, so the arena's bytes do not depend on where it is mapped.
#include <cstring>

#include "sdk_internal.h"

using namespace mpt::gc;
using namespace mpt::gc::detail;

extern "C" volatile OSHeapHandle __OSCurrentHeap = -1;

namespace {

constexpr std::uint32_t kAlign = 32;
constexpr std::uint32_t kHeader = 32;

struct BlockHeader {
  std::uint32_t prev;
  std::uint32_t next;
  std::uint32_t size;  // including the header
  std::uint32_t pad[5];
};
static_assert(sizeof(BlockHeader) == kHeader);

std::uint32_t roundUp(std::uint32_t v, std::uint32_t a) { return (v + a - 1) & ~(a - 1); }
std::uint32_t roundDown(std::uint32_t v, std::uint32_t a) { return v & ~(a - 1); }

BlockHeader* block(std::uint32_t off) { return reinterpret_cast<BlockHeader*>(impl().arena + off); }

std::uint32_t arenaOffset(const void* p, const char* what) {
  if (!impl().inArena(p)) fatal(std::string(what) + ": address outside the SDK arena");
  return impl().offsetOf(p);
}

HeapSlot& heapFor(OSHeapHandle h) {
  auto& s = st();
  if (h < 0 || h >= s.heap_count || !s.heaps[h].active) fatal("invalid OS heap handle");
  return s.heaps[h];
}

void unlink(std::uint32_t& head, std::uint32_t off) {
  BlockHeader* b = block(off);
  if (b->prev) block(b->prev)->next = b->next;
  else head = b->next;
  if (b->next) block(b->next)->prev = b->prev;
  b->prev = b->next = 0;
}

void pushFront(std::uint32_t& head, std::uint32_t off) {
  BlockHeader* b = block(off);
  b->prev = 0;
  b->next = head;
  if (head) block(head)->prev = off;
  head = off;
}

// Inserts into the address-ordered free list and merges with neighbours.
void insertFree(HeapSlot& heap, std::uint32_t off) {
  std::uint32_t prev = 0, cur = heap.free_head;
  while (cur && cur < off) {
    prev = cur;
    cur = block(cur)->next;
  }
  BlockHeader* b = block(off);
  b->prev = prev;
  b->next = cur;
  if (prev) block(prev)->next = off;
  else heap.free_head = off;
  if (cur) block(cur)->prev = off;
  if (cur && off + b->size == cur) {
    b->size += block(cur)->size;
    unlink(heap.free_head, cur);
    b->next = block(off)->next;
  }
  if (prev && prev + block(prev)->size == off) {
    block(prev)->size += b->size;
    unlink(heap.free_head, off);
  }
}

// Removes [lo, hi) from a free block, keeping the pieces on either side.
void carveFree(HeapSlot& heap, std::uint32_t off, std::uint32_t lo, std::uint32_t hi) {
  const std::uint32_t end = off + block(off)->size;
  unlink(heap.free_head, off);
  if (lo > off && lo - off >= kHeader + kAlign) {
    block(off)->size = lo - off;
    insertFree(heap, off);
  }
  if (end > hi && end - hi >= kHeader + kAlign) {
    BlockHeader* tail = block(hi);
    std::memset(tail, 0, sizeof *tail);
    tail->size = end - hi;
    insertFree(heap, hi);
  }
}

}  // namespace

namespace mpt::gc::detail {

void osSyncGlobalsFromState() { __OSCurrentHeap = st().current_heap; }

void heapHash(StateHasher& h) {
  auto& s = st();
  h.update_u64(static_cast<std::uint64_t>(s.heap_count));
  for (std::int32_t i = 0; i < s.heap_count && i < static_cast<std::int32_t>(kMaxHeaps); ++i) {
    const HeapSlot& heap = s.heaps[i];
    h.update_u64(static_cast<std::uint64_t>(heap.active));
    if (!heap.active) continue;
    h.update_u64(heap.start);
    h.update_u64(heap.end);
    for (std::uint32_t off = heap.free_head; off; off = block(off)->next) {
      h.update_u64(off);
      h.update_u64(block(off)->size);
    }
    h.update_u64(0xF7EEull);
    for (std::uint32_t off = heap.alloc_head; off; off = block(off)->next) {
      h.update_u64(off);
      h.update_u64(block(off)->size);
    }
  }
}

}  // namespace mpt::gc::detail

extern "C" {

void* OSGetArenaLo(void) {
  boundary();
  return impl().arena + st().arena_lo;
}

void* OSGetArenaHi(void) {
  boundary();
  return impl().arena + st().arena_hi;
}

void OSSetArenaLo(void* addr) {
  boundary();
  st().arena_lo = arenaOffset(addr, "OSSetArenaLo");
}

void OSSetArenaHi(void* addr) {
  boundary();
  st().arena_hi = arenaOffset(addr, "OSSetArenaHi");
}

void* OSAllocFromArenaLo(u32 size, u32 align) {
  boundary();
  auto& s = st();
  if (align == 0 || (align & (align - 1))) fatal("OSAllocFromArenaLo: alignment must be a power of two");
  const std::uint32_t lo = roundUp(s.arena_lo, align);
  const std::uint32_t next = roundUp(lo + size, align);
  if (next > s.arena_hi) fatal("OSAllocFromArenaLo: OS arena exhausted");
  s.arena_lo = next;
  return impl().arena + lo;
}

void* OSAllocFromArenaHi(u32 size, u32 align) {
  boundary();
  auto& s = st();
  if (align == 0 || (align & (align - 1))) fatal("OSAllocFromArenaHi: alignment must be a power of two");
  if (s.arena_hi < size) fatal("OSAllocFromArenaHi: OS arena exhausted");
  const std::uint32_t hi = roundDown(s.arena_hi - size, align);
  if (hi < s.arena_lo) fatal("OSAllocFromArenaHi: OS arena exhausted");
  s.arena_hi = hi;
  return impl().arena + hi;
}

void* OSInitAlloc(void* arenaStart, void* arenaEnd, int maxHeaps) {
  boundary();
  auto& s = st();
  if (maxHeaps <= 0 || maxHeaps > static_cast<int>(kMaxHeaps)) fatal("OSInitAlloc: maxHeaps out of range (1..16)");
  const std::uint32_t start = arenaOffset(arenaStart, "OSInitAlloc");
  const std::uint32_t end = arenaOffset(static_cast<std::byte*>(arenaEnd) - 1, "OSInitAlloc") + 1;
  // The console keeps a 12-byte descriptor per heap at the start of the range;
  // ours live in SdkState, but the same space is consumed so layouts match.
  const std::uint32_t next = roundUp(start + 12u * static_cast<std::uint32_t>(maxHeaps), kAlign);
  if (next > end) fatal("OSInitAlloc: range too small");
  for (HeapSlot& h : s.heaps) h = HeapSlot{};
  s.heap_count = maxHeaps;
  s.current_heap = -1;
  osSyncGlobalsFromState();
  return impl().arena + next;
}

OSHeapHandle OSCreateHeap(void* start, void* end) {
  boundary();
  auto& s = st();
  const std::uint32_t lo = roundUp(arenaOffset(start, "OSCreateHeap"), kAlign);
  const std::uint32_t hi = roundDown(arenaOffset(static_cast<std::byte*>(end) - 1, "OSCreateHeap") + 1, kAlign);
  if (hi <= lo + kHeader) return -1;
  for (std::int32_t i = 0; i < s.heap_count; ++i) {
    HeapSlot& h = s.heaps[i];
    if (h.active) continue;
    h = HeapSlot{1, lo, hi, lo, 0};
    BlockHeader* b = block(lo);
    std::memset(b, 0, sizeof *b);
    b->size = hi - lo;
    return i;
  }
  return -1;
}

void OSDestroyHeap(OSHeapHandle heap) {
  boundary();
  heapFor(heap) = HeapSlot{};
}

OSHeapHandle OSSetCurrentHeap(OSHeapHandle heap) {
  boundary();
  auto& s = st();
  const OSHeapHandle old = s.current_heap;
  s.current_heap = heap;
  osSyncGlobalsFromState();
  return old;
}

void* OSAllocFromHeap(OSHeapHandle heap, u32 size) {
  boundary();
  HeapSlot& h = heapFor(heap);
  const std::uint32_t need = roundUp(size, kAlign) + kHeader;
  for (std::uint32_t off = h.free_head; off; off = block(off)->next) {
    BlockHeader* b = block(off);
    if (b->size < need) continue;
    if (b->size - need >= kHeader + kAlign) {
      const std::uint32_t rest = off + need;
      BlockHeader* r = block(rest);
      std::memset(r, 0, sizeof *r);
      r->size = b->size - need;
      b->size = need;
      unlink(h.free_head, off);
      insertFree(h, rest);
    } else {
      unlink(h.free_head, off);
    }
    pushFront(h.alloc_head, off);
    return impl().arena + off + kHeader;
  }
  return nullptr;
}

void OSFreeToHeap(OSHeapHandle heap, void* ptr) {
  boundary();
  if (!ptr) return;
  HeapSlot& h = heapFor(heap);
  const std::uint32_t off = arenaOffset(ptr, "OSFreeToHeap") - kHeader;
  bool found = false;
  for (std::uint32_t a = h.alloc_head; a; a = block(a)->next) {
    if (a == off) {
      found = true;
      break;
    }
  }
  if (!found) fatal("OSFreeToHeap: pointer was not allocated from this heap");
  unlink(h.alloc_head, off);
  insertFree(h, off);
}

void* OSAllocFixed(void** rstart, void** rend) {
  boundary();
  auto& s = st();
  const std::uint32_t lo = roundUp(arenaOffset(*rstart, "OSAllocFixed"), kAlign);
  const std::uint32_t hi = roundDown(arenaOffset(static_cast<std::byte*>(*rend) - 1, "OSAllocFixed") + 1, kAlign);
  if (hi <= lo) return nullptr;
  // Refuse if any allocated block overlaps the range.
  for (std::int32_t i = 0; i < s.heap_count; ++i) {
    const HeapSlot& h = s.heaps[i];
    if (!h.active) continue;
    for (std::uint32_t a = h.alloc_head; a; a = block(a)->next)
      if (a < hi && a + block(a)->size > lo) return nullptr;
  }
  for (std::int32_t i = 0; i < s.heap_count; ++i) {
    HeapSlot& h = s.heaps[i];
    if (!h.active) continue;
    std::uint32_t off = h.free_head;
    while (off) {
      const std::uint32_t next = block(off)->next;
      if (off < hi && off + block(off)->size > lo) carveFree(h, off, lo, hi);
      off = next;
    }
  }
  *rstart = impl().arena + lo;
  *rend = impl().arena + hi;
  return *rstart;
}

s32 OSCheckHeap(OSHeapHandle heap) {
  boundary();
  HeapSlot& h = heapFor(heap);
  s32 free_bytes = 0;
  for (std::uint32_t off = h.free_head; off; off = block(off)->next) free_bytes += static_cast<s32>(block(off)->size);
  return free_bytes;
}

}  // extern "C"
