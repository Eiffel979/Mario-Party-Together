// The context switch preserves every callee-saved register across a round trip
// into a coroutine that clobbers them all (architect condition for our own switch,
// ADR 0008 §1): the general-purpose registers, xmm6-15 on Win64, MXCSR, the x87
// control word and the Win64 TIB stack fields.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../src/context.h"
#include "harness.h"

using mpt::gc::detail::make_context;

namespace {

struct Probe {
  std::uint64_t gpr[8];       // rbx rbp rdi rsi r12 r13 r14 r15 after the round trip
  std::uint64_t xmm[10][2];   // xmm6..xmm15 (Win64 only)
  std::uint32_t mxcsr;
  std::uint16_t x87cw;
  std::uint16_t pad;
  std::uint64_t tib_after[4];   // gs:[0x08], [0x10], [0x1478], [0x1748] (Win64 only)
  std::uint64_t tib_before[4];
};
static_assert(offsetof(Probe, xmm) == 64);
static_assert(offsetof(Probe, mxcsr) == 224);
static_assert(offsetof(Probe, x87cw) == 228);
static_assert(offsetof(Probe, tib_after) == 232);
static_assert(offsetof(Probe, tib_before) == 264);

void* g_host_sp = nullptr;
void* g_co_sp = nullptr;

}  // namespace

extern "C" void mpt_test_probe_switch(void** save_sp, void* load_sp, Probe* out);
extern "C" void mpt_test_clobber_and_switch(void** save_sp, void* load_sp);

