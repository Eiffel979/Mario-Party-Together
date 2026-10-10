// Negative probe for check_no_host_fma.cmake: a plain fmaf call without the redirect
// header leaves an undefined fmaf, which the check must report. Never linked.
#include <math.h>

extern "C" float mpt_gc_fma_negative_probe(float a, float b, float c) { return fmaf(a, b, c); }
