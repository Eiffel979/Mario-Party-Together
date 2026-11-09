/* Functions for the function-start stub module (function_starts_module.cpp): a static C function, an
 * extern C function and a function written in assembly, each with its address taken. The real linker
 * (lld-link or ld.lld) must keep each one's function type in the image's symbol table (ADR 0008,
 * "Module words: a safe rule must not depend on layout", point 6). Synthetic: no game code. */
#include <stdint.h>

int mpt_stub_extern_function(int x);
int mpt_stub_asm_function(int x);

static int mpt_stub_static_function(int x) { return x * 7 + 1; }

int mpt_stub_extern_function(int x) { return x * 5 + 2; }

/* Hand-written assembly carries no function type unless it says so: .type on ELF, .def/.scl/.type 32
 * (IMAGE_SYM_DTYPE_FUNCTION << 4) on COFF. */
#if defined(_WIN32)
/* clang-cl assembles inline asm in Intel syntax. */
__asm__(".text\n"
        ".globl mpt_stub_asm_function\n"
        ".def mpt_stub_asm_function; .scl 2; .type 32; .endef\n"
        ".p2align 4\n"
        "mpt_stub_asm_function:\n"
        "  lea eax, [rcx + 3]\n"
        "  ret\n");
#else
__asm__(".text\n"
        ".globl mpt_stub_asm_function\n"
        ".hidden mpt_stub_asm_function\n"
        ".type mpt_stub_asm_function, @function\n"
        ".p2align 4\n"
        "mpt_stub_asm_function:\n"
        "  leal 3(%rdi), %eax\n"
        "  ret\n"
        ".size mpt_stub_asm_function, .-mpt_stub_asm_function\n");
#endif

/* The three addresses, in this order: static, extern, assembly. */
void mpt_stub_function_addresses(uintptr_t out[3]) {
  out[0] = (uintptr_t)&mpt_stub_static_function;
  out[1] = (uintptr_t)&mpt_stub_extern_function;
  out[2] = (uintptr_t)&mpt_stub_asm_function;
}
