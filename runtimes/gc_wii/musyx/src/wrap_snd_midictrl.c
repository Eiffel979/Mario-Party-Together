/* Compiles upstream snd_midictrl.c unchanged and exposes its file-scope MIDI
 * controller state to the engine hash, which reads it by value. */
#include "snd_midictrl.c"

#include "musyx_glue.h"

void mpt_musyx_midi_state(MptMusyxMidiState* out) {
  out->midi_ctrl = &midi_ctrl[0][0][0];
  out->midi_ctrl_size = sizeof midi_ctrl;
  out->fx_ctrl = &fx_ctrl[0][0];
  out->fx_ctrl_size = sizeof fx_ctrl;
  out->midi_last_note = &midi_lastNote[0][0];
  out->midi_last_note_size = sizeof midi_lastNote;
  out->fx_last_note = fx_lastNote;
  out->fx_last_note_size = sizeof fx_lastNote;
  out->dirty = &inpGlobalMIDIDirtyFlags[0][0];
  out->dirty_count = sizeof inpGlobalMIDIDirtyFlags / sizeof inpGlobalMIDIDirtyFlags[0][0];
}
