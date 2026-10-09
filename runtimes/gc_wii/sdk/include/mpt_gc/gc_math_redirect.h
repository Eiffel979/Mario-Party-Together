/* Forced-include header for code linked into the game module (P1-15).
 * Every explicit fmaf resolves to our one software implementation, never to the
 * host CRT or a per-machine FMA instruction (ADR 0008 §5 condition 1).
 * Include it after <math.h> or force it with -include / /FI.
 */
#ifndef MPT_GC_GC_MATH_REDIRECT_H
#define MPT_GC_GC_MATH_REDIRECT_H

#include "mpt_gc/gc_math.h"

#define fmaf(a, b, c) mpt_fmaf((a), (b), (c))

#endif /* MPT_GC_GC_MATH_REDIRECT_H */
