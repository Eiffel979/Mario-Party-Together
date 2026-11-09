// mpt_mp4_module_symbols (#55 review): the PE/COFF and ELF symbol-table readers the state hash names
// globals and module pointers with, and ModuleSymbols::canonical()'s rules (ADR 0008, "Module words: a
// safe rule must not depend on layout"). Two parts:
//  - this test's own image, as the real linker wrote it;
//  - a synthetic PE/COFF image and a synthetic ELF64 image with the same symbols, built here byte by
//    byte, so both readers are checked on every platform.
// Synthetic: no game code, no dump.
#include <cstdint>
#include <cstdio>
#include <cstring>
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

namespace {

using namespace mpt::mp4;

void testOwnImage() {
  const auto lo = reinterpret_cast<std::uintptr_t>(&mpt_test_range[0]);
  const std::vector<GameRange> ranges = {{"test", lo, lo + sizeof mpt_test_range}};
  ModuleSymbols syms;
  std::string error;
  const bool loaded = syms.load(ranges, error);
  if (!loaded) std::fprintf(stderr, "load: %s\n", error.c_str());
  CHECK(loaded);
  if (!loaded) return;
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

  // Functions: inside the image, stable, distinct, and only at their exact start.
  const auto fa = reinterpret_cast<std::uintptr_t>(&mpt_test_function_a);
  const auto fb = reinterpret_cast<std::uintptr_t>(&mpt_test_function_b);
  CHECK(syms.contains(fa) && syms.contains(fb));
  PointerClass cls = PointerClass::unknown;
  const std::uint64_t ida = syms.canonical(fa, &cls);
  CHECK(cls == PointerClass::symbol);
  CHECK(ida == syms.canonical(fa));
  CHECK(ida != syms.canonical(fb));
  CHECK(syms.idOf(reinterpret_cast<const void*>(fa)) == ida);
  // One byte in: not a function start, so the word itself (#66 review, nit h).
  CHECK(syms.canonical(fa + 1, &cls) == fa + 1);
  CHECK(cls == PointerClass::code_interior || cls == PointerClass::raw);
  CHECK(isRaw(cls));
  // Data symbols by name and offset.
  CHECK(syms.canonical(lo + 8, &cls) != syms.canonical(lo));
  CHECK(cls == PointerClass::symbol);

  // C++ function starts all share one value.
  const auto c1 = reinterpret_cast<std::uintptr_t>(&cxxHelperOne);
  const auto c2 = reinterpret_cast<std::uintptr_t>(&cxxHelperTwo);
  CHECK(syms.canonical(c1, &cls) == syms.canonical(c2));
  CHECK(cls == PointerClass::cxx);
  CHECK(syms.canonical(c1 + 1, &cls) == c1 + 1);
  CHECK(isRaw(cls));

  // The image's headers lie in no section: raw.
  CHECK(syms.canonical(syms.base() + 8, &cls) == syms.base() + 8);
  CHECK(cls == PointerClass::raw);

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
  std::printf("mp4_module_symbols: own image, %zu symbols\n", syms.symbolCount());
}

// ---- Synthetic images ------------------------------------------------------------------------------
//
// One layout, offsets from the image base, in both formats:
//   [0x0000, 0x1000)  headers, in no section
//   [0x1000, 0x1100)  .text: the section symbol at 0x1000, then
//                     0x1010 mpt_syn_function_a   function, extern
//                     0x1040 mpt_syn_label        untyped, static (a label)
//                     0x1060 mpt_syn_text_object  data, extern (an object in code)
//                     0x1080 mpt_syn_function_b   function, static
//                     0x10A0 mpt_layout_probe_code function (a layout probe symbol)
//                     0x10C0 C++ function one, 0x10E0 C++ function two
//   [0x2000, 0x2100)  .data: 0x2010 mpt_syn_data, 0x20 bytes
//   [0x2100, 0x3000)  a gap, in no section
constexpr std::uint32_t kImageSize = 0x3000;

enum class SymKind { function, label, object };
struct SynSym {
  const char* elf_name;
  const char* coff_name;
  std::uint32_t off;
  int section;  // 1 .text, 2 .data
  SymKind kind;
  bool global;
};
const SynSym kSyms[] = {
    {"mpt_syn_function_a", "mpt_syn_function_a", 0x1010, 1, SymKind::function, true},
    {"mpt_syn_label", "mpt_syn_label", 0x1040, 1, SymKind::label, false},
    {"mpt_syn_text_object", "mpt_syn_text_object", 0x1060, 1, SymKind::object, true},
    {"mpt_syn_function_b", "mpt_syn_function_b", 0x1080, 1, SymKind::function, false},
    {"mpt_layout_probe_code", "mpt_layout_probe_code", 0x10A0, 1, SymKind::function, true},
    {"_Z9cxxSynOnev", "?cxxSynOne@@YAHXZ", 0x10C0, 1, SymKind::function, true},
    {"_Z9cxxSynTwov", "?cxxSynTwo@@YAHXZ", 0x10E0, 1, SymKind::function, true},
    {"mpt_syn_data", "mpt_syn_data", 0x2010, 2, SymKind::object, true},
};

struct Writer {
  std::vector<unsigned char> f;
  template <typename T>
  void put(std::size_t at, T v) {
    if (f.size() < at + sizeof v) f.resize(at + sizeof v);
    std::memcpy(f.data() + at, &v, sizeof v);
  }
  void bytes(std::size_t at, const void* p, std::size_t n) {
    if (f.size() < at + n) f.resize(at + n);
    std::memcpy(f.data() + at, p, n);
  }
};

std::vector<unsigned char> syntheticPe() {
  Writer w;
  w.f.assign(0x200, 0);
  w.f[0] = 'M';
  w.f[1] = 'Z';
  w.put<std::uint32_t>(0x3C, 0x80);
  w.bytes(0x80, "PE\0\0", 4);
  const std::size_t fh = 0x84, opt = fh + 20, opt_size = 240, sh = opt + opt_size;
  const std::size_t symtab = 0x400;
  w.put<std::uint16_t>(fh + 0, 0x8664);
  w.put<std::uint16_t>(fh + 2, 2);
  w.put<std::uint32_t>(fh + 8, static_cast<std::uint32_t>(symtab));
  w.put<std::uint16_t>(fh + 16, static_cast<std::uint16_t>(opt_size));
  w.put<std::uint16_t>(opt, 0x20B);  // PE32+
  w.put<std::uint32_t>(opt + 56, kImageSize);
  struct Sec {
    const char* name;
    std::uint32_t va, ch;
  } secs[] = {{".text", 0x1000, 0x60000020u}, {".data", 0x2000, 0xC0000040u}};
  for (int i = 0; i < 2; ++i) {
    w.bytes(sh + i * 40, secs[i].name, std::strlen(secs[i].name));
    w.put<std::uint32_t>(sh + i * 40 + 8, 0x100);
    w.put<std::uint32_t>(sh + i * 40 + 12, secs[i].va);
    w.put<std::uint32_t>(sh + i * 40 + 36, secs[i].ch);
  }
  // Symbols: the section symbol with one auxiliary record, then kSyms. Long names in the string table.
  std::string strtab(4, '\0');
  std::uint32_t n = 0;
  auto sym = [&](const char* name, std::uint32_t value, std::int16_t secnum, std::uint16_t type, std::uint8_t cls,
                 std::uint8_t aux) {
    const std::size_t s = symtab + n * 18u;
    w.put<std::uint64_t>(s, 0);
    if (std::strlen(name) <= 8) {
      w.bytes(s, name, std::strlen(name));
    } else {
      w.put<std::uint32_t>(s, 0);
      w.put<std::uint32_t>(s + 4, static_cast<std::uint32_t>(strtab.size()));
      strtab += name;
      strtab += '\0';
    }
    w.put<std::uint32_t>(s + 8, value);
    w.put<std::int16_t>(s + 12, secnum);
    w.put<std::uint16_t>(s + 14, type);
    w.put<std::uint8_t>(s + 16, cls);
    w.put<std::uint8_t>(s + 17, aux);
    ++n;
    for (std::uint8_t a = 0; a < aux; ++a) {
      // An auxiliary record that would read as a function symbol if the reader did not skip it.
      const std::size_t x = symtab + n * 18u;
      w.bytes(x, "mpt_aux", 7);
      w.put<std::uint32_t>(x + 8, 0x30);
      w.put<std::int16_t>(x + 12, 1);
      w.put<std::uint16_t>(x + 14, 0x20);
      w.put<std::uint8_t>(x + 16, 2);
      w.put<std::uint8_t>(x + 17, 0);
      ++n;
    }
  };
  sym(".text", 0, 1, 0, 3, 1);
  for (const SynSym& s : kSyms) {
    const std::uint32_t sec_va = s.section == 1 ? 0x1000u : 0x2000u;
    sym(s.coff_name, s.off - sec_va, static_cast<std::int16_t>(s.section),
        s.kind == SymKind::function ? 0x20 : 0, s.global ? 2 : 3, 0);
  }
  w.put<std::uint32_t>(fh + 12, n);
  const std::size_t str_at = symtab + n * 18u;
  const auto size = static_cast<std::uint32_t>(strtab.size());
  std::memcpy(strtab.data(), &size, 4);
  w.bytes(str_at, strtab.data(), strtab.size());
  return w.f;
}

std::vector<unsigned char> syntheticElf() {
  // Linked at 0x200000 (lld's --image-base would give the real base): loading at another base biases
  // every address, as glibc's loader does for a shared object.
  constexpr std::uint64_t kLink = 0x200000;
  Writer w;
  w.f.assign(0x40, 0);
  w.bytes(0, "\x7f" "ELF", 4);
  w.f[4] = 2;  // ELFCLASS64
  w.f[5] = 1;  // little endian
  w.f[6] = 1;
  w.put<std::uint16_t>(0x10, 3);     // ET_DYN
  w.put<std::uint16_t>(0x12, 0x3E);  // x86-64
  w.put<std::uint64_t>(0x20, 0x40);  // e_phoff
  w.put<std::uint16_t>(0x36, 56);
  w.put<std::uint16_t>(0x38, 1);
  w.put<std::uint16_t>(0x3A, 64);
  // One PT_LOAD covering the image.
  w.put<std::uint32_t>(0x40, 1);
  w.put<std::uint64_t>(0x40 + 16, kLink);
  w.put<std::uint64_t>(0x40 + 40, kImageSize);
  // Symbols and strings.
  std::string strtab(1, '\0');
  std::vector<unsigned char> symbols(24, 0);  // the null symbol
  auto sym = [&](const char* name, std::uint64_t value, std::uint16_t shndx, std::uint8_t info, std::uint64_t size) {
    unsigned char e[24] = {};
    const auto name_off = static_cast<std::uint32_t>(strtab.size());
    strtab += name;
    strtab += '\0';
    std::memcpy(e, &name_off, 4);
    e[4] = info;
    std::memcpy(e + 6, &shndx, 2);
    std::memcpy(e + 8, &value, 8);
    std::memcpy(e + 16, &size, 8);
    symbols.insert(symbols.end(), e, e + 24);
  };
  sym("", kLink + 0x1000, 1, 0x03, 0);  // STT_SECTION, STB_LOCAL
  for (const SynSym& s : kSyms) {
    const std::uint8_t type = s.kind == SymKind::function ? 2 : s.kind == SymKind::object ? 1 : 0;
    const std::uint8_t bind = s.global ? 1 : 0;
    sym(s.elf_name, kLink + s.off, static_cast<std::uint16_t>(s.section), static_cast<std::uint8_t>(bind << 4 | type),
        s.section == 2 ? 0x20 : 0x10);
  }
  const std::string shstr = std::string("\0.text\0.data\0.symtab\0.strtab\0.shstrtab\0", 40);
  const std::size_t symoff = 0x1000, stroff = symoff + symbols.size(), shstroff = stroff + strtab.size();
  w.bytes(symoff, symbols.data(), symbols.size());
  w.bytes(stroff, strtab.data(), strtab.size());
  w.bytes(shstroff, shstr.data(), shstr.size());
  const std::size_t shoff = (shstroff + shstr.size() + 7) & ~std::size_t{7};
  struct Sh {
    std::uint32_t name, type;
    std::uint64_t flags, addr, off, size;
    std::uint32_t link;
    std::uint64_t entsize;
  } shs[] = {
      {0, 0, 0, 0, 0, 0, 0, 0},
      {1, 1, 0x6, kLink + 0x1000, 0, 0x100, 0, 0},   // .text: ALLOC | EXECINSTR
      {7, 1, 0x3, kLink + 0x2000, 0, 0x100, 0, 0},   // .data: WRITE | ALLOC
      {13, 2, 0, 0, symoff, symbols.size(), 4, 24},  // .symtab, its strings in section 4
      {21, 3, 0, 0, stroff, strtab.size(), 0, 0},    // .strtab
      {29, 3, 0, 0, shstroff, shstr.size(), 0, 0},   // .shstrtab
  };
  for (std::size_t i = 0; i < std::size(shs); ++i) {
    const std::size_t s = shoff + i * 64;
    w.put<std::uint32_t>(s, shs[i].name);
    w.put<std::uint32_t>(s + 4, shs[i].type);
    w.put<std::uint64_t>(s + 8, shs[i].flags);
    w.put<std::uint64_t>(s + 16, shs[i].addr);
    w.put<std::uint64_t>(s + 24, shs[i].off);
    w.put<std::uint64_t>(s + 32, shs[i].size);
    w.put<std::uint32_t>(s + 40, shs[i].link);
    w.put<std::uint64_t>(s + 56, shs[i].entsize);
  }
  w.put<std::uint64_t>(0x28, shoff);
  w.put<std::uint16_t>(0x3C, static_cast<std::uint16_t>(std::size(shs)));
  w.put<std::uint16_t>(0x3E, 5);
  return w.f;
}

struct Starts {
  std::uint64_t a = 0, b = 0, cxx = 0, data = 0;
};

// The rules on one synthetic image at one base. Returns the layout-independent values.
Starts checkSynthetic(const char* format, const std::vector<unsigned char>& image, std::uintptr_t base) {
  Starts out;
  ModuleSymbols syms;
  std::string error;
  const bool loaded = syms.loadImage(image, base, {}, error);
  if (!loaded) std::fprintf(stderr, "%s: loadImage: %s\n", format, error.c_str());
  CHECK(loaded);
  if (!loaded) return out;
  CHECK(syms.base() == base);
  CHECK(syms.end() == base + kImageSize);
  CHECK(syms.symbolCount() == std::size(kSyms));  // no section symbol, no auxiliary record
  PointerClass cls = PointerClass::unknown;
  const auto at = [base](std::uint32_t off) { return base + off; };
  const auto isRawAt = [&](std::uint32_t off, PointerClass want) {
    PointerClass c = PointerClass::symbol;
    const bool same = syms.canonical(at(off), &c) == at(off);
    if (!same || c != want) std::fprintf(stderr, "%s: offset 0x%x is not raw as expected\n", format, off);
    return same && c == want;
  };

  // An exact function start canonicalizes to its name's ID, an extern and a static one alike.
  out.a = syms.canonical(at(0x1010), &cls);
  CHECK(cls == PointerClass::symbol);
  CHECK(out.a != at(0x1010));
  out.b = syms.canonical(at(0x1080), &cls);
  CHECK(cls == PointerClass::symbol);
  CHECK(out.b != out.a);
  CHECK(syms.idOf(reinterpret_cast<const void*>(at(0x1010))) == out.a);

  // Interior, padding and below the section's first function: raw.
  CHECK(isRawAt(0x1011, PointerClass::code_interior));
  CHECK(isRawAt(0x1030, PointerClass::code_interior));
  CHECK(isRawAt(0x1000, PointerClass::code_interior));  // the section symbol is not a start
  CHECK(isRawAt(0x1008, PointerClass::code_interior));
  // A label and a data symbol in code are not starts, nor is a layout probe symbol.
  CHECK(isRawAt(0x1040, PointerClass::code_interior));
  CHECK(isRawAt(0x1060, PointerClass::code_interior));
  CHECK(isRawAt(0x10A0, PointerClass::code_interior));
  // Headers and the gap after the last section: in no section, raw.
  CHECK(isRawAt(0x0030, PointerClass::raw));
  CHECK(isRawAt(0x0FF8, PointerClass::raw));
  CHECK(isRawAt(0x2100, PointerClass::raw));
  CHECK(isRawAt(0x2FF8, PointerClass::raw));
  CHECK(syms.idOf(reinterpret_cast<const void*>(at(0x2100))) == at(0x2100));

  // C++ function starts: one shared value; inside one, raw.
  out.cxx = syms.canonical(at(0x10C0), &cls);
  CHECK(cls == PointerClass::cxx);
  CHECK(syms.canonical(at(0x10E0), &cls) == out.cxx);
  CHECK(cls == PointerClass::cxx);
  CHECK(isRawAt(0x10C4, PointerClass::code_interior));

  // Data: by symbol and offset, unchanged; below the first data symbol, the sentinel.
  out.data = syms.canonical(at(0x2018), &cls);
  CHECK(cls == PointerClass::symbol);
  CHECK(out.data != syms.canonical(at(0x2010)));
  syms.canonical(at(0x2008), &cls);
  CHECK(cls == PointerClass::unknown);

  CHECK(syms.contains(at(kImageSize - 1)));
  CHECK(!syms.contains(at(kImageSize)));
  CHECK(!syms.contains(base - 1));
  return out;
}

void testSynthetic() {
  const std::vector<unsigned char> pe = syntheticPe(), elf = syntheticElf();
  constexpr std::uintptr_t kBase = 0x5B97'9E3C'0000ull, kOther = 0x5B97'A000'0000ull;
  const Starts pe1 = checkSynthetic("coff", pe, kBase);
  const Starts pe2 = checkSynthetic("coff", pe, kOther);
  const Starts elf1 = checkSynthetic("elf", elf, kBase);
  const Starts elf2 = checkSynthetic("elf", elf, kOther);
  // The canonical values depend on names and offsets only: the same at any base and in either format.
  for (const Starts* s : {&pe2, &elf1, &elf2}) {
    CHECK(s->a == pe1.a);
    CHECK(s->b == pe1.b);
    CHECK(s->cxx == pe1.cxx);
    CHECK(s->data == pe1.data);
  }
  // Neither format nor garbage is refused.
  ModuleSymbols none;
  std::string error;
  const std::vector<unsigned char> junk(64, 0x5A);
  CHECK(!none.loadImage(junk, kBase, {}, error));
  CHECK(!error.empty());
  std::printf("mp4_module_symbols: synthetic COFF and ELF images checked\n");
}

}  // namespace

int main() {
  testOwnImage();
  testSynthetic();
  std::printf("mp4_module_symbols: %d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
