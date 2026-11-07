// See module_symbols.h.
#include "module_symbols.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mpt::mp4 {

std::uint64_t fnv1a64(std::string_view s) {
  std::uint64_t h = 0xCBF29CE484222325ull;
  for (const char c : s) {
    h ^= static_cast<unsigned char>(c);
    h *= 0x100000001B3ull;
  }
  return h;
}

namespace {

// Distinct tags keep the classes of canonical values apart.
constexpr std::uint64_t kTagSymbol = 0x5359'4D42'0000'0001ull;   // "SYMB"
constexpr std::uint64_t kTagText = 0x5445'5854'0000'0001ull;     // "TEXT": a string literal
constexpr std::uint64_t kTagUnknown = 0x554E'4B4E'0000'0001ull;  // "UNKN": no symbol at or below
constexpr std::uint64_t kTagCxx = 0x4358'5850'0000'0001ull;      // "CXXP": any C++ symbol

std::uint64_t mix(std::uint64_t x) {  // splitmix64's finalizer
  x ^= x >> 30;
  x *= 0xBF58476D1CE4E5B9ull;
  x ^= x >> 27;
  x *= 0x94D049BB133111EBull;
  x ^= x >> 31;
  return x;
}

template <typename T>
bool readAt(const std::vector<unsigned char>& f, std::uint64_t off, T& out) {
  if (off > f.size() || f.size() - off < sizeof(T)) return false;
  std::memcpy(&out, f.data() + off, sizeof(T));
  return true;
}

bool readFile(const std::filesystem::path& path, std::vector<unsigned char>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size <= 0) return false;
  out.resize(static_cast<std::size_t>(size));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(out.data()), size);
  return static_cast<bool>(in);
}

// The image file and load address of the module that contains this function.
bool ownImage(std::filesystem::path& path, std::uintptr_t& base) {
#if defined(_WIN32)
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&ownImage), &module))
    return false;
  std::wstring name(32768, L'\0');
  const DWORD n = GetModuleFileNameW(module, name.data(), static_cast<DWORD>(name.size()));
  if (n == 0 || n >= name.size()) return false;
  name.resize(n);
  path = name;
  base = reinterpret_cast<std::uintptr_t>(module);
  return true;
#else
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(&ownImage), &info) == 0 || info.dli_fname == nullptr) return false;
  path = info.dli_fname;
  base = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
  return true;
#endif
}

struct RawSymbol {
  std::uintptr_t addr;
  std::uint64_t size;
  std::uint32_t section;
  std::string name;
};

struct RawSection {
  std::uintptr_t lo, hi;
  SectionKind kind;
};

// A name that is never a source-level symbol: section names, assembler temporaries, labels.
bool skipName(std::string_view n) { return n.empty() || n[0] == '.' || n[0] == '$' || n == "@feat.00"; }

// The layout probe's symbols (src/layout_probe.c), which exist only in a probe build: they still end the
// symbol before them, but are never hashed.
bool isProbe(std::string_view plain) { return plain.rfind("mpt_layout_probe", 0) == 0; }