#if defined(_WIN32)
// Win64: rcx = save_sp, rdx = load_sp, r8 = out. Frame below the 8 pushes:
//   [0,32) shadow  [32,192) caller xmm6-15  [192] out  [200] caller MXCSR/CW
//   [208,240) TIB before  [240,248) scratch  (264 bytes, keeps rsp 16-aligned)
extern "C" __attribute__((naked)) void mpt_test_probe_switch(void**, void*, Probe*) {
  __asm__(
      "pushq %rbp\n\t" "pushq %rbx\n\t" "pushq %rdi\n\t" "pushq %rsi\n\t"
      "pushq %r12\n\t" "pushq %r13\n\t" "pushq %r14\n\t" "pushq %r15\n\t"
      "subq $264, %rsp\n\t"
      "movups %xmm6, 32(%rsp)\n\t" "movups %xmm7, 48(%rsp)\n\t" "movups %xmm8, 64(%rsp)\n\t"
      "movups %xmm9, 80(%rsp)\n\t" "movups %xmm10, 96(%rsp)\n\t" "movups %xmm11, 112(%rsp)\n\t"
      "movups %xmm12, 128(%rsp)\n\t" "movups %xmm13, 144(%rsp)\n\t" "movups %xmm14, 160(%rsp)\n\t"
      "movups %xmm15, 176(%rsp)\n\t"
      "movq %r8, 192(%rsp)\n\t"
      "stmxcsr 200(%rsp)\n\t" "fnstcw 204(%rsp)\n\t"
      "movq %gs:0x08, %rax\n\t" "movq %rax, 208(%rsp)\n\t"
      "movq %gs:0x10, %rax\n\t" "movq %rax, 216(%rsp)\n\t"
      "movq %gs:0x1478, %rax\n\t" "movq %rax, 224(%rsp)\n\t"
      "movq %gs:0x1748, %rax\n\t" "movq %rax, 232(%rsp)\n\t"
      "movabsq $0x1111111111111111, %rbx\n\t"
      "movabsq $0x2222222222222222, %rbp\n\t"
      "movabsq $0x3333333333333333, %rdi\n\t"
      "movabsq $0x4444444444444444, %rsi\n\t"
      "movabsq $0x5555555555555555, %r12\n\t"
      "movabsq $0x6666666666666666, %r13\n\t"
      "movabsq $0x7777777777777777, %r14\n\t"
      "movabsq $0x8888888888888888, %r15\n\t"
      "movabsq $0xA6A6A6A6A6A6A6A6, %rax\n\t" "movq %rax, %xmm6\n\t" "movlhps %xmm6, %xmm6\n\t"
      "movabsq $0xA7A7A7A7A7A7A7A7, %rax\n\t" "movq %rax, %xmm7\n\t" "movlhps %xmm7, %xmm7\n\t"
      "movabsq $0xA8A8A8A8A8A8A8A8, %rax\n\t" "movq %rax, %xmm8\n\t" "movlhps %xmm8, %xmm8\n\t"
      "movabsq $0xA9A9A9A9A9A9A9A9, %rax\n\t" "movq %rax, %xmm9\n\t" "movlhps %xmm9, %xmm9\n\t"
      "movabsq $0xAAAAAAAAAAAAAAAA, %rax\n\t" "movq %rax, %xmm10\n\t" "movlhps %xmm10, %xmm10\n\t"
      "movabsq $0xABABABABABABABAB, %rax\n\t" "movq %rax, %xmm11\n\t" "movlhps %xmm11, %xmm11\n\t"
      "movabsq $0xACACACACACACACAC, %rax\n\t" "movq %rax, %xmm12\n\t" "movlhps %xmm12, %xmm12\n\t"
      "movabsq $0xADADADADADADADAD, %rax\n\t" "movq %rax, %xmm13\n\t" "movlhps %xmm13, %xmm13\n\t"
      "movabsq $0xAEAEAEAEAEAEAEAE, %rax\n\t" "movq %rax, %xmm14\n\t" "movlhps %xmm14, %xmm14\n\t"
      "movabsq $0xAFAFAFAFAFAFAFAF, %rax\n\t" "movq %rax, %xmm15\n\t" "movlhps %xmm15, %xmm15\n\t"
      "movl $0x9FC0, 240(%rsp)\n\t" "ldmxcsr 240(%rsp)\n\t"
      "movw $0x037F, 244(%rsp)\n\t" "fldcw 244(%rsp)\n\t"
      "callq mpt_gc_switch_context\n\t"
      "movq 192(%rsp), %rax\n\t"
      "movq %rbx, 0(%rax)\n\t" "movq %rbp, 8(%rax)\n\t" "movq %rdi, 16(%rax)\n\t" "movq %rsi, 24(%rax)\n\t"
      "movq %r12, 32(%rax)\n\t" "movq %r13, 40(%rax)\n\t" "movq %r14, 48(%rax)\n\t" "movq %r15, 56(%rax)\n\t"
      "movups %xmm6, 64(%rax)\n\t" "movups %xmm7, 80(%rax)\n\t" "movups %xmm8, 96(%rax)\n\t"
      "movups %xmm9, 112(%rax)\n\t" "movups %xmm10, 128(%rax)\n\t" "movups %xmm11, 144(%rax)\n\t"
      "movups %xmm12, 160(%rax)\n\t" "movups %xmm13, 176(%rax)\n\t" "movups %xmm14, 192(%rax)\n\t"
      "movups %xmm15, 208(%rax)\n\t"
      "stmxcsr 224(%rax)\n\t" "fnstcw 228(%rax)\n\t"
      "movq %gs:0x08, %rcx\n\t" "movq %rcx, 232(%rax)\n\t"
      "movq %gs:0x10, %rcx\n\t" "movq %rcx, 240(%rax)\n\t"
      "movq %gs:0x1478, %rcx\n\t" "movq %rcx, 248(%rax)\n\t"
      "movq %gs:0x1748, %rcx\n\t" "movq %rcx, 256(%rax)\n\t"
      "movq 208(%rsp), %rcx\n\t" "movq %rcx, 264(%rax)\n\t"
      "movq 216(%rsp), %rcx\n\t" "movq %rcx, 272(%rax)\n\t"
      "movq 224(%rsp), %rcx\n\t" "movq %rcx, 280(%rax)\n\t"
      "movq 232(%rsp), %rcx\n\t" "movq %rcx, 288(%rax)\n\t"
      "ldmxcsr 200(%rsp)\n\t" "fldcw 204(%rsp)\n\t"
      "movups 32(%rsp), %xmm6\n\t" "movups 48(%rsp), %xmm7\n\t" "movups 64(%rsp), %xmm8\n\t"
      "movups 80(%rsp), %xmm9\n\t" "movups 96(%rsp), %xmm10\n\t" "movups 112(%rsp), %xmm11\n\t"
      "movups 128(%rsp), %xmm12\n\t" "movups 144(%rsp), %xmm13\n\t" "movups 160(%rsp), %xmm14\n\t"
      "movups 176(%rsp), %xmm15\n\t"
      "addq $264, %rsp\n\t"
      "popq %r15\n\t" "popq %r14\n\t" "popq %r13\n\t" "popq %r12\n\t"
      "popq %rsi\n\t" "popq %rdi\n\t" "popq %rbx\n\t" "popq %rbp\n\t"
      "retq\n\t");
}

