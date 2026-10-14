/* The musyx side of the AudioEngine hook: pull rendering and the by-value hash
 * of musyx's game-visible state (ADR 0007 §4, ADR 0008 §8).
 *
 * Hash manifest v1 (MP4 polls stream, music and SFX status and a MIDI controller
 * value, mp4.md §7). Every field is hashed by value; data pointers as arena
 * offsets, function pointers as stable IDs (a sentinel until P1-16):
 *  - engine: sndActive, synthIdleWaitActive, synthFlags, synthRealTime, the
 *    global and track volume tables, synthInfo, the PC render clock;
 *  - each synth voice: macro state and position, ids, flags, notes, pitch,
 *    volume and envelope, timers, local variables, message queue counters;
 *  - each DSP voice: state, sample id and position, pitch, volumes, ADSR;
 *  - each sequencer instance: state, ids, mutes, volume groups, flags;
 *  - the MIDI controller tables (per sequencer channel and per SFX voice),
 *    last notes and dirty flags;
 *  - the stream table.
 * PCM, mixing buffers and resampler history are presentation only and are
 * left out; they are still saved with the rest of musyx's statics.
 */
#include <string.h>

#include "musyx/musyx.h"
#include "musyx/dspvoice.h"
#include "musyx/hardware.h"
#include "musyx/pc.h"
#include "musyx/sal.h"
#include "musyx/seq.h"
#include "musyx/synth.h"
#include "musyx_glue.h"

int mpt_musyx_configure(void) {
  const SND_PC_CONFIG config = {32000, 2};
  return sndPCConfigure(&config) ? 1 : 0;
}

void mpt_musyx_render(int16_t* out, uint32_t frames) {
  /* Before sndInit (and after sndQuit) the console's AI would play silence. */
  if (!sndActive || !sndPCRender(out, frames)) memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
}

static void putF(const MptMusyxHasher* h, float f) {
  uint32_t bits;
  memcpy(&bits, &f, sizeof bits);
  h->put(h->self, bits);
}

static void hashAdsr(const MptMusyxHasher* h, const ADSR_VARS* a) {
  h->put(h->self, a->mode | ((uint64_t)a->state << 8) | ((uint64_t)a->cnt << 32));
  h->put(h->self, (uint32_t)a->currentVolume | ((uint64_t)(uint32_t)a->currentIndex << 32));
  h->put(h->self, (uint32_t)a->currentDelta);
}

static void hashSynthVoice(const MptMusyxHasher* h, const SYNTH_VOICE* v) {
  h->put(h->self, (uint64_t)v->macState | ((uint64_t)v->trapEventAny << 8) | ((uint64_t)v->callStackEntryNum << 16) |
                      ((uint64_t)v->callStackIndex << 24) | ((uint64_t)v->timeUsedByInput << 32) |
                      ((uint64_t)v->loop << 40));
  h->put(h->self, h->ptr(h->self, v->addr));
  h->put(h->self, h->ptr(h->self, v->curAddr));
  for (int i = 0; i < 3; ++i) {
    h->put(h->self, h->ptr(h->self, v->trapEventAddr[i]));
    h->put(h->self, h->ptr(h->self, v->trapEventCurAddr[i]));
  }
  h->put(h->self, v->lastLowCallTime);
  h->put(h->self, v->lastZeroCallTime);
  h->put(h->self, v->macStartTime);
  h->put(h->self, v->wait);
  h->put(h->self, v->waitTime);
  for (int i = 0; i < 16; ++i) h->put(h->self, (uint32_t)v->local_vars[i]);
  h->put(h->self, v->child | ((uint64_t)v->parent << 32));
  h->put(h->self, v->id | ((uint64_t)v->lastVID << 32));
  h->put(h->self, (uint64_t)v->allocId | ((uint64_t)v->macroId << 32) | ((uint64_t)v->keyGroup << 48) |
                      ((uint64_t)v->prio << 56));
  h->put(h->self, v->age | ((uint64_t)v->ageSpeed << 32));
  h->put(h->self, v->cFlags);
  h->put(h->self, v->block | ((uint64_t)v->fxFlag << 8) | ((uint64_t)v->vGroup << 16) | ((uint64_t)v->studio << 24) |
                      ((uint64_t)v->track << 32) | ((uint64_t)v->midi << 40) | ((uint64_t)v->midiSet << 48) |
                      ((uint64_t)v->section << 56));
  h->put(h->self, v->sInfo | ((uint64_t)v->playFrq << 32));
  h->put(h->self, v->curNote | ((uint64_t)(uint8_t)v->curDetune << 16) | ((uint64_t)v->orgNote << 24) |
                      ((uint64_t)v->lastNote << 32) | ((uint64_t)v->portType << 40) | ((uint64_t)v->curPitch << 48));
  h->put(h->self, v->volume | ((uint64_t)v->orgVolume << 32));
  putF(h, v->lastVolFaderScale);
  putF(h, v->treCurScale);
  h->put(h->self, v->lastPan | ((uint64_t)v->lastSPan << 32));
  h->put(h->self, v->panning[0] | ((uint64_t)v->panning[1] << 32));
  h->put(h->self, (uint32_t)v->envDelta | ((uint64_t)v->envTarget << 32));
  h->put(h->self, v->envCurrent);
  h->put(h->self, v->vibCurTime | ((uint64_t)(uint32_t)v->vibCurOffset << 32));
  h->put(h->self, v->portCurPitch | ((uint64_t)v->portTime << 32));
  hashAdsr(h, &v->pitchADSR);
  h->put(h->self, v->midiDirtyFlags);
  h->put(h->self, h->ptr(h->self, v->vidList));
  h->put(h->self, v->mesgNum | ((uint64_t)v->mesgRead << 8));
}