[[maybe_unused]] bool parsePe(const std::vector<unsigned char>& f, std::uintptr_t base, std::vector<RawSection>& sections,
             std::vector<RawSymbol>& syms, std::uintptr_t& end, std::string& error) {
  std::uint32_t pe = 0, sig = 0;
  if (!readAt(f, 0x3C, pe) || !readAt(f, pe, sig) || sig != 0x00004550u) return error = "not a PE image", false;
  const std::uint64_t fh = pe + 4;
  std::uint16_t nsect = 0, opt_size = 0;
  std::uint32_t symtab = 0, nsyms = 0, image_size = 0;
  if (!readAt(f, fh + 2, nsect) || !readAt(f, fh + 8, symtab) || !readAt(f, fh + 12, nsyms) ||
      !readAt(f, fh + 16, opt_size) || !readAt(f, fh + 20 + 56, image_size))
    return error = "truncated PE header", false;
  if (symtab == 0 || nsyms == 0)
    return error = "the module image has no COFF symbol table (it must be linked with /DEBUG:SYMTAB)", false;
  end = base + image_size;
  const std::uint64_t sh = fh + 20 + opt_size;
  for (std::uint16_t i = 0; i < nsect; ++i) {
    std::uint32_t vsize = 0, va = 0, ch = 0;
    if (!readAt(f, sh + i * 40u + 8, vsize) || !readAt(f, sh + i * 40u + 12, va) || !readAt(f, sh + i * 40u + 36, ch))
      return error = "truncated PE section table", false;
    const SectionKind kind = (ch & 0x20000000u) != 0   ? SectionKind::code
                             : (ch & 0x80000000u) != 0 ? SectionKind::data
                                                       : SectionKind::rodata;
    sections.push_back({base + va, base + va + vsize, kind});
  }
  const std::uint64_t strtab = symtab + std::uint64_t{nsyms} * 18u;
  for (std::uint32_t i = 0; i < nsyms; ++i) {
    const std::uint64_t s = symtab + std::uint64_t{i} * 18u;
    std::uint32_t value = 0;
    std::int16_t secnum = 0;
    std::uint8_t cls = 0, aux = 0;
    if (!readAt(f, s + 8, value) || !readAt(f, s + 12, secnum) || !readAt(f, s + 16, cls) || !readAt(f, s + 17, aux))
      return error = "truncated COFF symbol table", false;
    const std::uint32_t this_aux = aux;
    if (secnum >= 1 && secnum <= nsect && (cls == 2 || cls == 3)) {  // EXTERNAL, STATIC
      std::string name;
      std::uint32_t zeros = 0, off = 0;
      readAt(f, s, zeros);
      if (zeros == 0) {
        readAt(f, s + 4, off);
        const std::uint64_t at = strtab + off;
        if (at < f.size()) name = reinterpret_cast<const char*>(f.data() + at);  // NUL-terminated in the file
      } else {
        char shortname[9] = {};
        std::memcpy(shortname, f.data() + s, 8);
        name = shortname;
      }
      if (!skipName(name)) {
        const RawSection& sec = sections[static_cast<std::size_t>(secnum - 1)];
        syms.push_back({sec.lo + value, 0, static_cast<std::uint32_t>(secnum - 1), std::move(name)});
      }
    }
    i += this_aux;
  }
  return true;
}

[[maybe_unused]] bool parseElf(const std::vector<unsigned char>& f, std::uintptr_t base, std::vector<RawSection>& sections,
              std::vector<RawSymbol>& syms, std::uintptr_t& end, std::string& error) {
  if (f.size() < 64 || std::memcmp(f.data(), "\x7f" "ELF", 4) != 0 || f[4] != 2) return error = "not an ELF64 image", false;
  std::uint64_t phoff = 0, shoff = 0;
  std::uint16_t phentsize = 0, phnum = 0, shentsize = 0, shnum = 0;
  readAt(f, 0x20, phoff);
  readAt(f, 0x28, shoff);
  readAt(f, 0x36, phentsize);
  readAt(f, 0x38, phnum);
  readAt(f, 0x3A, shentsize);
  readAt(f, 0x3C, shnum);
  std::uint64_t lo = ~0ull, hi = 0;
  for (std::uint16_t i = 0; i < phnum; ++i) {
    std::uint32_t type = 0;
    std::uint64_t vaddr = 0, memsz = 0;
    const std::uint64_t ph = phoff + std::uint64_t{i} * phentsize;
    if (!readAt(f, ph, type) || !readAt(f, ph + 16, vaddr) || !readAt(f, ph + 40, memsz))
      return error = "truncated ELF program headers", false;
    if (type != 1) continue;  // PT_LOAD
    lo = std::min(lo, vaddr & ~std::uint64_t{0xFFF});  // uint64_t is unsigned long on LP64
    hi = std::max(hi, vaddr + memsz);
  }
  if (hi == 0) return error = "the ELF image has no loadable segment", false;
  const std::uintptr_t bias = base - lo;
  end = bias + hi;
  std::uint64_t symoff = 0, symsize = 0, strsect = 0;
  for (std::uint16_t i = 0; i < shnum; ++i) {
    const std::uint64_t s = shoff + std::uint64_t{i} * shentsize;
    std::uint32_t type = 0, link = 0;
    std::uint64_t fl = 0, addr = 0, off = 0, size = 0;
    if (!readAt(f, s + 4, type) || !readAt(f, s + 8, fl) || !readAt(f, s + 16, addr) || !readAt(f, s + 24, off) ||
        !readAt(f, s + 32, size) || !readAt(f, s + 40, link))
      return error = "truncated ELF section headers", false;
    const SectionKind kind = (fl & 0x4) ? SectionKind::code : (fl & 0x1) ? SectionKind::data : SectionKind::rodata;
    // Non-allocated sections get an empty range: no runtime address lies in them.
    sections.push_back({(fl & 0x2) ? bias + addr : 0, (fl & 0x2) ? bias + addr + size : 0, kind});
    if (type == 2) {  // SHT_SYMTAB
      symoff = off;
      symsize = size;
      strsect = link;
    }
  }
  if (symoff == 0) return error = "the module image has no .symtab (it must not be stripped)", false;
  std::uint64_t stroff = 0, strsize = 0;
  if (!readAt(f, shoff + strsect * shentsize + 24, stroff) || !readAt(f, shoff + strsect * shentsize + 32, strsize))
    return error = "truncated ELF string table header", false;
  for (std::uint64_t s = symoff; s + 24 <= symoff + symsize; s += 24) {
    std::uint32_t name_off = 0;
    std::uint8_t info = 0;
    std::uint16_t shndx = 0;
    std::uint64_t value = 0, size = 0;
    readAt(f, s, name_off);
    readAt(f, s + 4, info);
    readAt(f, s + 6, shndx);
    readAt(f, s + 8, value);
    readAt(f, s + 16, size);
    const unsigned type = info & 0xF;
    if (shndx == 0 || shndx >= 0xFF00 || shndx >= shnum || (type != 0 && type != 1 && type != 2)) continue;
    if (sections[shndx].hi == 0 || name_off >= strsize) continue;
    std::string name = reinterpret_cast<const char*>(f.data() + stroff + name_off);
    if (skipName(name)) continue;
    syms.push_back({bias + value, size, shndx, std::move(name)});
  }
  return true;
}

