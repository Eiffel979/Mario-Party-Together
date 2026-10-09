// Uses fmaf and std::fmaf the way game code will, through the redirect header
// (forced-included in the game module, P1-15). check_no_host_fma.cmake verifies this
// object has no undefined fmaf; test_math.cpp checks the results.
#include <cmath>
#include <math.h>

#include "mpt_gc/gc_math_redirect.h"

#include <cmath>  // including again after the redirect must not undo it

extern "C" float mpt_gc_redirect_probe_c(float a, float b, float c) { return fmaf(a, b, c); }
extern "C" float mpt_gc_redirect_probe_std(float a, float b, float c) { return std::fmaf(a, b, c); }
