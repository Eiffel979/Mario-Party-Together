/* The game's side of the SDK's layout contract (mpt_gc/layout_contract.h): compiled with the pinned
 * aurora headers the game uses, so a struct the SDK writes cannot differ from the game's view of it. */
#include <stddef.h>

#include "dolphin.h"
#include "dolphin/ar.h"
#include "dolphin/card.h"
#include "dolphin/dvd.h"
#include "dolphin/os.h"
#include "dolphin/pad.h"

#define MPT_GC_LAYOUT_SIZE(type, size) \
  _Static_assert((size) == 0 || sizeof(type) == (size), "layout contract: sizeof(" #type ")");
#define MPT_GC_LAYOUT_FIELD(type, sdk_field, game_field, offset) \
  _Static_assert(offsetof(type, game_field) == (offset), "layout contract: " #type "." #game_field);
#include "mpt_gc/layout_contract.h"
#undef MPT_GC_LAYOUT_SIZE
#undef MPT_GC_LAYOUT_FIELD
