/* The layout probe (MPT_MP4_LAYOUT_PROBE, off by default; test only). Built first among the game core's
 * objects, it moves everything after it: 8 KiB of code shift every function, and 4 KiB plus an odd 24
 * bytes in the game core's data range shift every global behind it. A probe module must give the same
 * state_hash trail as a normal one (only the deep hash may differ): the Windows stand-in for the DLL and
 * the .so laying the module out differently (ADR 0008 amendment after P1-16 hash coverage). Its symbols
 * start with mpt_layout_probe, which the state hash leaves out (src/module_symbols.cpp). */
__attribute__((used)) static unsigned char mpt_layout_probe_data[4096 + 24] = {1};

void mpt_layout_probe_code(void);

__attribute__((used, noinline)) void mpt_layout_probe_code(void) {
  __asm__ volatile(".fill 8192, 1, 0x90");
  mpt_layout_probe_data[0] ^= 1;
}
