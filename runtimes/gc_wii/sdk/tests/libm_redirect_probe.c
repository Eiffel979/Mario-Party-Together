/* Uses the transcendental functions musyx calls, through libm_redirect.h (the
 * forced include for musyx). check_no_host_math.cmake verifies this object has
 * no undefined host math symbol; test_math.cpp checks the results. */
#include <math.h>

#include "mpt_gc/libm_redirect.h"

#include <math.h> /* including again after the redirect must not undo it */

float mpt_gc_libm_probe_powf(float x, float y) { return powf(x, y); }
double mpt_gc_libm_probe_pow(double x, double y) { return pow(x, y); }
double mpt_gc_libm_probe_sin(double x) { return sin(x); }
double mpt_gc_libm_probe_cos(double x) { return cos(x); }
float mpt_gc_libm_probe_sinf(float x) { return sinf(x); }
