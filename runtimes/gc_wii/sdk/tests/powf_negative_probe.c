/* Negative probe for check_no_host_math.cmake: a plain powf call without the
 * redirect leaves an undefined host powf, which the check must report. Never linked. */
#include <math.h>

float mpt_gc_powf_negative_probe(float x, float y) { return powf(x, y); }
