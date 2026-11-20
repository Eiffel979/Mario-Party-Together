/* The game module's replacements for Party Board's port layer (P1-15): its settings and version
 * queries, the runtime DOL data reader, and bridges to SDK calls whose aurora declarations differ.
 * Party Board's main loop, UI, frame limiter, DLL loader and aurora OS/DVD/CARD are not used. */
#ifndef MPT_MP4_PORT_H
#define MPT_MP4_PORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Options Party Board reads from its user settings. Here they are fixed for a session before boot
 * (they change game behaviour, so the host sets them from the ruleset, never from local
 * preferences). All zero is the console's behaviour. */
typedef struct MptMp4PortConfig {
  int skip_boot_sequence;          /* bootDll: straight to mode select */
  int unlock_all_minigames;        /* GWMgAvailGet answers TRUE for every minigame */
  int unlock_bowsers_gnarly_party; /* mentDll, option/record.c */
} MptMp4PortConfig;

extern MptMp4PortConfig mpt_mp4_port_config;

/* The DOL data reader (ADR 0009 section 3). Takes the user's main.dol (sys/main.dol in the extracted
 * disc), checks that every blob the game reads is inside a DOL section, and copies them into module
 * storage (the game-core state range), so the DOL buffer need not outlive the call. Returns 0 and fills `error` when it is not GMPE01_00's layout. */
int mpt_mp4_dol_attach(const unsigned char* dol, size_t size, char* error, size_t error_size);
void mpt_mp4_dol_detach(void);

/* Test only (M4-07, E2 game-side proof): a direct start of one minigame instead of the boot sequence.
 * `minigame` is its number (401 for m401Dll), 0 for a normal boot; `focus` (0..3) is the pad that plays
 * config slot 0 in a seated category (1-vs-3, 2-vs-2, Bowser, Fortune, Item): the single seat, or the
 * first player of team 0. Pads rotate with it (slot i on pad (i + focus) % 4).
 * Set before boot; the configuration lives outside the game data range (src/test_start.c). */
void mpt_mp4_test_start_config(int minigame, int focus);
/* Called by game/main.c (patch 0038) for omMasterInit's start overlay: `normal` (bootDll) unless a test
 * start is configured. Then it seats four human players, sets GWSystem.mg_next and returns instDll. */
int mpt_mp4_test_start(int normal);

/* The game's entry point (game/main.c, patch 0010): the SDK runs it as the root coroutine. */
void mpt_mp4_game_main(void);

/* CARDInit from the console SDK's declaration (aurora declares a two-argument form). */
void mpt_mp4_CARDInit(void);
/* AIInitDMA and AIGetDMAStartAddr with host pointers (aurora declares them with 32-bit addresses). */
void mpt_mp4_AIInitDMA(void* start, unsigned int length);
void* mpt_mp4_AIGetDMAStartAddr(void);

#ifdef __cplusplus
}
#endif

#endif /* MPT_MP4_PORT_H */
