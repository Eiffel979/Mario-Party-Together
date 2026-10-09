/* The deterministic math library (ADR 0008 §5).
 *
 * - mpt_fmaf: the one correctly rounded software fusedMultiplyAdd that every
 *   explicit fmaf in the game module resolves to (ADR 0008 §5 condition 1).
 *   It uses only IEEE double add and multiply (round-to-odd on an exact
 *   two-sum), so it needs no FMA instruction and gives the same bits on every
 *   x86-64 and ARM64 host built with -ffp-contract=off.
 * - mpt_sinf/cosf/tanf/acosf/atan2f/sqrtf: the float trig our own C matrix
 *   library uses. They are plain C evaluated in double under strict FP, so
 *   their results are platform independent. They are not the MSL math the
 *   game calls (that comes from the pinned game source, ADR 0009 §2).
 *
 * gc_math_redirect.h maps fmaf onto mpt_fmaf for code compiled into the game
 * module (a forced include in P1-15).
 */
#ifndef MPT_GC_GC_MATH_H
#define MPT_GC_GC_MATH_H

#include "mpt_gc/types.h"

MPT_GC_EXTERN_C_BEGIN

f32 mpt_fmaf(f32 a, f32 b, f32 c);

f32 mpt_sinf(f32 x);
f32 mpt_cosf(f32 x);
f32 mpt_tanf(f32 x);
f32 mpt_acosf(f32 x);
f32 mpt_atan2f(f32 y, f32 x);
f32 mpt_sqrtf(f32 x);

MPT_GC_EXTERN_C_END

#endif /* MPT_GC_GC_MATH_H */
