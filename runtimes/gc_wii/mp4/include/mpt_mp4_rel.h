/* The game module's overlay table (P1-15 required pieces 1 and 2).
 *
 * Every REL is linked into the game module with its symbols isolated (tools/isolate_rel.cmake). Its
 * writable data sits in its own range [data_lo, data_hi): .data with its initial values and .bss as
 * zeros (RELs are compiled with -fno-zero-initialized-in-bss). A pristine copy of each range is taken
 * once, before any game code runs, and restored on every link: that is what OSLink gives on the
 * console, where each link maps the REL afresh.
 */
#ifndef MPT_MP4_REL_H
#define MPT_MP4_REL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MptMp4RelInfo {
  const char* name;           /* the overlay table's name, e.g. "m401Dll" */
  void (*object_setup)(void); /* the REL's ObjectSetup, or 0 if it has none */
  unsigned char* data_lo;     /* writable data range; data_lo == data_hi if the REL has none */
  unsigned char* data_hi;
} MptMp4RelInfo;

/* Takes the pristine copies. Called once by the module before game code runs. Returns 0 on failure. */
int mpt_mp4_rel_init(void);
/* The REL named `name`, with its data restored, or NULL if the module has no such REL. */
const MptMp4RelInfo* mpt_mp4_rel_link(const char* name);
void mpt_mp4_rel_object_setup(const MptMp4RelInfo* rel);
void mpt_mp4_rel_unlink(const MptMp4RelInfo* rel);

/* The game core's writable data (the DOL, msm and the port files; src/game_data.c). Together with
 * every REL's range, this is the game state outside the SDK arena. */
unsigned char* mpt_mp4_game_data_lo(void);
unsigned char* mpt_mp4_game_data_hi(void);
/* Restores the game core's data to its image at module load (the first call takes that image). Called
 * before every boot. Returns 0 on failure. */
int mpt_mp4_game_data_reset(void);

/* The generated table (mpt_mp4_rel_table.c). */
extern const MptMp4RelInfo* const mpt_mp4_rel_table[];
extern const unsigned mpt_mp4_rel_count;

#ifdef __cplusplus
}
#endif

#endif /* MPT_MP4_REL_H */
