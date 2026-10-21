/* msm's group pushes with a reason when MusyX refuses one (P1-16 dump-lane diagnostics). msm only
 * reports MSM_ERR_GRP_FAILPUSH; this says why, through OSReport. The headroom probe allocates and frees
 * again, so msm's heap is left as it was. */
#include <stdint.h>

#include "dolphin.h"
#include "msm/msmmem.h"
#include "musyx/musyx.h"
#include "musyx/pc.h"
#include "musyx/synthdata.h"

#include "mpt_msm_swap.h"

/* The largest block msm's heap can give right now. */
static u32 largest_free(void) {
  u32 lo = 0, hi = 64u << 20;
  while (lo < hi) {
    const u32 mid = lo + (hi - lo + 1) / 2;
    void* p = msmMemAlloc(mid);
    if (p != NULL) {
      msmMemFree(p);
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

static u32 be32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static u16 be16(const u8* p) { return (u16)(((u16)p[0] << 8) | p[1]); }

/* Whether `id` is in one of the pool's big-endian MEM_DATA lists (POOL_DATA offset at `which`). */
static int in_pool(const u8* pool, int which, u16 id) {
  const u32 list = be32(pool + 4 * which);
  if (list == 0) return 0;
  const u8* m = pool + list;
  for (int guard = 0; guard < 65536; ++guard) {
    const u32 next = be32(m);
    if (next == 0xFFFFFFFFu) return 0;
    if (be16(m + 4) == id) return 1;
    m += next;
  }
  return 0;
}

/* The ids group `gid` names that are neither in its pool nor registered already: what MusyX's PC
 * loader refuses (pcPrepareRecord), where the console's InsertMacros/InsertCurves/... register NULL. */
static void report_missing(const u8* prj, const u8* pool, u16 gid) {
  u32 off = 0;
  for (int n = 0; n < 4096; ++n) {
    const u8* g = prj + off;
    if (be16(g + 4) == gid) break;
    const u32 next = be32(g);
    if (next == 0xFFFFFFFFu) return;
    off = next;
  }
  const u8* g = prj + off;
  /* GROUP_DATA: macroOff 0x8, curveOff 0x10, keymapOff 0x14, layerOff 0x18; POOL_DATA order:
   * macros 0, curves 1, keymaps 2, layers 3. */
  static const struct { u32 field; int pool; u16 flag; const char* name; } kinds[] = {
      {0x08, 0, 0x0000, "macro"}, {0x10, 1, 0x0000, "curve"}, {0x14, 2, 0x4000, "keymap"}, {0x18, 3, 0x8000, "layer"}};
  int reported = 0;
  for (int k = 0; k < 4; ++k) {
    const u8* list = prj + be32(g + kinds[k].field);
    for (int guard = 0; guard < 4096; ++guard) {
      u16 first = be16(list), last = first;
      if (first == 0xFFFF) break;
      if (first & 0x8000) {
        first &= 0x3FFF;
        list += 2;
        last = be16(list);
      }
      list += 2;
      for (u32 id = first; id <= last; ++id) {
        const u16 full = (u16)(id | kinds[k].flag);
        u16 count = 0;
        const void* existing = k == 0 ? (const void*)dataGetMacro(full)
                               : k == 1 ? dataGetCurve(full)
                               : k == 2 ? dataGetKeymap(full)
                                        : dataGetLayer(full, &count);
        if (!existing && !in_pool(pool, kinds[k].pool, full) && reported < 8) {
          OSReport("msm:   group %u names %s %u, which is in no pool and not registered\n", (unsigned)gid,
                   kinds[k].name, (unsigned)id);
          ++reported;
        }
      }
    }
  }
  if (reported == 0) OSReport("msm:   every macro, curve, keymap and layer of group %u resolves\n", (unsigned)gid);
}

/* The sample ids group `gid` names that its own (big-endian, 0x20-byte entries) sample directory lacks.
 * MusyX's PC loader refuses such a group unless the sample is already resident; the console's
 * ScanIDList skips it. */
static void report_missing_samples(const u8* prj, const u8* sdir, u16 gid) {
  u32 off = 0;
  for (int n = 0; n < 4096; ++n) {
    if (be16(prj + off + 4) == gid) break;
    const u32 next = be32(prj + off);
    if (next == 0xFFFFFFFFu) return;
    off = next;
  }
  const u8* list = prj + be32(prj + off + 0x0C); /* GROUP_DATA.sampleOff */
  int total = 0, missing = 0;
  for (int guard = 0; guard < 4096; ++guard) {
    u16 first = be16(list), last = first;
    if (first == 0xFFFF) break;
    if (first & 0x8000) {
      first &= 0x3FFF;
      list += 2;
      last = be16(list);
    }
    list += 2;
    for (u32 id = first; id <= last; ++id) {
      ++total;
      int found = 0;
      for (const u8* e = sdir; be16(e) != 0xFFFF; e += 0x20) {
        if (be16(e) == id) {
          found = 1;
          break;
        }
      }
      if (!found && ++missing <= 8)
        OSReport("msm:   group %u names sample %u, which its sample directory lacks\n", (unsigned)gid, (unsigned)id);
    }
  }
  OSReport("msm:   group %u: %d samples named, %d not in its directory\n", (unsigned)gid, total, missing);
}

bool mpt_msm_push_group(void* project, u16 gid, void* samples, void* sdir, void* pool) {
  if (sndPushGroup(project, gid, samples, sdir, pool)) return true;
  if (!sndIsInstalled()) {
    OSReport("msm: sndPushGroup(%u) failed: MusyX is not initialised\n", (unsigned)gid);
    return false;
  }
  SND_PC_GROUP_ASSETS assets = {{project, SIZE_MAX}, {pool, SIZE_MAX}, {sdir, SIZE_MAX}, {samples, SIZE_MAX}};
  SND_PC_ASSET_ERROR error = {0, 0, 0};
  if (!sndPCValidateGroup(&assets, &error)) {
    OSReport("msm: sndPushGroup(%u) failed: %s at offset 0x%x: %s\n", (unsigned)gid,
             error.section ? error.section : "?", (unsigned)error.offset, error.reason ? error.reason : "?");
  } else {
    OSReport("msm: sndPushGroup(%u) failed: the data validates; group missing, stack full or out of memory "
             "(largest free msm block %u bytes)\n",
             (unsigned)gid, (unsigned)largest_free());
    report_missing((const u8*)project, (const u8*)pool, gid);
    report_missing_samples((const u8*)project, (const u8*)sdir, gid);
  }
  return false;
}
