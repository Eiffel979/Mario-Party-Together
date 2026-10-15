/* The bounds of the game core's writable data (game_section.h). Not compiled with that header. */
#include "mpt_mp4_rel.h"

#if defined(_WIN32)
/* The linker merges .data$mptgame$* into .data in suffix order: $a, the game's $b, then $c. */
#pragma section(".data$mptgame$a", read, write)
#pragma section(".data$mptgame$c", read, write)
__declspec(allocate(".data$mptgame$a")) static unsigned char mpt_start = 1;
__declspec(allocate(".data$mptgame$c")) static unsigned char mpt_stop = 1;
#define MPT_GAME_LO (&mpt_start + 1)
#define MPT_GAME_HI (&mpt_stop)
#else
extern unsigned char __start_mptgame[] __attribute__((weak, visibility("hidden")));
extern unsigned char __stop_mptgame[] __attribute__((weak, visibility("hidden")));
#define MPT_GAME_LO (__start_mptgame)
#define MPT_GAME_HI (__stop_mptgame)
#endif

unsigned char* mpt_mp4_game_data_lo(void) { return MPT_GAME_LO; }
unsigned char* mpt_mp4_game_data_hi(void) { return MPT_GAME_HI; }
