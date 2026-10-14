/* Negative probe for check_no_host_math.cmake: a host log2 (the leak the P1-14
 * gate found on Linux) must be reported. Never linked. */
#include <math.h>

double mpt_gc_log2_negative_probe(double x) { return log2(x); }
