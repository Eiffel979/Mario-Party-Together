/* Forced-include header for C code linked into the game module that calls the
 * C math library directly: musyx (ADR 0008 §5 and ADR 0009, amendment after
 * P1-13b) and, since P1-15, the game code itself (runtimes/gc_wii/mp4 forces it
 * into every game C source through mpt_mp4_game_flags). Every transcendental
 * function such code uses resolves to our deterministic library, never to the
 * host CRT or glibc.
 *
 * Exactly specified functions (sqrt, fmod, floor, ceil, trunc, fabs, round,
 * lround, copysign, frexp, ldexp) keep the host versions, as ADR 0008 §5 allows.
 *
 * Only the functions the audited sources call are mapped; anything else falls
 * through to the host and is caught at link level by check_no_host_math.cmake,
 * which then needs a new mpt_ function here, with goldens. The game module's
 * own link check (runtimes/gc_wii/mp4) runs the same script over the game
 * objects.
 */
#ifndef MPT_GC_LIBM_REDIRECT_H
#define MPT_GC_LIBM_REDIRECT_H

#include "mpt_gc/gc_math_redirect.h"

/* No parentheses around the arguments: the macros must also rewrite
 * declarations such as `extern float powf(float, float);` (synth_adsr.c). */
#undef powf
#undef pow
#undef sin
#undef cos
#undef sinf
#undef cosf
#undef tanf
#undef acosf
#undef atan2f
#define powf(x, y) mpt_powf(x, y)
#define pow(x, y) mpt_pow(x, y)
#define sin(x) mpt_sin(x)
#define cos(x) mpt_cos(x)
#define sinf(x) mpt_sinf(x)
#define cosf(x) mpt_cosf(x)
#define tanf(x) mpt_tanf(x)
#define acosf(x) mpt_acosf(x)
#define atan2f(y, x) mpt_atan2f(y, x)
#undef atan2
#undef asinf
#undef atanf
#define atan2(y, x) mpt_atan2(y, x)
#define asinf(x) mpt_asinf(x)
#define atanf(x) mpt_atanf(x)

#endif /* MPT_GC_LIBM_REDIRECT_H */
