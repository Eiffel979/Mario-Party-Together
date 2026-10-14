/* Forced include for every musyx translation unit we compile.
 *
 * 1. The libm redirect: musyx's transcendental calls (powf, sin, cos) resolve to
 *    our deterministic math library (ADR 0008 §5, ADR 0009 amendment after P1-13b).
 * 2. musyx keeps its whole state in C statics. These pragmas put every writable
 *    global and static of these units into two dedicated sections, which
 *    musyx_state_markers.c brackets, so a save state can capture exactly
 *    musyx's state as registered globals (SdkConfig::state_regions). Constant
 *    data stays in read-only sections.
 *    - COFF: grouped sections ".mpxd$m" (initialized) and ".mpxb$m" (zero), sorted
 *      between the "$a" and "$z" markers by the linker. Data and bss need separate
 *      groups: the linker moves uninitialized contributions to the end of a section.
 *    - ELF: sections "mpt_musyx_data" and "mpt_musyx_bss", bounded by the
 *      linker-provided __start_ and __stop_ symbols.
 */
#ifndef MPT_MUSYX_STATE_SECTION_H
#define MPT_MUSYX_STATE_SECTION_H

#include "mpt_gc/libm_redirect.h"

#if defined(_WIN32)
#pragma clang section data = ".mpxd$m" bss = ".mpxb$m"
#else
#pragma clang section data = "mpt_musyx_data" bss = "mpt_musyx_bss"
#endif

#endif /* MPT_MUSYX_STATE_SECTION_H */
