/* Forced-include header for aurora's GX front end and MTX (game-module code, ADR 0007 section 6,
 * ADR 0008 section 5 and its amendment after P1-13 section 4).
 *
 * - fmaf resolves to P1-13's correctly rounded mpt_fmaf (via gc_math_redirect.h), never the host
 *   CRT or a hardware FMA instruction. aurora's PSVEC reciprocal-square-root emulation uses it.
 * - sinf, cosf, tanf and acosf resolve to the SDK's deterministic float trig, not the host libm.
 *   (MSL math from the pinned game source replaces these in the game module in P1-15.)
 * - atan2f too. sqrtf stays the host's: IEEE 754 specifies it exactly (ADR 0008 section 5).
 *
 * Included first in every front-end and MTX translation unit (-include / /FI), after <math.h> and
 * <cmath>, so later includes cannot undo the macros. std::sinf(...) becomes std::mpt_sinf(...),
 * which the using-declarations below provide.
 */
#ifndef MPT_GX_MATH_REDIRECT_H
#define MPT_GX_MATH_REDIRECT_H

#include "mpt_gc/gc_math_redirect.h"

#ifdef __cplusplus
namespace std {
using ::mpt_acosf;
using ::mpt_atan2f;
using ::mpt_cosf;
using ::mpt_sinf;
using ::mpt_tanf;
}  // namespace std
#endif

#undef sinf
#undef cosf
#undef tanf
#undef acosf
#undef atan2f
#define sinf(x) mpt_sinf((x))
#define cosf(x) mpt_cosf((x))
#define tanf(x) mpt_tanf((x))
#define acosf(x) mpt_acosf((x))
#define atan2f(y, x) mpt_atan2f((y), (x))

#endif /* MPT_GX_MATH_REDIRECT_H */
