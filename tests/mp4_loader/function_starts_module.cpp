// A stub module for the function-start check (test_function_starts.cpp; ADR 0008, "Module words: a
// safe rule must not depend on layout", point 6). It is linked like the game module: at the fixed base,
// with ICF and incremental linking off (mpt_mp4_fixed_base_link) and a symbol table
// (mpt_mp4_link_symtab), by the real lld-link or ld.lld of the Clang build. It reads its own symbol
// table with mpt_mp4_module_symbols and reports whether a static C function, an extern C function and
// an assembly function (function_starts_stub.c) are function starts. No game code and no dump.
#include <cstdint>
#include <cstdio>
#include <string>

#include "module_symbols.h"
#include "mpt/mp4/game_abi.h"

extern "C" void mpt_stub_function_addresses(std::uintptr_t out[3]);

// Writes a report of up to `size` bytes and returns the number of failed checks (-1 if the symbol
// table cannot be read).
extern "C" MPT_MP4_EXPORT int mpt_mp4_stub_function_starts(char* report, std::size_t size) {
  using namespace mpt::mp4;
  std::string text;
  ModuleSymbols syms;
  std::string error;
  int failures = 0;
  if (!syms.load({}, error)) {
    text = "load: " + error + "\n";
    failures = -1;
  } else {
    std::uintptr_t fn[3];
    mpt_stub_function_addresses(fn);
    const char* const kinds[3] = {"static C", "extern C", "assembly"};
    for (int i = 0; i < 3; ++i) {
      PointerClass start = PointerClass::raw, inside = PointerClass::raw;
      const bool ok_start = syms.contains(fn[i]) && syms.canonical(fn[i], &start) != fn[i] && start == PointerClass::symbol;
      const bool ok_inside = syms.canonical(fn[i] + 1, &inside) == fn[i] + 1 && inside == PointerClass::code_interior;
      char line[256];
      std::snprintf(line, sizeof line, "%s function %s: start %s, +1 %s\n", kinds[i], syms.nameAt(fn[i]).c_str(),
                    ok_start ? "is a function start" : "NOT a function start",
                    ok_inside ? "raw" : "NOT raw");
      text += line;
      failures += (ok_start ? 0 : 1) + (ok_inside ? 0 : 1);
    }
  }
  if (report != nullptr && size != 0) std::snprintf(report, size, "%s", text.c_str());
  return failures;
}