// MSVC-ABI string literal symbols (??_C@...): ELF has no symbol for a literal, so neither may we.
bool isLiteralName(std::string_view n) { return n.rfind("??_C@", 0) == 0; }
// C++ names: never game state (the unordered_set lesson of P1-16).
bool isCxxName(std::string_view n) { return n.rfind("?", 0) == 0 || n.rfind("_Z", 0) == 0; }

}  // namespace

const ModuleSymbols::Section* ModuleSymbols::sectionOf(std::uintptr_t p) const {
  for (const Section& s : sections_)
    if (p >= s.lo && p < s.hi) return &s;
  return nullptr;
}

bool ModuleSymbols::load(const std::vector<GameRange>& game_ranges, std::string& error) {
  std::filesystem::path path;
  if (!ownImage(path, base_)) return error = "cannot find the module's own image", false;
  std::vector<unsigned char> file;
  if (!readFile(path, file)) return error = "cannot read the module image", false;
  std::vector<RawSection> raw_sections;
  std::vector<RawSymbol> raw;
#if defined(_WIN32)
  const bool ok = parsePe(file, base_, raw_sections, raw, end_, error);
#else
  const bool ok = parseElf(file, base_, raw_sections, raw, end_, error);
#endif
  if (!ok) return false;
  elf_ =
#if defined(_WIN32)
      false;
#else
      true;
#endif
  sections_.clear();
  for (const RawSection& s : raw_sections) sections_.push_back({s.lo, s.hi, s.kind});

  // Qualified names: inside a game data range, "<range>:<name>".
  auto rangeOf = [&](std::uintptr_t a) -> const GameRange* {
    for (const GameRange& r : game_ranges)
      if (a >= r.lo && a < r.hi) return &r;
    return nullptr;
  };
  syms_.clear();
  syms_.reserve(raw.size());
  for (RawSymbol& r : raw) {
    ModuleSymbol s;
    s.addr = r.addr;
    s.size = r.size;
    s.section = r.section;
    const GameRange* range = rangeOf(r.addr);
    s.name = range != nullptr ? range->name + ":" + r.name : std::move(r.name);
    s.id = fnv1a64(s.name);
    syms_.push_back(std::move(s));
  }
  std::sort(syms_.begin(), syms_.end(), [](const ModuleSymbol& a, const ModuleSymbol& b) {
    return a.addr != b.addr ? a.addr < b.addr : a.name < b.name;
  });
  // Gaps: to the next symbol at a higher address in the same section, or the section's end.
  for (std::size_t i = 0; i < syms_.size(); ++i) {
    ModuleSymbol& s = syms_[i];
    std::uintptr_t next = sections_[s.section].hi;
    for (std::size_t j = i + 1; j < syms_.size() && syms_[j].section == s.section; ++j) {
      if (syms_[j].addr > s.addr) {
        next = syms_[j].addr;
        break;
      }
      if (syms_[j].addr == s.addr) {  // an alias: only the last one at an address owns the bytes
        next = s.addr;
        break;
      }
    }
    s.gap = next > s.addr ? next - s.addr : 0;
  }

  // The game's data, in name order; a range's bytes before its first symbol are a pseudo-symbol.
  game_.clear();
  host_in_game_.clear();
  for (const GameRange& r : game_ranges) {
    if (r.hi <= r.lo) continue;
    std::uintptr_t first = r.hi;
    auto from = std::lower_bound(syms_.begin(), syms_.end(), r.lo,
                                 [](const ModuleSymbol& s, std::uintptr_t a) { return s.addr < a; });
    for (auto it = from; it != syms_.end() && it->addr < r.hi; ++it) {
      const ModuleSymbol& s = *it;
      first = std::min(first, s.addr);
      ModuleSymbol g = s;
      g.gap = std::min<std::uint64_t>(g.gap, r.hi - g.addr);
      const std::string_view plain = std::string_view(g.name).substr(r.name.size() + 1);
      if (isProbe(plain)) continue;
      if (isCxxName(plain)) host_in_game_.push_back(g.name);
      if (g.gap != 0) game_.push_back(std::move(g));
    }
    if (first > r.lo) {
      ModuleSymbol lead;
      lead.addr = r.lo;
      lead.gap = first - r.lo;
      lead.name = r.name + ":<lead>";
      lead.id = fnv1a64(lead.name);
      game_.push_back(std::move(lead));
    }
  }
  std::sort(game_.begin(), game_.end(), [](const ModuleSymbol& a, const ModuleSymbol& b) {
    return a.name != b.name ? a.name < b.name : a.addr < b.addr;
  });
  return true;
}

