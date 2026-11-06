// The game module's own symbol table, read from its image file at create (ADR 0008 amendment after P1-16
// hash coverage: "globals by symbol"). Host side: nothing here is game state, and nothing here is saved.
//
// Windows: the COFF symbol table lld-link writes into the DLL with /DEBUG:SYMTAB. Linux: the .so's
// .symtab. Both list static (local) symbols as well as external ones, with their names as the game's
// sources spell them, so the same names exist on both platforms whatever the section layout.
//
// It gives the state hash two things:
//  - the game's data symbols (every symbol inside the game core's data range or a REL's), by name, for
//    hashing globals in name order;
//  - a stable value for any word that points into the module: the symbol it points into, by name, and
//    the offset, or for a string literal its text.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mpt::mp4 {

enum class SectionKind : std::uint8_t { code, rodata, data };

// How canonical() resolved an address (for debug_hash_items' counts).
enum class PointerClass : std::uint8_t {
  symbol,   // a C symbol, by name and offset
  text,     // a string literal, by its text
  cxx,      // a C++ symbol: one value for all of them (MSVC and Itanium mangle differently)
  unknown,  // no symbol at or below it in its section, or no section
};

struct ModuleSymbol {
  std::uintptr_t addr = 0;
  std::uint64_t size = 0;  // ELF st_size; 0 when the format has none (COFF)
  std::uint64_t gap = 0;   // bytes to the next symbol or the end of its section
  std::uint64_t id = 0;    // FNV-1a 64 of the qualified name
  std::uint32_t section = 0;
  std::string name;        // qualified: "<range>:<name>" inside a game data range, else the plain name
};

// A range of game data (the game core's or one REL's): the name that qualifies its symbols.
struct GameRange {
  std::string name;
  std::uintptr_t lo = 0, hi = 0;
};

class ModuleSymbols {
 public:
  // Reads the image file of the module that contains this code. Fails with a message when the image
  // has no symbol table or is malformed.
  bool load(const std::vector<GameRange>& game_ranges, std::string& error);

  std::uintptr_t base() const { return base_; }
  std::uintptr_t end() const { return end_; }
  bool contains(std::uint64_t word) const { return word - base_ < end_ - base_; }

  // A stable value for an address inside the module (contains(p)): the C symbol at or below it and the
  // offset; a string literal in read-only data by its text; a C++ symbol as one value (its mangled name
  // differs between the MSVC and Itanium ABIs); anything else as one sentinel. Equal for the same
  // symbol and offset on every platform; can merge two addresses, never split one. `cls`, if given,
  // receives how it was resolved.
  std::uint64_t canonical(std::uintptr_t p, PointerClass* cls = nullptr) const;
  // canonical(), or 0 for a word outside the module (SdkConfig::pointer_id's contract).
  std::uint64_t idOf(const void* p) const;

  // The game's data symbols in qualified-name order (duplicates, from static symbols of the same name
  // in one range, are adjacent), plus a leading pseudo-symbol for any bytes of a range before its
  // first symbol.
  const std::vector<ModuleSymbol>& gameData() const { return game_; }
  // Symbols in the game data ranges whose names are C++ (host runtime objects that do not belong there).
  const std::vector<std::string>& hostSymbolsInGameData() const { return host_in_game_; }
  std::size_t symbolCount() const { return syms_.size(); }

 private:
  struct Section {
    std::uintptr_t lo = 0, hi = 0;
    SectionKind kind = SectionKind::data;
  };
  const Section* sectionOf(std::uintptr_t p) const;

  std::uintptr_t base_ = 0, end_ = 0;
  bool elf_ = false;  // ELF sizes are exact; COFF has none
  std::vector<Section> sections_;
  std::vector<ModuleSymbol> syms_;  // by address, then name
  std::vector<ModuleSymbol> game_;
  std::vector<std::string> host_in_game_;
};

std::uint64_t fnv1a64(std::string_view s);

}  // namespace mpt::mp4
