// mpt_mp4_module_symbols on this test's own image (#55 review): the PE/COFF or ELF symbol-table parser
// the state hash names globals and module pointers with. Synthetic: no game code, no dump.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "module_symbols.h"

namespace {

int g_failures = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                       \
    }                                                                     \
  } while (0)

}  // namespace

// A C global standing in for a game data range, and two C functions standing in for callbacks.
extern "C" {
volatile unsigned char mpt_test_range[64] = {1, 2, 3};
__attribute__((noinline)) int mpt_test_function_a(int x) { return x + mpt_test_range[0]; }
__attribute__((noinline)) int mpt_test_function_b(int x) { return x * 3 + mpt_test_range[1]; }
}
// A C++ function: its mangled name differs between ABIs, so it must hash as one shared value.
__attribute__((noinline)) int cxxHelperOne(int x) { return x - mpt_test_range[2]; }
__attribute__((noinline)) int cxxHelperTwo(int x) { return x ^ mpt_test_range[3]; }

int main() {
  using namespace mpt::mp4;
  const auto lo = reinterpret_cast<std::uintptr_t>(&mpt_test_range[0]);
  const std::vector<GameRange> ranges = {{"test", lo, lo + sizeof mpt_test_range}};
  ModuleSymbols syms;
  std::string error;
  const bool loaded = syms.load(ranges, error);
  if (!loaded) std::fprintf(stderr, "load: %s\n", error.c_str());
  CHECK(loaded);
  if (!loaded) return 1;
  CHECK(syms.symbolCount() > 0);

  // The range's symbol, qualified by the range's name and clamped to the range.
  bool found = false;
  for (const ModuleSymbol& s : syms.gameData()) {
    if (s.name == "test:mpt_test_range") {
      found = true;
      CHECK(s.addr == lo);
      CHECK(s.gap == sizeof mpt_test_range);
      CHECK(s.id == fnv1a64("test:mpt_test_range"));
    }
  }
  CHECK(found);
  CHECK(syms.hostSymbolsInGameData().empty());

  // Functions: inside the image, stable, distinct, and distinct by offset.
  const auto fa = reinterpret_cast<std::uintptr_t>(&mpt_test_function_a);
  const auto fb = reinterpret_cast<std::uintptr_t>(&mpt_test_function_b);
  CHECK(syms.contains(fa) && syms.contains(fb));
  PointerClass cls = PointerClass::unknown;
  const std::uint64_t ida = syms.canonical(fa, &cls);
  CHECK(cls == PointerClass::symbol);
  CHECK(ida == syms.canonical(fa));
  CHECK(ida != syms.canonical(fb));
  CHECK(syms.canonical(fa + 1) != ida);
  CHECK(syms.idOf(reinterpret_cast<const void*>(fa)) == ida);
  // Data symbols by name and offset.
  CHECK(syms.canonical(lo + 8, &cls) != syms.canonical(lo));
  CHECK(cls == PointerClass::symbol);

  // C++ symbols all share one value per section kind.
  const auto c1 = reinterpret_cast<std::uintptr_t>(&cxxHelperOne);
  const auto c2 = reinterpret_cast<std::uintptr_t>(&cxxHelperTwo);
  CHECK(syms.canonical(c1, &cls) == syms.canonical(c2));
  CHECK(cls == PointerClass::cxx);

  // String literals by their text, whatever their address.
  static const char* const kHello = "mpt module symbols test literal";
  const auto lit = reinterpret_cast<std::uintptr_t>(kHello);
  if (syms.contains(lit)) {
    CHECK(syms.canonical(lit, &cls) != syms.canonical(lit + 1));
    CHECK(cls == PointerClass::text);
  }

  // Outside the image: no ID.
  int on_stack = 0;
  CHECK(syms.idOf(&on_stack) == 0);
  CHECK(syms.idOf(nullptr) == 0);

  std::printf("mp4_module_symbols: %zu symbols, %d failure(s)\n", syms.symbolCount(), g_failures);
  return g_failures == 0 ? 0 : 1;
}
