/* The layout probe (MPT_MP4_LAYOUT_PROBE, off by default; test only). Built first among the game core's
 * objects, it moves everything after it. Mode ON: 8 KiB of code shift every function, and 4 KiB plus an
 * odd 24 bytes in the game core's data range and in its read-only data shift every global and constant
 * behind them. Mode rodata: the read-only padding only. A probe module must give the same state_hash
 * trail as a normal one (only the deep hash may differ): the Windows stand-in for the DLL and the .so
 * laying the module out differently (ADR 0008 amendment after P1-16 hash coverage). Its symbols start
 * with mpt_layout_probe, which the state hash leaves out (src/module_symbols.cpp). It has no rw anchor
 * (game_section.h): an extra one would add a symbol the normal module lacks. */
#ifdef MPT_MP4_LAYOUT_PROBE_DATA
__attribute__((used)) static unsigned char mpt_layout_probe_data[4096 + 24] = {1};
#endif

#ifdef MPT_MP4_LAYOUT_PROBE_RODATA
__attribute__((used)) static const unsigned char mpt_layout_probe_rodata[4096 + 24] = {1};
#endif

#ifdef MPT_MP4_LAYOUT_PROBE_CODE
void mpt_layout_probe_code(void);

__attribute__((used, noinline)) void mpt_layout_probe_code(void) {
  __asm__ volatile(".fill 8192, 1, 0x90");
#ifdef MPT_MP4_LAYOUT_PROBE_DATA
  mpt_layout_probe_data[0] ^= 1;
#endif
}
#endif
