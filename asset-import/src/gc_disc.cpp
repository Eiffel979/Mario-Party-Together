#include "mpt/asset_import/gc_disc.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <set>
#include <span>

namespace mpt::asset_import {
namespace {

constexpr std::uint64_t kGcMagicOffset = 0x1C;
constexpr std::uint32_t kGcMagic = 0xC2339F3D;
constexpr std::uint64_t kWiiMagicOffset = 0x18;
constexpr std::uint32_t kWiiMagic = 0x5D1C9EA3;
constexpr std::uint64_t kNkitMagicOffset = 0x200;  // NKit writes "NKIT" into the unused header area
constexpr std::uint64_t kGameNameOffset = 0x20;
constexpr std::size_t kGameNameSize = 0x3E0;
constexpr std::uint32_t kFstEntrySize = 12;
constexpr std::uint32_t kDolHeaderSize = 0x100;
constexpr int kDolSections = 18;  // 7 text + 11 data
constexpr std::uint32_t kDolSizesOffset = 0x90;
constexpr std::size_t kCopyChunk = 1 << 20;

std::uint32_t be32(std::span<const std::byte> data, std::size_t offset) {
  return (std::uint32_t{std::to_integer<std::uint8_t>(data[offset])} << 24) |
         (std::uint32_t{std::to_integer<std::uint8_t>(data[offset + 1])} << 16) |
         (std::uint32_t{std::to_integer<std::uint8_t>(data[offset + 2])} << 8) |
         std::uint32_t{std::to_integer<std::uint8_t>(data[offset + 3])};
}

std::string hex(std::uint64_t value) {
  char text[24];
  std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(value));
  return text;
}

bool is_upper_alnum(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::string lowercase(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = ascii_lower(c);
  return out;
}

// A path component must be safe to create on Windows, Linux and macOS without
// escaping its parent or aliasing another entry. Returns an empty string if it is,
// or the reason it is not.
std::string check_name(std::string_view name) {
  if (name.empty()) return "an empty name";
  if (name.size() > kGcMaxNameLength) return "a name longer than " + std::to_string(kGcMaxNameLength) + " bytes";
  if (name == "." || name == "..") return "the name '" + std::string(name) + "'";
  for (char c : name) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 0x20 || byte > 0x7E) return "a name with a control or non-ASCII byte";
    if (std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos) {
      return "a name containing '" + std::string(1, c) + "'";
    }
  }
  if (name.back() == '.' || name.back() == ' ') return "a name ending in a dot or space";

  // Windows device names, with or without an extension (Windows ignores trailing
  // spaces before the extension, so "nul .txt" is a device too).
  std::string stem = lowercase(name.substr(0, name.find('.')));
  while (!stem.empty() && stem.back() == ' ') stem.pop_back();
  static constexpr std::array<std::string_view, 6> kDevices = {"con", "prn", "aux", "nul", "conin$", "conout$"};
  bool reserved = std::find(kDevices.begin(), kDevices.end(), stem) != kDevices.end();
  if (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '0' && stem[3] <= '9') {
    reserved = true;
  }
  if (reserved) return "the reserved device name '" + std::string(name) + "'";
  return {};
}

struct Range {
  std::uint64_t begin;
  std::uint64_t end;
  std::string what;
};

}  // namespace

std::string_view gc_region_name(char region_code) {
  switch (region_code) {
    case 'E': return "USA";
    case 'J': return "JPN";
    case 'P': return "EUR";
    case 'K': return "KOR";
    case 'D': return "GER";
    case 'F': return "FRA";
    case 'S': return "ESP";
    case 'I': return "ITA";
    case 'U': return "AUS";
    case 'W': return "TWN";
    default: return "unknown";
  }
}

std::string gc_revision_name(std::uint8_t version) { return "Rev " + std::to_string(version); }

