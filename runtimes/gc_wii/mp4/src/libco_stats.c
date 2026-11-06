/* The process-stack high-water diagnostics of src/libco.c. Host side: built into mp4_game without
 * game_section.h, so these sit outside the game data range and are neither saved nor hashed. The
 * peak follows from how much host stack the code under game calls uses (aurora's GX, the C
 * runtime), which differs by compiler and platform (#49 review). Not reset by a soft reset or a
 * load_state: they only decide when the measurement prints. */
#include <stdint.h>

uint32_t mpt_co_peak_permille;
uint32_t mpt_co_switches;
