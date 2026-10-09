// Cooperative context switching for SDK threads and game processes (ADR 0008 §1).
//
// A context is just a saved stack pointer. The callee-saved registers, MXCSR,
// the x87 control word and (on Windows) the TIB stack bounds are pushed onto the
// stack being left, so a context lives entirely in memory the caller provides.
// SDK stacks come from the arena, which puts every suspended context inside the
// save state.
//
// Implemented for x86-64 Windows (Win64 ABI) and x86-64 SysV (Linux) with Clang,
// all in context.cpp with the frame layouts documented there. Other targets are an
// #error. ARM64 is not supported yet; libco is the named fallback (ADR 0008 §1).
//
// Rules for code running on these contexts:
// - No C++ exception, longjmp or SEH unwind may cross a switch: a coroutine's
//   first frame (the trampoline) has no unwind information, and the host's frames
//   are on another stack. Game code uses neither; SDK code reports errors by
//   faulting the runtime instead.
// - The process must not use CET user shadow stacks (Sdk::create checks).
#pragma once

#include <cstddef>
#include <cstdint>

namespace mpt::gc::detail {

using ContextEntry = void (*)(void* arg);

// The MXCSR every new context starts with: round to nearest, all exceptions
// masked, no flush-to-zero, no denormals-are-zero.
inline constexpr std::uint32_t kDeterministicMxcsr = 0x1F80;

// Prepares a fresh context on [stack_lo, stack_lo + stack_size). The first switch
// into it calls entry(arg), which must never return. Returns the context's
// initial stack pointer.
void* make_context(void* stack_lo, std::size_t stack_size, ContextEntry entry, void* arg);

// Saves the current context into *save_sp and resumes the context at load_sp.
// Returns when something switches back to the saved context.
extern "C" void mpt_gc_switch_context(void** save_sp, void* load_sp);

}  // namespace mpt::gc::detail
