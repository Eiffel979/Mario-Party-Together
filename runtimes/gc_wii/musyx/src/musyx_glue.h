/* C interface between the musyx sources and our C++ engine adapter. */
#ifndef MPT_MUSYX_GLUE_H
#define MPT_MUSYX_GLUE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Callbacks into StateHasher: a value, a data pointer (hashed as an arena offset),
 * a function pointer (hashed as a stable ID, a sentinel until P1-16). */
typedef struct MptMusyxHasher {
  void* self;
  void (*put)(void* self, uint64_t v);
  uint64_t (*ptr)(void* self, const void* p);
  uint64_t (*fn)(void* self, const void* f);
} MptMusyxHasher;

typedef struct MptMusyxMidiState {
  const uint8_t* midi_ctrl;
  size_t midi_ctrl_size;
  const uint8_t* fx_ctrl;
  size_t fx_ctrl_size;
  const uint8_t* midi_last_note;
  size_t midi_last_note_size;
  const uint8_t* fx_last_note;
  size_t fx_last_note_size;
  const uint32_t* dirty;
  size_t dirty_count;
} MptMusyxMidiState;

void mpt_musyx_state_bounds(unsigned char** data_lo, unsigned char** data_hi, unsigned char** bss_lo,
                            unsigned char** bss_hi);
void mpt_musyx_midi_state(MptMusyxMidiState* out);
void mpt_musyx_hash_streams(const MptMusyxHasher* h);

/* musyx_glue.c */
int mpt_musyx_configure(void);  /* 32 kHz stereo pull renderer; before sndInit */
void mpt_musyx_render(int16_t* out, uint32_t frames);
void mpt_musyx_hash(const MptMusyxHasher* h);

#ifdef __cplusplus
}
#endif

#endif /* MPT_MUSYX_GLUE_H */
