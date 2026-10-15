// The SDK's side of mpt_gc/layout_contract.h: its declarations of the structs it reads or writes in
// game memory must have the layout the game modules assert against the pinned aurora headers.
#include <cstddef>

#include "mpt_gc/ar.h"
#include "mpt_gc/card.h"
#include "mpt_gc/dvd.h"
#include "mpt_gc/os.h"
#include "mpt_gc/pad.h"

#define MPT_GC_LAYOUT_SIZE(type, size) \
  static_assert((size) == 0 || sizeof(type) == (size), "layout contract: sizeof(" #type ")");
#define MPT_GC_LAYOUT_FIELD(type, sdk_field, game_field, offset) \
  static_assert(offsetof(type, sdk_field) == (offset), "layout contract: " #type "." #sdk_field);
#include "mpt_gc/layout_contract.h"
#undef MPT_GC_LAYOUT_SIZE
#undef MPT_GC_LAYOUT_FIELD
