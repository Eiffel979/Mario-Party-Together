/* Bounds of musyx's state sections (see musyx_state_section.h). */
#include "musyx_glue.h"

#if defined(_WIN32)
#pragma clang section data = ".mpxd$a" bss = ".mpxb$a"
unsigned char mpt_musyx_data_begin[8] = {1};
unsigned char mpt_musyx_bss_begin[8];
#pragma clang section data = ".mpxd$z" bss = ".mpxb$z"
unsigned char mpt_musyx_data_end[8] = {1};
unsigned char mpt_musyx_bss_end[8];
#pragma clang section data = "" bss = ""

void mpt_musyx_state_bounds(unsigned char** data_lo, unsigned char** data_hi, unsigned char** bss_lo,
                            unsigned char** bss_hi) {
  *data_lo = mpt_musyx_data_begin + sizeof mpt_musyx_data_begin;
  *data_hi = mpt_musyx_data_end;
  *bss_lo = mpt_musyx_bss_begin + sizeof mpt_musyx_bss_begin;
  *bss_hi = mpt_musyx_bss_end;
}
#else
extern unsigned char __start_mpt_musyx_data[], __stop_mpt_musyx_data[];
extern unsigned char __start_mpt_musyx_bss[], __stop_mpt_musyx_bss[];

void mpt_musyx_state_bounds(unsigned char** data_lo, unsigned char** data_hi, unsigned char** bss_lo,
                            unsigned char** bss_hi) {
  *data_lo = __start_mpt_musyx_data;
  *data_hi = __stop_mpt_musyx_data;
  *bss_lo = __start_mpt_musyx_bss;
  *bss_hi = __stop_mpt_musyx_bss;
}
#endif
