#pragma once

// GameCube disc reader (Phase 1 work item P1-09).
//
// Reads plain, full GameCube disc images (.iso/.gcm): the disc header, the
// apploader and DOL bounds, and the file system table (FST). Every structure is
// validated before anything is trusted, because the image is user-supplied:
// names that could escape the output directory, entries that overlap or run past
// the end of the image, malformed directory nesting and oversize tables are all
// rejected with a message.
//
// Compressed or trimmed containers (RVZ, WIA, GCZ, WBFS, CISO, NKit) are out of
// scope (Phase 1 decision D9) and are rejected with a message naming the format.
// Nothing here decrypts anything: GameCube discs are not encrypted.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mpt/asset_import/byte_source.h"
#include "mpt/core/result.h"

namespace mpt::asset_import {

// Size of a full retail GameCube disc image (a 1.4 GB "mini DVD").
inline constexpr std::uint64_t kGcFullDiscSize = 1'459'978'240;

// Fixed system areas of a GameCube disc.
inline constexpr std::uint64_t kGcHeaderSize = 0x440;      // boot.bin
inline constexpr std::uint64_t kGcBi2Offset = 0x440;
inline constexpr std::uint64_t kGcBi2Size = 0x2000;        // bi2.bin
inline constexpr std::uint64_t kGcApploaderOffset = 0x2440;
inline constexpr std::uint64_t kGcApploaderHeaderSize = 0x20;

// Limits far above any retail disc, so malformed images cannot make us allocate
// or walk unbounded amounts of data.
inline constexpr std::uint32_t kGcMaxFstSize = 16u << 20;
inline constexpr std::uint32_t kGcMaxApploaderSize = 16u << 20;
inline constexpr std::uint32_t kGcMaxDolSize = 32u << 20;
inline constexpr std::size_t kGcMaxPathLength = 512;
inline constexpr std::size_t kGcMaxNameLength = 255;

struct GcDiscHeader {
  std::string disc_id;  // 6 characters: game code (4) + maker code (2), e.g. "GMPE01"
  std::uint8_t disc_number = 0;
  std::uint8_t version = 0;  // revision byte: 0x00 = Rev 0
  std::string game_name;     // internal name, printable ASCII (other bytes become '?')
  std::uint32_t dol_offset = 0;
  std::uint32_t fst_offset = 0;
  std::uint32_t fst_size = 0;
  std::uint32_t fst_max_size = 0;

  std::string game_code() const { return disc_id.substr(0, 4); }
  std::string maker_code() const { return disc_id.substr(4, 2); }
  char region_code() const { return disc_id.size() > 3 ? disc_id[3] : '\0'; }
};

// Reads only the disc header (boot.bin): checks the GameCube magic, refuses NKit and
// Wii images, and parses the disc ID, version and table offsets. It does not look at
// the FST, so it also works on images that GcDisc::open would refuse.
std::optional<GcDiscHeader> read_gc_disc_header(const ByteSource& source, std::string& error);

// Why an FST name is unsafe to create on disk (path separators, ".", "..", control or
// non-ASCII bytes, a trailing dot or space, Windows device names...), or an empty
// string if it is fine.
std::string gc_name_problem(std::string_view name);

// Longest output path extract_gc_disc() creates, in characters, counting the
// destination folder. Windows refuses longer paths unless the executable opts into
// long paths in its manifest (a shell decision, not asset-import's).
#ifdef _WIN32
inline constexpr std::size_t kGcMaxOutputPathLength = 259;
#else
inline constexpr std::size_t kGcMaxOutputPathLength = 4095;
#endif

// Region name for the 4th disc-ID character: 'E' → "USA", 'P' → "EUR", 'J' → "JPN", ...
// Returns "unknown" for codes this table does not list.
std::string_view gc_region_name(char region_code);

// Revision label for the version byte, e.g. 0x00 → "Rev 0".
std::string gc_revision_name(std::uint8_t version);

// The decomp-style version tag, e.g. disc ID "GMPE01" with version 0x00 → "GMPE01_00".
// Discs after the first get a suffix: "GMPE01_00_disc2".
std::string gc_version_tag(const GcDiscHeader& header);

// One file in the FST.
struct GcFile {
  std::string path;  // relative, '/'-separated, every component validated (see gc_disc.cpp)
  std::uint32_t offset = 0;
  std::uint32_t size = 0;
};

class GcDisc {
 public:
  // Reads and validates a plain GameCube disc image with every structural and safety
  // rule. On failure `error` says why, in words a user can act on. The importer
  // identifies dumps by hash first and uses this to explain a mismatch; extraction
  // requires a GcDisc, so the rules always hold for what is written to disk.
  static std::optional<GcDisc> open(const ByteSource& source, std::string& error);

  const GcDiscHeader& header() const { return header_; }
  std::uint64_t image_size() const { return image_size_; }
  bool is_full_size() const { return image_size_ == kGcFullDiscSize; }

  std::uint32_t apploader_size() const { return apploader_size_; }  // header + body + trailer
  std::uint32_t dol_size() const { return dol_size_; }

  const std::vector<GcFile>& files() const { return files_; }              // FST order
  const std::vector<std::string>& directories() const { return dirs_; }    // FST order, parents first
  const GcFile* find(std::string_view path) const;                        // exact, case-sensitive

 private:
  GcDiscHeader header_;
  std::uint64_t image_size_ = 0;
  std::uint32_t apploader_size_ = 0;
  std::uint32_t dol_size_ = 0;
  std::vector<GcFile> files_;
  std::vector<std::string> dirs_;
};

// Extracts a validated disc into `dest` in the layout Dolphin uses:
//   sys/boot.bin, sys/bi2.bin, sys/apploader.img, sys/main.dol, sys/fst.bin
//   files/<FST paths>
// `dest` must not exist or must be an empty directory. Every output path must fit
// in kGcMaxOutputPathLength; this is checked before anything is written. On
// failure, anything this call created is removed again, including parent folders
// of `dest` that it had to create. The output stays on the user's machine: callers
// put it in the local cache (build_dir) and nowhere else.
Result extract_gc_disc(const ByteSource& source, const GcDisc& disc, const std::filesystem::path& dest);

}  // namespace mpt::asset_import