static void hashDspVoice(const MptMusyxHasher* h, const DSPvoice* d) {
  h->put(h->self, d->state | ((uint64_t)d->postBreak << 8) | ((uint64_t)d->startupBreak << 16) |
                      ((uint64_t)d->studio << 24) | ((uint64_t)d->smp_id << 32) | ((uint64_t)d->streamLoopPS << 48));
  h->put(h->self, d->prio | ((uint64_t)d->virtualSampleID << 32));
  h->put(h->self, d->playInfo.posHi | ((uint64_t)d->playInfo.posLo << 32));
  h->put(h->self, d->playInfo.pitch | ((uint64_t)d->currentAddr << 32));
  for (int i = 0; i < 5; ++i) h->put(h->self, d->changed[i] | ((uint64_t)d->pitch[i] << 32));
  h->put(h->self, d->volL | ((uint64_t)d->volR << 16) | ((uint64_t)d->volS << 32) | ((uint64_t)d->volLa << 48));
  h->put(h->self, d->volRa | ((uint64_t)d->volSa << 16) | ((uint64_t)d->volLb << 32) | ((uint64_t)d->volRb << 48));
  h->put(h->self, d->volSb | ((uint64_t)d->srcTypeSelect << 16) | ((uint64_t)d->srcCoefSelect << 32) |
                      ((uint64_t)d->singleOffset << 48));
  h->put(h->self, h->ptr(h->self, d->smp_info.addr));
  h->put(h->self, d->smp_info.length | ((uint64_t)d->smp_info.loop << 32));
  h->put(h->self, d->smp_info.loopLength | ((uint64_t)d->smp_info.compType << 32));
  h->put(h->self, d->vSampleInfo.inLoopBuffer);
  hashAdsr(h, &d->adsr);
}

static void hashBytes(const MptMusyxHasher* h, const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i += 8) {
    uint64_t v = 0;
    const size_t k = n - i < 8 ? n - i : 8;
    memcpy(&v, p + i, k);  /* plain byte arrays: no padding, no pointers */
    h->put(h->self, v);
  }
}

void mpt_musyx_hash(const MptMusyxHasher* h) {
  h->put(h->self, 0x4D50544D55535831ull); /* "MPTMUSX1", manifest version 1 */
  h->put(h->self, sndActive | ((uint64_t)synthIdleWaitActive << 8));
  h->put(h->self, synthFlags);
  h->put(h->self, synthRealTime);
  for (int i = 0; i < 16; ++i) h->put(h->self, (uint32_t)synthGlobalVariable[i]);
  hashBytes(h, synthTrackVolume, sizeof synthTrackVolume);
  h->put(h->self, synthInfo.mixFrq | ((uint64_t)synthInfo.numSamples << 32));
  h->put(h->self, synthInfo.voiceNum | ((uint64_t)synthInfo.maxMusic << 8) | ((uint64_t)synthInfo.maxSFX << 16) |
                      ((uint64_t)synthInfo.studioNum << 24));
  const SND_PC_RENDER_INFO ri = sndPCGetRenderInfo();
  h->put(h->self, ri.renderedFrames);
  h->put(h->self, ri.controlTicks);
  if (!sndActive) return;

  if (synthVoice)
    for (uint32_t i = 0; i < synthInfo.voiceNum; ++i) hashSynthVoice(h, &synthVoice[i]);
  if (dspVoice)
    for (uint32_t i = 0; i < salNumVoices; ++i) hashDspVoice(h, &dspVoice[i]);
  for (int i = 0; i < 8; ++i) {
    const SEQ_INSTANCE* s = &seqInstance[i];
    h->put(h->self, s->state | ((uint64_t)s->index << 8) | ((uint64_t)s->groupID << 16) | ((uint64_t)s->publicId << 32));
    if (!s->state) continue;
    h->put(h->self, s->trackMute[0] | ((uint64_t)s->trackMute[1] << 32));
    hashBytes(h, s->trackVolGroup, sizeof s->trackVolGroup);
    h->put(h->self, s->defVGroup | ((uint64_t)s->syncActive << 8) | ((uint64_t)s->defStudio << 16) |
                        ((uint64_t)s->keyOffCheck << 24));
  }
  MptMusyxMidiState m;
  mpt_musyx_midi_state(&m);
  hashBytes(h, m.midi_ctrl, m.midi_ctrl_size);
  hashBytes(h, m.fx_ctrl, m.fx_ctrl_size);
  hashBytes(h, m.midi_last_note, m.midi_last_note_size);
  hashBytes(h, m.fx_last_note, m.fx_last_note_size);
  for (size_t i = 0; i < m.dirty_count; ++i) h->put(h->self, m.dirty[i]);
  mpt_musyx_hash_streams(h);
}
