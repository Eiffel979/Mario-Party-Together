// See context.h. Our own minimal switch instead of libco or minicoro: neither is
// available through vcpkg, both would need a new fetch path, and the switch is
// about 40 instructions per ABI. ADR 0008 §1's requirements (caller-provided
// stack memory, identical behaviour on Windows and Linux) are what matter, and
// this meets them. Recorded as an open question for architect.
#include "context.h"

#include "mpt_gc/coro.h"

#include <cstring>

#if !defined(__clang__)
#error "The GameCube SDK layer must be built with Clang (ADR 0008 §4)"
#endif
#if !defined(__x86_64__) && !defined(_M_X64)
#error "Context switching is implemented for x86-64 only"
#endif

namespace mpt::gc::detail {

extern "C" void mpt_gc_context_trampoline();

#if defined(_WIN32)

// Win64 frame, from the saved stack pointer upwards (272 bytes):
//   0..159   xmm6..xmm15
//   160      MXCSR (4) + x87 control word (2) + pad
//   168      TIB StackBase        gs:[0x08]
//   176      TIB StackLimit       gs:[0x10]
//   184      TIB DeallocationStack gs:[0x1478]
//   192      TIB GuaranteedStackBytes gs:[0x1748]
//   200..256 r15 r14 r13 r12 rsi rdi rbx rbp
//   264      return address
extern "C" __attribute__((naked)) void mpt_gc_switch_context(void** /*save_sp*/, void* /*load_sp*/) {
  __asm__(
      "pushq %rbp\n\t"
      "pushq %rbx\n\t"
      "pushq %rdi\n\t"
      "pushq %rsi\n\t"
      "pushq %r12\n\t"
      "pushq %r13\n\t"
      "pushq %r14\n\t"
      "pushq %r15\n\t"
      "subq $200, %rsp\n\t"
      "movups %xmm6, 0(%rsp)\n\t"
      "movups %xmm7, 16(%rsp)\n\t"
      "movups %xmm8, 32(%rsp)\n\t"
      "movups %xmm9, 48(%rsp)\n\t"
      "movups %xmm10, 64(%rsp)\n\t"
      "movups %xmm11, 80(%rsp)\n\t"
      "movups %xmm12, 96(%rsp)\n\t"
      "movups %xmm13, 112(%rsp)\n\t"
      "movups %xmm14, 128(%rsp)\n\t"
      "movups %xmm15, 144(%rsp)\n\t"
      "stmxcsr 160(%rsp)\n\t"
      "fnstcw 164(%rsp)\n\t"
      "movq %gs:0x08, %rax\n\t"
      "movq %rax, 168(%rsp)\n\t"
      "movq %gs:0x10, %rax\n\t"
      "movq %rax, 176(%rsp)\n\t"
      "movq %gs:0x1478, %rax\n\t"
      "movq %rax, 184(%rsp)\n\t"
      "movq %gs:0x1748, %rax\n\t"
      "movq %rax, 192(%rsp)\n\t"
      "movq %rsp, (%rcx)\n\t"
      "movq %rdx, %rsp\n\t"
      "movups 0(%rsp), %xmm6\n\t"
      "movups 16(%rsp), %xmm7\n\t"
      "movups 32(%rsp), %xmm8\n\t"
      "movups 48(%rsp), %xmm9\n\t"
      "movups 64(%rsp), %xmm10\n\t"
      "movups 80(%rsp), %xmm11\n\t"
      "movups 96(%rsp), %xmm12\n\t"
      "movups 112(%rsp), %xmm13\n\t"
      "movups 128(%rsp), %xmm14\n\t"
      "movups 144(%rsp), %xmm15\n\t"
      "ldmxcsr 160(%rsp)\n\t"
      "fldcw 164(%rsp)\n\t"
      "movq 168(%rsp), %rax\n\t"
      "movq %rax, %gs:0x08\n\t"
      "movq 176(%rsp), %rax\n\t"
      "movq %rax, %gs:0x10\n\t"
      "movq 184(%rsp), %rax\n\t"
      "movq %rax, %gs:0x1478\n\t"
      "movq 192(%rsp), %rax\n\t"
      "movq %rax, %gs:0x1748\n\t"
      "addq $200, %rsp\n\t"
      "popq %r15\n\t"
      "popq %r14\n\t"
      "popq %r13\n\t"
      "popq %r12\n\t"
      "popq %rsi\n\t"
      "popq %rdi\n\t"
      "popq %rbx\n\t"
      "popq %rbp\n\t"
      "retq\n\t");
}

// Entered by `ret` with rsp 16-byte aligned, rbx = entry, r12 = arg.
extern "C" __attribute__((naked)) void mpt_gc_context_trampoline() {
  __asm__(
      "subq $32, %rsp\n\t"
      "movq %r12, %rcx\n\t"
      "callq *%rbx\n\t"
      "ud2\n\t");
}

namespace {
constexpr std::size_t kFrameBytes = 272;
constexpr std::uint16_t kX87ControlWord = 0x027F;  // Windows x64 default
}  // namespace

void* make_context(void* stack_lo, std::size_t stack_size, ContextEntry entry, void* arg) {
  auto lo = reinterpret_cast<std::uintptr_t>(stack_lo);
  std::uintptr_t top = (lo + stack_size) & ~std::uintptr_t{15};
  std::uintptr_t after_ret = top - 16;  // rsp once the trampoline is entered: 16-byte aligned
  std::uintptr_t sp = after_ret - kFrameBytes;
  auto* frame = reinterpret_cast<unsigned char*>(sp);
  std::memset(frame, 0, kFrameBytes);
  const std::uint32_t mxcsr = kDeterministicMxcsr;
  std::memcpy(frame + 160, &mxcsr, 4);
  std::memcpy(frame + 164, &kX87ControlWord, 2);
  const std::uint64_t stack_base = top, stack_limit = lo, dealloc = lo, guaranteed = 0;
  std::memcpy(frame + 168, &stack_base, 8);
  std::memcpy(frame + 176, &stack_limit, 8);
  std::memcpy(frame + 184, &dealloc, 8);
  std::memcpy(frame + 192, &guaranteed, 8);
  const std::uint64_t r12 = reinterpret_cast<std::uintptr_t>(arg);
  const std::uint64_t rbx = reinterpret_cast<std::uintptr_t>(entry);
  const std::uint64_t ret = reinterpret_cast<std::uintptr_t>(&mpt_gc_context_trampoline);
  std::memcpy(frame + 224, &r12, 8);
  std::memcpy(frame + 248, &rbx, 8);
  std::memcpy(frame + 264, &ret, 8);
  return frame;
}

#else  // SysV x86-64

// SysV frame, from the saved stack pointer upwards (64 bytes):
//   0   MXCSR (4) + x87 control word (2) + pad
//   8..48  r15 r14 r13 r12 rbx rbp
//   56  return address
extern "C" __attribute__((naked)) void mpt_gc_switch_context(void** /*save_sp*/, void* /*load_sp*/) {
  __asm__(
      "pushq %rbp\n\t"
      "pushq %rbx\n\t"
      "pushq %r12\n\t"
      "pushq %r13\n\t"
      "pushq %r14\n\t"
      "pushq %r15\n\t"
      "subq $8, %rsp\n\t"
      "stmxcsr 0(%rsp)\n\t"
      "fnstcw 4(%rsp)\n\t"
      "movq %rsp, (%rdi)\n\t"
      "movq %rsi, %rsp\n\t"
      "ldmxcsr 0(%rsp)\n\t"
      "fldcw 4(%rsp)\n\t"
      "addq $8, %rsp\n\t"
      "popq %r15\n\t"
      "popq %r14\n\t"
      "popq %r13\n\t"
      "popq %r12\n\t"
      "popq %rbx\n\t"
      "popq %rbp\n\t"
      "retq\n\t");
}

extern "C" __attribute__((naked)) void mpt_gc_context_trampoline() {
  __asm__(
      "movq %r12, %rdi\n\t"
      "callq *%rbx\n\t"
      "ud2\n\t");
}

namespace {
constexpr std::size_t kFrameBytes = 64;
constexpr std::uint16_t kX87ControlWord = 0x037F;  // SysV default
}  // namespace

void* make_context(void* stack_lo, std::size_t stack_size, ContextEntry entry, void* arg) {
  auto lo = reinterpret_cast<std::uintptr_t>(stack_lo);
  std::uintptr_t top = (lo + stack_size) & ~std::uintptr_t{15};
  std::uintptr_t after_ret = top - 16;
  std::uintptr_t sp = after_ret - kFrameBytes;
  auto* frame = reinterpret_cast<unsigned char*>(sp);
  std::memset(frame, 0, kFrameBytes);
  const std::uint32_t mxcsr = kDeterministicMxcsr;
  std::memcpy(frame + 0, &mxcsr, 4);
  std::memcpy(frame + 4, &kX87ControlWord, 2);
  const std::uint64_t r12 = reinterpret_cast<std::uintptr_t>(arg);
  const std::uint64_t rbx = reinterpret_cast<std::uintptr_t>(entry);
  const std::uint64_t ret = reinterpret_cast<std::uintptr_t>(&mpt_gc_context_trampoline);
  std::memcpy(frame + 32, &r12, 8);
  std::memcpy(frame + 40, &rbx, 8);
  std::memcpy(frame + 56, &ret, 8);
  return frame;
}

#endif

}  // namespace mpt::gc::detail

// ---- The C API for game-side coroutines (mpt_gc/coro.h) ----------------------------------------------

extern "C" void* mpt_gc_coro_make(void* stack_lo, std::size_t stack_size, void (*entry)(void*), void* arg) {
  return mpt::gc::detail::make_context(stack_lo, stack_size, entry, arg);
}

extern "C" void mpt_gc_coro_switch(void** save, void* load) { mpt::gc::detail::mpt_gc_switch_context(save, load); }
