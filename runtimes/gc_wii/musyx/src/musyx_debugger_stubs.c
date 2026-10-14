/* musyx's runtime calls its debugger's instrumentation hooks unconditionally on
 * the PC target. We do not link the debugger (src/musyx/debugger: a UI-facing
 * facility with lock-free shared state meant for another thread), so these are
 * the hooks with the debugger absent: never paused, nothing recorded. */
#include "musyx/debugger.h"

void musyxDebuggerSetPaused(bool8 paused) { (void)paused; }
bool8 musyxDebuggerIsPaused(void) { return 0; }
bool8 musyxDebuggerConsumeStepFrame(void) { return 0; }
void musyxDebuggerRuntimeFrame(u32 deltaTime) { (void)deltaTime; }
void musyxDebuggerRuntimeMacroStep(SYNTH_VOICE* voice, const MSTEP* step) {
  (void)voice;
  (void)step;
}
void musyxDebuggerRuntimeVoiceStart(SYNTH_VOICE* voice) { (void)voice; }
void musyxDebuggerRuntimeVoiceStop(SYNTH_VOICE* voice) { (void)voice; }
void musyxDebuggerRuntimeMessage(u32 voiceId, s32 message) {
  (void)voiceId;
  (void)message;
}