std::string gc_version_tag(const GcDiscHeader& header) {
  char version[16];
  std::snprintf(version, sizeof(version), "%02X", static_cast<unsigned>(header.version));
  std::string tag = header.disc_id + "_" + version;
  if (header.disc_number > 0) tag += "_disc" + std::to_string(header.disc_number + 1);
  return tag;
}

const GcFile* GcDisc::find(std::string_view path) const {
  for (const GcFile& file : files_) {
    if (file.path == path) return &file;
  }
  return nullptr;
}

std::string gc_name_problem(std::string_view name) { return check_name(name); }

std::optional<GcDiscHeader> read_gc_disc_header(const ByteSource& source, std::string& error) {
  const std::uint64_t image_size = source.size();
  auto fail = [&error](std::string message) {
    error = std::move(message);
    return std::nullopt;
  };
  if (image_size < kGcHeaderSize) {
    return fail("The image is too small to be a GameCube disc (" + std::to_string(image_size) +
                " bytes). It is probably truncated.");
  }
  std::vector<std::byte> header(kGcHeaderSize);
  if (!source.read(0, header)) return fail("Could not read the disc header.");

  if (be32(header, kNkitMagicOffset) == 0x4E4B4954) {  // "NKIT"
    return fail("NKit images are not supported: they are altered and do not hash as a retail dump. "
                "Restore the disc to a full, plain .iso and try again.");
  }
  if (be32(header, kWiiMagicOffset) == kWiiMagic) {
    return fail("This is a Wii disc image, not a GameCube one.");
  }
  if (be32(header, kGcMagicOffset) != kGcMagic) {
    return fail("This is not a GameCube disc image (the disc header magic is missing).");
  }

  GcDiscHeader h;
  for (std::size_t i = 0; i < 6; ++i) {
    const char c = static_cast<char>(header[i]);
    if (!is_upper_alnum(c)) return fail("The disc header has an invalid disc ID.");
    h.disc_id.push_back(c);
  }
  h.disc_number = std::to_integer<std::uint8_t>(header[6]);
  h.version = std::to_integer<std::uint8_t>(header[7]);
  for (std::size_t i = 0; i < kGameNameSize; ++i) {
    const auto byte = std::to_integer<std::uint8_t>(header[kGameNameOffset + i]);
    if (byte == 0) break;
    h.game_name.push_back(byte >= 0x20 && byte <= 0x7E ? static_cast<char>(byte) : '?');
  }
  h.dol_offset = be32(header, 0x420);
  h.fst_offset = be32(header, 0x424);
  h.fst_size = be32(header, 0x428);
  h.fst_max_size = be32(header, 0x42C);
  return h;
}

