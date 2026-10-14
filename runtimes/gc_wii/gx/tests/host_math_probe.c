/* Negative probe for check_no_host_math.cmake: plain host libm calls, without the math redirect.
 * The check must fail on this object (test gx_no_host_math_detects_host_calls). */
#include <math.h>

float mpt_gx_host_math_probe(float x) { return sinf(x) + (float)log2((double)x); }