// Clobbers every callee-saved register and the FP control state, then switches.
extern "C" __attribute__((naked)) void mpt_test_clobber_and_switch(void**, void*) {
  __asm__(
      "movabsq $0xDEADDEADDEADDEAD, %rax\n\t"
      "movq %rax, %rbx\n\t" "movq %rax, %rbp\n\t" "movq %rax, %rdi\n\t" "movq %rax, %rsi\n\t"
      "movq %rax, %r12\n\t" "movq %rax, %r13\n\t" "movq %rax, %r14\n\t" "movq %rax, %r15\n\t"
      "movq %rax, %xmm6\n\t" "movq %rax, %xmm7\n\t" "movq %rax, %xmm8\n\t" "movq %rax, %xmm9\n\t"
      "movq %rax, %xmm10\n\t" "movq %rax, %xmm11\n\t" "movq %rax, %xmm12\n\t" "movq %rax, %xmm13\n\t"
      "movq %rax, %xmm14\n\t" "movq %rax, %xmm15\n\t"
      "pushq $0\n\t"
      "movl $0x1FC0, (%rsp)\n\t" "ldmxcsr (%rsp)\n\t"
      "movw $0x007F, 4(%rsp)\n\t" "fldcw 4(%rsp)\n\t"
      "popq %rax\n\t"
      "jmp mpt_gc_switch_context\n\t");
}

constexpr std::uint16_t kProbeCw = 0x037F;

#else  // SysV

// SysV: rdi = save_sp, rsi = load_sp, rdx = out. Frame below the 6 pushes:
//   [0] out  [8] caller MXCSR/CW  [16] scratch  (24 bytes)
extern "C" __attribute__((naked)) void mpt_test_probe_switch(void**, void*, Probe*) {
  __asm__(
      "pushq %rbp\n\t" "pushq %rbx\n\t" "pushq %r12\n\t" "pushq %r13\n\t" "pushq %r14\n\t" "pushq %r15\n\t"
      "subq $24, %rsp\n\t"
      "movq %rdx, 0(%rsp)\n\t"
      "stmxcsr 8(%rsp)\n\t" "fnstcw 12(%rsp)\n\t"
      "movabsq $0x1111111111111111, %rbx\n\t"
      "movabsq $0x2222222222222222, %rbp\n\t"
      "movabsq $0x5555555555555555, %r12\n\t"
      "movabsq $0x6666666666666666, %r13\n\t"
      "movabsq $0x7777777777777777, %r14\n\t"
      "movabsq $0x8888888888888888, %r15\n\t"
      "movl $0x9FC0, 16(%rsp)\n\t" "ldmxcsr 16(%rsp)\n\t"
      "movw $0x027F, 20(%rsp)\n\t" "fldcw 20(%rsp)\n\t"
      "callq mpt_gc_switch_context\n\t"
      "movq 0(%rsp), %rax\n\t"
      "movq %rbx, 0(%rax)\n\t" "movq %rbp, 8(%rax)\n\t"
      "movq %r12, 32(%rax)\n\t" "movq %r13, 40(%rax)\n\t" "movq %r14, 48(%rax)\n\t" "movq %r15, 56(%rax)\n\t"
      "stmxcsr 224(%rax)\n\t" "fnstcw 228(%rax)\n\t"
      "ldmxcsr 8(%rsp)\n\t" "fldcw 12(%rsp)\n\t"
      "addq $24, %rsp\n\t"
      "popq %r15\n\t" "popq %r14\n\t" "popq %r13\n\t" "popq %r12\n\t" "popq %rbx\n\t" "popq %rbp\n\t"
      "retq\n\t");
}

