#include "mpt/asset_import/importer.h"

#include <fstream>
#include <memory>
#include <span>
#include <vector>

#include "mpt/asset_import/byte_source.h"
#include "mpt/asset_import/gc_disc.h"
#include "mpt/asset_import/sha1.h"
#include "mpt/core/sha256.h"

namespace mpt::asset_import {
namespace {

constexpr std::uintmax_t kMaxN64Size = 64ull * 1024 * 1024;  // largest N64 cartridge
constexpr std::size_t kChunkSize = 1 << 20;

// SHA-256 (the identity) and SHA-1 (to cross-check public dump databases, D8),
// fed from the same read.
struct Hashers {
  Sha256 sha256;
  Sha1 sha1;
  void update(std::span<const std::byte> data) {
    sha256.update(data);
    sha1.update(data);
  }
};

// "Mario Party 4 (USA, Rev 1, GMPE01_01)"
std::string describe(const KnownDump& dump) {
  std::string text = std::string(title_display_name(dump.title)) + " (" + dump.region + ", " + dump.revision;
  if (!dump.game_version.empty()) text += ", " + dump.game_version;
  return text + ")";
}

}  // namespace

IdentifyReport Importer::identify(const std::filesystem::path& path) const {
  IdentifyReport report;
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  std::ifstream file(path, std::ios::binary);
  if (ec || !file) {
    report.message = "Could not read " + path_for_message(path);
    return report;
  }

  std::vector<std::byte> probe(kFormatProbeSize);
  file.read(reinterpret_cast<char*>(probe.data()), static_cast<std::streamsize>(probe.size()));
  probe.resize(static_cast<std::size_t>(file.gcount()));
  report.format = detect_format(probe);

  if (report.format == DumpFormat::unknown) {
    report.message = "Not a recognized N64, GameCube or Wii dump.";
    return report;
  }
  if (is_compressed_container(report.format)) {
    report.message = std::string(to_string(report.format)) +
                     " images are not supported yet: only plain, full-size .iso disc images can be imported. "
                     "Convert the dump to a plain .iso (for example with Dolphin's Convert File, format ISO) "
                     "and try again.";
    return report;
  }

  file.clear();
  file.seekg(0);
  Hashers hasher;
  if (is_n64(report.format)) {
    if (size > kMaxN64Size || size % 4 != 0) {
      report.message = "File size does not match an N64 cartridge image.";
      return report;
    }
    std::vector<std::byte> rom(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(rom.data()), static_cast<std::streamsize>(rom.size()));
    if (static_cast<std::uintmax_t>(file.gcount()) != size) {
      report.message = "Could not read the whole file.";
      return report;
    }
    normalize_n64(rom, report.format);
    hasher.update(rom);
  } else {
    std::vector<std::byte> chunk(kChunkSize);
    while (file) {
      file.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
      hasher.update(std::span(chunk).first(static_cast<std::size_t>(file.gcount())));
    }
  }
  report.sha256 = to_hex(hasher.sha256.finish());
  report.sha1 = hasher.sha1.finish_hex();

  // The hash is the identity: a known-good dump is accepted on its hash alone, even
  // if a strict structural rule would object (extraction enforces those rules on
  // what it writes). For a GameCube image the header names the disc either way, and
  // on a mismatch the full structural check explains what is wrong with it.
  std::unique_ptr<FileSource> gc_source;
  std::string gc_error;
  if (report.format == DumpFormat::gc_disc) {
    gc_source = FileSource::open(path, gc_error);
    if (gc_source) {
      if (auto header = read_gc_disc_header(*gc_source, gc_error)) report.gc_disc = std::move(*header);
    }
  }

  if (const KnownDump* match = known_.find_supported(report.sha256, report.sha1, size)) {
    report.match = *match;
    return report;
  }

  // Known, but not a version we support: name it (E11) instead of calling it unknown.
  if (const KnownDump* known = known_.find_by_sha1(report.sha1, size); known && !known->supported) {
    report.unsupported = *known;
    report.message = "This is " + describe(*known) + ". " + known->reason;
    return report;
  }
  if (report.gc_disc) {
    const auto candidates = known_.find_by_disc(report.gc_disc->disc_id, report.gc_disc->version);
    for (const KnownDump* known : candidates) {
      if (known->supported) continue;
      report.unsupported = *known;
      report.message = "This is " + describe(*known) + ". " + known->reason;
      if (size != kGcFullDiscSize) report.message += " (The image also looks truncated or trimmed.)";
      return report;
    }
    for (const KnownDump* known : candidates) {
      // The header says this is a version we support, but the hashes differ.
      report.message = "This disc says it is " + describe(*known) +
                       ", but it does not match the verified dump of that version.";
      std::string damage;
      if (!GcDisc::open(*gc_source, damage)) {
        report.message += " It is damaged or altered: " + damage;
      } else if (size != kGcFullDiscSize) {
        report.message += " The image is " + std::to_string(size) + " bytes but a full disc is " +
                          std::to_string(kGcFullDiscSize) + " bytes, so it is probably truncated or trimmed.";
      } else {
        report.message += " It may be a bad dump or a modified copy. Dump the disc again and try again.";
      }
      return report;
    }
  }

  if (report.format == DumpFormat::gc_disc && !report.gc_disc) {
    report.message = gc_error;
  } else if (std::string error; report.gc_disc && !GcDisc::open(*gc_source, error)) {
    report.message = "This GameCube disc (" + report.gc_disc->disc_id + ") is not in the known-good list, and " +
                     "it is damaged or altered: " + error;
  } else if (report.gc_disc) {
    const GcDiscHeader& h = *report.gc_disc;
    report.message = "This GameCube disc (" + h.disc_id + ", " + std::string(gc_region_name(h.region_code())) + ", " +
                     gc_revision_name(h.version) + ") is not in the known-good list.";
    if (size != kGcFullDiscSize) {
      report.message += " The image is " + std::to_string(size) + " bytes but a full disc is " +
                        std::to_string(kGcFullDiscSize) + " bytes, so it is probably truncated or trimmed.";
    } else {
      report.message += " It may be a bad dump, a hack, or an unsupported revision or region.";
    }
  } else {
    report.message = "This dump is not in the known-good list. It may be a bad dump, a hack, or an unsupported revision.";
  }
  return report;
}

}  // namespace mpt::asset_import