std::uint64_t ModuleSymbols::canonical(std::uintptr_t p, PointerClass* cls) const {
  PointerClass ignored;
  PointerClass& out = cls != nullptr ? *cls : ignored;
  out = PointerClass::unknown;
  const Section* sec = sectionOf(p);
  if (sec == nullptr) return mix(kTagUnknown);
  // The greatest symbol at or below p (the first of its aliases by name).
  auto it = std::upper_bound(syms_.begin(), syms_.end(), p,
                             [](std::uintptr_t a, const ModuleSymbol& s) { return a < s.addr; });
  const ModuleSymbol* sym = nullptr;
  if (it != syms_.begin()) {
    --it;
    const std::uintptr_t at = it->addr;
    while (it != syms_.begin() && (it - 1)->addr == at) --it;
    sym = &*it;
    if (sym->addr < sec->lo) sym = nullptr;  // in an earlier section
  }
  if (sec->kind == SectionKind::rodata) {
    const bool literal =
        sym == nullptr || isLiteralName(sym->name) || (elf_ && p >= sym->addr + sym->size);
    if (literal) {
      // Its text up to and including the NUL, at most 64 bytes: the same on every platform.
      const auto* c = reinterpret_cast<const unsigned char*>(p);
      const std::size_t room = static_cast<std::size_t>(std::min<std::uintptr_t>(sec->hi - p, 64));
      std::uint64_t h = 0xCBF29CE484222325ull;
      for (std::size_t i = 0; i < room; ++i) {
        h ^= c[i];
        h *= 0x100000001B3ull;
        if (c[i] == 0) break;
      }
      out = PointerClass::text;
      return mix(kTagText ^ h);
    }
  }
  if (sym == nullptr) return mix(kTagUnknown ^ static_cast<std::uint64_t>(sec->kind));
  if (isCxxName(sym->name)) {
    out = PointerClass::cxx;
    return mix(kTagCxx ^ static_cast<std::uint64_t>(sec->kind));
  }
  out = PointerClass::symbol;
  return mix(kTagSymbol ^ mix(sym->id + 0x9E3779B97F4A7C15ull * (p - sym->addr)));
}

std::string ModuleSymbols::nameAt(std::uintptr_t p) const {
  auto it = std::upper_bound(syms_.begin(), syms_.end(), p,
                             [](std::uintptr_t a, const ModuleSymbol& s) { return a < s.addr; });
  if (it == syms_.begin()) return {};
  --it;
  char off[24];
  std::snprintf(off, sizeof off, "+0x%llx", static_cast<unsigned long long>(p - it->addr));
  return it->name + off;
}

std::uint64_t ModuleSymbols::idOf(const void* p) const {
  const auto a = reinterpret_cast<std::uintptr_t>(p);
  if (!contains(a)) return 0;
  const std::uint64_t v = canonical(a);
  return v != 0 ? v : 1;
}

}  // namespace mpt::mp4