extern "C" __attribute__((naked)) void mpt_test_clobber_and_switch(void**, void*) {
  __asm__(
      "movabsq $0xDEADDEADDEADDEAD, %rax\n\t"
      "movq %rax, %rbx\n\t" "movq %rax, %rbp\n\t"
      "movq %rax, %r12\n\t" "movq %rax, %r13\n\t" "movq %rax, %r14\n\t" "movq %rax, %r15\n\t"
      "pushq $0\n\t"
      "movl $0x1FC0, (%rsp)\n\t" "ldmxcsr (%rsp)\n\t"
      "movw $0x007F, 4(%rsp)\n\t" "fldcw 4(%rsp)\n\t"
      "popq %rax\n\t"
      "jmp mpt_gc_switch_context\n\t");
}

constexpr std::uint16_t kProbeCw = 0x027F;

#endif

namespace {

void clobberEntry(void*) {
  mpt_test_clobber_and_switch(&g_co_sp, g_host_sp);
  __builtin_trap();  // never resumed
}

}  // namespace

TEST_CASE("a switch round trip preserves every callee-saved register and the FP control state") {
  std::vector<std::byte> stack(64 * 1024);
  g_co_sp = make_context(stack.data(), stack.size(), &clobberEntry, nullptr);
  Probe p{};
  mpt_test_probe_switch(&g_host_sp, g_co_sp, &p);

  CHECK(p.gpr[0] == 0x1111111111111111ull);
  CHECK(p.gpr[1] == 0x2222222222222222ull);
#if defined(_WIN32)
  CHECK(p.gpr[2] == 0x3333333333333333ull);
  CHECK(p.gpr[3] == 0x4444444444444444ull);
#endif
  CHECK(p.gpr[4] == 0x5555555555555555ull);
  CHECK(p.gpr[5] == 0x6666666666666666ull);
  CHECK(p.gpr[6] == 0x7777777777777777ull);
  CHECK(p.gpr[7] == 0x8888888888888888ull);
#if defined(_WIN32)
  for (int i = 0; i < 10; ++i) {
    const std::uint64_t b = 0xA6u + static_cast<std::uint64_t>(i);
    const std::uint64_t pattern = b * 0x0101010101010101ull;
    CHECK(p.xmm[i][0] == pattern);
    CHECK(p.xmm[i][1] == pattern);
  }
  for (int i = 0; i < 4; ++i) CHECK(p.tib_after[i] == p.tib_before[i]);
#endif
  CHECK(p.mxcsr == 0x9FC0u);
  CHECK(p.x87cw == kProbeCw);
}

TEST_CASE("a fresh context starts with the deterministic MXCSR") {
  static std::uint32_t seen = 0;
  std::vector<std::byte> stack(64 * 1024);
  g_co_sp = make_context(stack.data(), stack.size(), [](void*) {
    seen = __builtin_ia32_stmxcsr();
    mpt::gc::detail::mpt_gc_switch_context(&g_co_sp, g_host_sp);
    __builtin_trap();
  }, nullptr);
  mpt::gc::detail::mpt_gc_switch_context(&g_host_sp, g_co_sp);
  CHECK(seen == mpt::gc::detail::kDeterministicMxcsr);
}