std::optional<GcDisc> GcDisc::open(const ByteSource& source, std::string& error) {
  GcDisc disc;
  disc.image_size_ = source.size();
  const std::uint64_t image_size = disc.image_size_;
  auto fail = [&error](std::string message) {
    error = std::move(message);
    return std::nullopt;
  };
  auto in_image = [image_size](std::uint64_t offset, std::uint64_t size) {
    return offset <= image_size && size <= image_size - offset;
  };

  // --- Header -------------------------------------------------------------
  if (image_size < kGcApploaderOffset + kGcApploaderHeaderSize) {
    return fail("The image is too small to be a GameCube disc (" + std::to_string(image_size) +
                " bytes). It is probably truncated.");
  }
  if (image_size > kGcFullDiscSize) {
    return fail("The image is larger than a GameCube disc (" + std::to_string(image_size) + " bytes, at most " +
                std::to_string(kGcFullDiscSize) + " expected).");
  }
  auto header = read_gc_disc_header(source, error);
  if (!header) return std::nullopt;
  disc.header_ = std::move(*header);
  const GcDiscHeader& h = disc.header_;

  // --- Apploader ----------------------------------------------------------
  std::array<std::byte, kGcApploaderHeaderSize> apploader{};
  if (!source.read(kGcApploaderOffset, apploader)) return fail("Could not read the apploader header.");
  const std::uint64_t apploader_size =
      std::uint64_t{kGcApploaderHeaderSize} + be32(apploader, 0x14) + be32(apploader, 0x18);
  if (apploader_size > kGcMaxApploaderSize || !in_image(kGcApploaderOffset, apploader_size)) {
    return fail("The apploader runs past the end of the image. The dump is probably truncated or damaged.");
  }
  disc.apploader_size_ = static_cast<std::uint32_t>(apploader_size);

  // --- DOL ----------------------------------------------------------------
  if (!in_image(h.dol_offset, kDolHeaderSize)) {
    return fail("The main executable offset " + hex(h.dol_offset) + " is outside the image.");
  }
  std::array<std::byte, kDolHeaderSize> dol{};
  if (!source.read(h.dol_offset, dol)) return fail("Could not read the main executable header.");
  std::uint64_t dol_size = kDolHeaderSize;
  for (int i = 0; i < kDolSections; ++i) {
    const std::uint64_t offset = be32(dol, static_cast<std::size_t>(4 * i));
    const std::uint64_t size = be32(dol, kDolSizesOffset + static_cast<std::size_t>(4 * i));
    if (size == 0) continue;
    if (offset < kDolHeaderSize) return fail("The main executable has a section inside its own header.");
    dol_size = std::max(dol_size, offset + size);
  }
  if (dol_size > kGcMaxDolSize || !in_image(h.dol_offset, dol_size)) {
    return fail("The main executable runs past the end of the image. The dump is probably truncated or damaged.");
  }
  disc.dol_size_ = static_cast<std::uint32_t>(dol_size);

  // --- FST ----------------------------------------------------------------
  if (h.fst_size < kFstEntrySize || h.fst_size > kGcMaxFstSize) {
    return fail("The file system table has an invalid size (" + std::to_string(h.fst_size) + " bytes).");
  }
  if (!in_image(h.fst_offset, h.fst_size)) {
    return fail("The file system table runs past the end of the image. The dump is probably truncated.");
  }
  std::vector<std::byte> fst(h.fst_size);
  if (!source.read(h.fst_offset, fst)) return fail("Could not read the file system table.");

  if (std::to_integer<std::uint8_t>(fst[0]) != 1) return fail("The file system table's root is not a directory.");
  const std::uint32_t count = be32(fst, 8);
  if (count == 0 || count > h.fst_size / kFstEntrySize) {
    return fail("The file system table's entry count (" + std::to_string(count) + ") does not fit the table.");
  }
  const std::size_t strings_begin = std::size_t{count} * kFstEntrySize;
  const std::span<const std::byte> strings = std::span<const std::byte>(fst).subspan(strings_begin);

  std::vector<Range> ranges;
  ranges.push_back({0, kGcApploaderOffset + apploader_size, "the disc header and apploader"});
  ranges.push_back({h.dol_offset, h.dol_offset + dol_size, "the main executable"});
  ranges.push_back({h.fst_offset, std::uint64_t{h.fst_offset} + h.fst_size, "the file system table"});

  struct Frame {
    std::uint32_t index;  // FST index of this directory
    std::uint32_t end;    // one past its last descendant
    std::string prefix;   // its path plus '/', empty for the root
    std::set<std::string> names;  // lowercased child names, to catch case-insensitive clashes
  };
  std::vector<Frame> stack;
  stack.push_back({0, count, {}, {}});

  for (std::uint32_t i = 1; i < count; ++i) {
    while (stack.back().end == i) stack.pop_back();  // the root ends at `count`, so it is never popped here
    Frame& parent = stack.back();
    const std::span<const std::byte> entry = std::span<const std::byte>(fst).subspan(std::size_t{i} * kFstEntrySize,
                                                                                     kFstEntrySize);
    const std::string where = "File system entry " + std::to_string(i);
    const auto type = std::to_integer<std::uint8_t>(entry[0]);
    const std::uint32_t name_offset = be32(entry, 0) & 0x00FFFFFF;
    const std::uint32_t a = be32(entry, 4);
    const std::uint32_t b = be32(entry, 8);

    if (type > 1) return fail(where + " has an unknown type.");
    if (name_offset >= strings.size()) return fail(where + " has a name outside the table.");
    std::string name;
    for (std::size_t p = name_offset;; ++p) {
      if (p >= strings.size()) return fail(where + " has an unterminated name.");
      const char c = static_cast<char>(strings[p]);
      if (c == '\0') break;
      if (name.size() == kGcMaxNameLength) {
        return fail(where + " has a name longer than " + std::to_string(kGcMaxNameLength) + " bytes.");
      }
      name.push_back(c);
    }
    if (const std::string problem = check_name(name); !problem.empty()) {
      return fail(where + " has " + problem + ". The image may be damaged or crafted.");
    }
    std::string path = parent.prefix + name;
    if (path.size() > kGcMaxPathLength) return fail(where + " has a path longer than " +
                                                    std::to_string(kGcMaxPathLength) + " bytes.");
    if (!parent.names.insert(lowercase(name)).second) {
      return fail(where + " ('" + path + "') duplicates another name in its directory.");
    }

    if (type == 1) {
      // Directory: `a` is the parent's index, `b` is one past its last descendant.
      if (a != parent.index) return fail(where + " ('" + path + "') names the wrong parent directory.");
      if (b <= i || b > parent.end) return fail(where + " ('" + path + "') has an invalid extent.");
      disc.dirs_.push_back(path);
      stack.push_back({i, b, path + "/", {}});
    } else {
      // File: `a` is its offset in the image, `b` its size.
      if (!in_image(a, b)) {
        return fail(where + " ('" + path + "') runs past the end of the image. The dump is probably truncated.");
      }
      if (b > 0) ranges.push_back({a, std::uint64_t{a} + b, "'" + path + "'"});
      disc.files_.push_back({std::move(path), a, b});
    }
  }

  std::sort(ranges.begin(), ranges.end(), [](const Range& x, const Range& y) { return x.begin < y.begin; });
  for (std::size_t i = 1; i < ranges.size(); ++i) {
    if (ranges[i].begin < ranges[i - 1].end) {
      return fail("The disc is malformed: " + ranges[i].what + " overlaps " + ranges[i - 1].what + ".");
    }
  }
  return disc;
}

