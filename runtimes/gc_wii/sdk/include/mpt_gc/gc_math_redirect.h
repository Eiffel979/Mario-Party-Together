/* Forced-include header for code linked into the game module (P1-15): every
 * explicit fmaf resolves to our one software implementation, never to the host
 * CRT or a per-machine FMA instruction (ADR 0008 §5 condition 1).
 *
 * It includes <math.h> (and <cmath> in C++) itself, before defining the macro, so:
 *  - forced first (-include / /FI), it does not break math.h's own declaration of
 *    fmaf, and later includes of <math.h>/<cmath> are no-ops behind their guards,
 *    so nothing can #undef the macro afterwards;
 *  - std::fmaf is covered too: <cmath> declares it before the macro exists, and
 *    the macro then turns std::fmaf(...) into std::mpt_fmaf(...), which the using
 *    declaration below provides.
 * check_no_host_fma.cmake verifies the result at link level: no object may leave
 * fma or fmaf undefined.
 */
#ifndef MPT_GC_GC_MATH_REDIRECT_H
#define MPT_GC_GC_MATH_REDIRECT_H

#include <math.h>
#ifdef __cplusplus
#include <cmath>
#endif

#include "mpt_gc/gc_math.h"

#ifdef __cplusplus
namespace std {
using ::mpt_fmaf;
}  // namespace std
#endif

#undef fmaf
#define fmaf(a, b, c) mpt_fmaf((a), (b), (c))

#endif /* MPT_GC_GC_MATH_REDIRECT_H */
