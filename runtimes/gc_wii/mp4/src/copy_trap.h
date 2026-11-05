// The copy-destination debug trap (ADR 0007 section 5, mp4.md section 8).
//
// GXCopyTex destinations stay GPU-side; their CPU bytes stay zero, so a CPU read of them would make
// game logic depend on something the console computed on the GPU. With the trap enabled (a debug boot
// flag), every whole page inside a copy destination is protected while a frame step runs. A CPU read
// there records a violation, which faults the runtime after the step, unless the running overlay is
// allow-listed (m415, Stamp Out!, which copies its shadow map into a texture; presentation only). A CPU
// write releases the range: the memory is in other use (the heap reused it, or the game cleared it).
//
// Host-side state; never part of the game state. Single-threaded, like the module.
#pragma once

#include <string>

namespace mpt::mp4 {

void copy_trap_enable();   // installs the fault handler and the GXCopyTex observer
void copy_trap_disable();
void copy_trap_arm();      // before a frame step: protect the recorded ranges
void copy_trap_disarm();   // after it, so host-side reads (save states, hashes) never fault
void copy_trap_forget();   // after a load_state: the recorded destinations no longer apply
std::string copy_trap_take_violation();  // "" if none since the last call

}  // namespace mpt::mp4
