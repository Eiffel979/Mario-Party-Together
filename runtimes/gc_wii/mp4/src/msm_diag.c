/* msm's group pushes with a reason when MusyX refuses one (P1-16 dump-lane diagnostics). msm only
 * reports MSM_ERR_GRP_FAILPUSH; this says why, through OSReport, and changes nothing else. */
#include <stdint.h>

#include "dolphin.h"
#include "musyx/musyx.h"
#include "musyx/pc.h"

#include "mpt_msm_swap.h"

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
    OSReport("msm: sndPushGroup(%u) failed: the data validates; group missing, stack full or out of memory\n",
             (unsigned)gid);
  }
  return false;
}
