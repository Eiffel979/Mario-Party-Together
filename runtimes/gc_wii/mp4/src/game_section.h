/* Forced into every game-core object (the DOL's sources, msm and our port files): their writable data
 * goes to one range, the game module's registered state region (ADR 0008 section 10). Zero-initialised
 * data is kept out of .bss by -fno-zero-initialized-in-bss. See game_data.c. */
#if defined(_WIN32)
#pragma data_seg(".data$mptgame$b")
#pragma bss_seg(".data$mptgame$b")
#else
#pragma clang section data="mptgame"
#endif
