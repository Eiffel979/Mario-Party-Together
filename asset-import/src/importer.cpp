#include "mpt/asset_import/importer.h"

#include <fstream>
#include <vector>

#include "mpt/asset_import/sha256.h"

namespace mpt::asset_import {
namespace {

constexpr std::uintmax_t kMaxN64Size = 64ull * 1024 * 1024;  // largest N64 cartridge
constexpr std::size_t kChunkSize = 1 << 20;

}  // namespace

IdentifyReport Importer::identify(const std::filesystem::path& path) const {
  IdentifyReport report;
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  std::ifstream file(path, std::ios::binary);
  if (ec || !file) {
    report.message = "Could not read " + path.string();
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
                     " images are not supported yet. Convert the dump to a plain .iso and try again.";
    return report;
  }

  file.clear();
  file.seekg(0);
  Sha256 hasher;
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
  report.sha256 = hasher.finish_hex();

  if (const KnownDump* match = known_.find_by_sha256(report.sha256); match && match->size == size) {
    report.match = *match;
  } else {
    report.message = "This dump is not in the known-good list. It may be a bad dump, a hack, or an unsupported revision.";
  }
  return report;
}

VerifiedDump Importer::make_verified(const KnownDump& dump, const std::filesystem::path& cache_root) {
  return VerifiedDump{dump.title, dump.region, dump.revision, dump.sha256,
                      cache_root / std::string(to_string(dump.title)) / dump.sha256};
}

}  // namespace mpt::asset_import