Result extract_gc_disc(const ByteSource& source, const GcDisc& disc, const std::filesystem::path& dest) {
  namespace fs = std::filesystem;
  std::error_code ec;

  if (fs::exists(fs::symlink_status(dest, ec))) {
    if (!fs::is_directory(fs::symlink_status(dest, ec)) || !fs::is_empty(dest, ec) || ec) {
      return Result::failure("The extraction folder " + path_for_message(dest) + " must be empty.");
    }
  }
  if (source.size() != disc.image_size()) {
    return Result::failure("The image changed size after it was read.");
  }

  // Check every output path length before writing anything, counting the full
  // destination path, so a deep cache folder fails early with a clear message.
  const fs::path absolute_dest = fs::absolute(dest, ec);
  if (ec) return Result::failure("Could not resolve " + path_for_message(dest) + ": " + ec.message());
  std::size_t longest = (absolute_dest / "sys" / "apploader.img").native().size();
  for (const std::string& dir : disc.directories()) {
    longest = std::max(longest, (absolute_dest / "files" / fs::path(dir)).native().size());
  }
  for (const GcFile& file : disc.files()) {
    longest = std::max(longest, (absolute_dest / "files" / fs::path(file.path)).native().size());
  }
  if (longest > kGcMaxOutputPathLength) {
    return Result::failure("The extraction folder " + path_for_message(absolute_dest) + " is too deep: a file inside " +
                           "it would need a " + std::to_string(longest) + "-character path, and the limit is " +
                           std::to_string(kGcMaxOutputPathLength) + ". Choose a shorter cache folder.");
  }

  // Remember which folders on the way to `dest` do not exist yet, so a failed
  // extraction removes exactly what it created and nothing else.
  std::vector<fs::path> created_parents;  // deepest first
  for (fs::path p = absolute_dest.parent_path(); !p.empty() && !fs::exists(p, ec); p = p.parent_path()) {
    created_parents.push_back(p);
    if (p == p.parent_path()) break;
  }
  const bool created = fs::create_directories(absolute_dest, ec);
  if (ec) return Result::failure("Could not create " + path_for_message(dest) + ": " + ec.message());

  auto cleanup_and_fail = [&](std::string message) {
    std::error_code ignored;
    if (created) {
      fs::remove_all(absolute_dest, ignored);
      for (const fs::path& parent : created_parents) fs::remove(parent, ignored);  // only removes empty folders
    } else {
      for (const auto& child : fs::directory_iterator(absolute_dest, ignored)) fs::remove_all(child.path(), ignored);
    }
    return Result::failure(std::move(message));
  };

  std::vector<std::byte> buffer;
  auto copy = [&](std::uint64_t offset, std::uint64_t size, const fs::path& out_path) -> Result {
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) return Result::failure("Could not create " + path_for_message(out_path));
    buffer.resize(static_cast<std::size_t>(std::min<std::uint64_t>(size, kCopyChunk)));
    while (size > 0) {
      const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(size, kCopyChunk));
      const std::span<std::byte> chunk = std::span(buffer).first(n);
      if (!source.read(offset, chunk)) return Result::failure("Could not read the disc image.");
      out.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(n));
      if (!out) return Result::failure("Could not write " + path_for_message(out_path));
      offset += n;
      size -= n;
    }
    out.close();
    if (!out) return Result::failure("Could not write " + path_for_message(out_path));
    return Result::success();
  };

  const fs::path sys = absolute_dest / "sys";
  const fs::path files = absolute_dest / "files";
  fs::create_directories(sys, ec);
  if (!ec) fs::create_directories(files, ec);
  if (ec) return cleanup_and_fail("Could not create folders in " + path_for_message(dest) + ": " + ec.message());

  const GcDiscHeader& h = disc.header();
  struct SystemFile {
    const char* name;
    std::uint64_t offset;
    std::uint64_t size;
  };
  const SystemFile system_files[] = {
      {"boot.bin", 0, kGcHeaderSize},
      {"bi2.bin", kGcBi2Offset, kGcBi2Size},
      {"apploader.img", kGcApploaderOffset, disc.apploader_size()},
      {"main.dol", h.dol_offset, disc.dol_size()},
      {"fst.bin", h.fst_offset, h.fst_size},
  };
  for (const SystemFile& file : system_files) {
    if (Result r = copy(file.offset, file.size, sys / file.name); !r) return cleanup_and_fail(r.error());
  }

  // FST paths were validated by GcDisc::open (no separators, no "." or "..", no
  // device names), so each one maps to a path strictly inside files/. Check anyway.
  const fs::path root = files.lexically_normal();
  auto inside_root = [&root](const fs::path& p) {
    const fs::path rel = p.lexically_normal().lexically_relative(root);
    return !rel.empty() && *rel.begin() != ".." && !rel.is_absolute();
  };
  for (const std::string& dir : disc.directories()) {
    const fs::path out = files / fs::path(dir);
    if (!inside_root(out)) return cleanup_and_fail("Refusing to extract outside the folder: " + dir);
    fs::create_directories(out, ec);
    if (ec) return cleanup_and_fail("Could not create " + path_for_message(out) + ": " + ec.message());
  }
  for (const GcFile& file : disc.files()) {
    const fs::path out = files / fs::path(file.path);
    if (!inside_root(out)) return cleanup_and_fail("Refusing to extract outside the folder: " + file.path);
    if (Result r = copy(file.offset, file.size, out); !r) return cleanup_and_fail(r.error());
  }
  return Result::success();
}

}  // namespace mpt::asset_import
