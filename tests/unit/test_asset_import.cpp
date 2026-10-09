#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "mpt/asset_import/dump_format.h"
#include "mpt/asset_import/importer.h"
#include "mpt/asset_import/known_dumps.h"
#include "mpt/asset_import/sha256.h"

using namespace mpt;
using namespace mpt::asset_import;

// All dumps in these tests are synthetic: a format magic followed by generated
// bytes. Never add real game data here (CLAUDE.md rule 1).

namespace {

std::span<const std::byte> as_bytes(std::string_view s) { return std::as_bytes(std::span(s.data(), s.size())); }

std::string sha256_of(std::span<const std::byte> data) {
  Sha256 h;
  h.update(data);
  return h.finish_hex();
}

// A fake 1 MiB "cartridge" in z64 order: the N64 magic word, then a byte pattern.
std::vector<std::byte> fake_z64() {
  std::vector<std::byte> rom(1 << 20);
  for (std::size_t i = 0; i < rom.size(); ++i) rom[i] = static_cast<std::byte>((i * 7 + 3) & 0xff);
  rom[0] = std::byte{0x80};
  rom[1] = std::byte{0x37};
  rom[2] = std::byte{0x12};
  rom[3] = std::byte{0x40};
  return rom;
}

std::vector<std::byte> to_v64(std::vector<std::byte> rom) {
  for (std::size_t i = 0; i < rom.size(); i += 2) std::swap(rom[i], rom[i + 1]);
  return rom;
}

std::vector<std::byte> to_n64(std::vector<std::byte> rom) {
  for (std::size_t i = 0; i < rom.size(); i += 4) std::reverse(rom.begin() + i, rom.begin() + i + 4);
  return rom;
}

std::vector<std::byte> with_magic_at(std::size_t offset, std::initializer_list<std::uint8_t> magic) {
  std::vector<std::byte> header(kFormatProbeSize);
  for (std::uint8_t b : magic) header[offset++] = std::byte{b};
  return header;
}

class TempDir {
 public:
  TempDir() {
    path_ = std::filesystem::temp_directory_path() /
            ("mpt_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  const std::filesystem::path& path() const { return path_; }

  std::filesystem::path write(const std::string& name, std::span<const std::byte> data) const {
    const auto file = path_ / name;
    std::ofstream(file, std::ios::binary)
        .write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return file;
  }

 private:
  std::filesystem::path path_;
};

KnownDumps known_with(const std::string& sha256, std::uint64_t size) {
  std::string error;
  const std::string json = R"({"version":1,"dumps":[{"title":"mp1","region":"USA","revision":"1.0","sha256":")" +
                           sha256 + R"(","size":)" + std::to_string(size) + "}]}";
  auto known = KnownDumps::parse(json, error);
  REQUIRE_MESSAGE(known.has_value(), error);
  return *known;
}

}  // namespace

TEST_CASE("SHA-256 matches FIPS 180-4 test vectors") {
  CHECK(sha256_of(as_bytes("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256_of(as_bytes("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(sha256_of(as_bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

  const std::string million_a(1'000'000, 'a');
  CHECK(sha256_of(as_bytes(million_a)) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

  // Feeding the same data in uneven pieces gives the same digest.
  Sha256 pieces;
  for (std::size_t pos = 0, step = 1; pos < million_a.size(); pos += step, step = step * 3 % 211 + 1) {
    pieces.update(as_bytes(std::string_view(million_a).substr(pos, step)));
  }
  CHECK(pieces.finish_hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("formats are detected from their header magic") {
  const auto z64 = fake_z64();
  CHECK(detect_format(z64) == DumpFormat::n64_z64);
  CHECK(detect_format(to_v64(z64)) == DumpFormat::n64_v64);
  CHECK(detect_format(to_n64(z64)) == DumpFormat::n64_n64);

  CHECK(detect_format(with_magic_at(0x1C, {0xC2, 0x33, 0x9F, 0x3D})) == DumpFormat::gc_disc);
  CHECK(detect_format(with_magic_at(0x18, {0x5D, 0x1C, 0x9E, 0xA3})) == DumpFormat::wii_disc);
  CHECK(detect_format(with_magic_at(0, {'R', 'V', 'Z', 0x01})) == DumpFormat::rvz);
  CHECK(detect_format(with_magic_at(0, {'W', 'B', 'F', 'S'})) == DumpFormat::wbfs);
  CHECK(detect_format(with_magic_at(0, {0x01, 0xC0, 0x0B, 0xB1})) == DumpFormat::gcz);
  CHECK(detect_format(as_bytes("hello world")) == DumpFormat::unknown);
  CHECK(detect_format({}) == DumpFormat::unknown);
}

TEST_CASE("N64 byte orders normalize to z64") {
  const auto z64 = fake_z64();
  auto v64 = to_v64(z64);
  auto n64 = to_n64(z64);
  REQUIRE(normalize_n64(v64, DumpFormat::n64_v64));
  REQUIRE(normalize_n64(n64, DumpFormat::n64_n64));
  CHECK(v64 == z64);
  CHECK(n64 == z64);

  std::vector<std::byte> odd(6);
  CHECK_FALSE(normalize_n64(odd, DumpFormat::n64_v64));
  CHECK_FALSE(normalize_n64(v64, DumpFormat::gc_disc));
}

TEST_CASE("known_dumps.json is validated") {
  const std::string empty_sha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  std::string error;
  CHECK(KnownDumps::parse(R"({"version":1,"dumps":[]})", error).has_value());

  CHECK_FALSE(KnownDumps::parse("not json", error).has_value());
  CHECK_FALSE(KnownDumps::parse(R"({"version":2,"dumps":[]})", error).has_value());
  CHECK_FALSE(KnownDumps::parse(R"({"version":1})", error).has_value());
  CHECK_FALSE(
      KnownDumps::parse(R"({"version":1,"dumps":[{"title":"mp99","sha256":")" + empty_sha + R"(","size":1}]})", error)
          .has_value());
  CHECK_FALSE(
      KnownDumps::parse(R"({"version":1,"dumps":[{"title":"mp1","sha256":")" + empty_sha + R"(","size":0}]})", error)
          .has_value());

  const std::string entry = R"({"title":"mp1","sha256":")" + empty_sha + R"(","size":1})";
  CHECK_FALSE(KnownDumps::parse(R"({"version":1,"dumps":[)" + entry + "," + entry + "]}", error).has_value());
  CHECK(error.find("duplicates") != std::string::npos);

  CHECK_FALSE(KnownDumps::parse(R"({"version":1,"dumps":[{"title":"mp1","sha256":"ABC","size":1}]})", error)
                  .has_value());
  CHECK(error.find("sha256") != std::string::npos);
}

TEST_CASE("the shipped known_dumps.json is valid") {
  std::string error;
  CHECK_MESSAGE(KnownDumps::load(MPT_KNOWN_DUMPS_JSON, error).has_value(), error);
}

TEST_CASE("importer identifies a synthetic N64 dump in any byte order") {
  const auto z64 = fake_z64();
  const std::string expected_sha = sha256_of(z64);
  const Importer importer(known_with(expected_sha, z64.size()));
  const TempDir dir;

  const std::pair<const char*, std::vector<std::byte>> cases[] = {
      {"fake.z64", z64}, {"fake.v64", to_v64(z64)}, {"fake.n64", to_n64(z64)}};
  for (const auto& [name, data] : cases) {
    CAPTURE(std::string(name));
    const IdentifyReport report = importer.identify(dir.write(name, data));
    CHECK(is_n64(report.format));
    CHECK(report.sha256 == expected_sha);
    REQUIRE(report.match.has_value());
    CHECK(report.match->title == TitleId::mp1);
    CHECK(report.match->region == "USA");
  }
}

TEST_CASE("importer rejects unknown, unsupported and unreadable files") {
  const Importer importer(known_with(std::string(64, '0'), 1));
  const TempDir dir;

  const IdentifyReport not_in_db = importer.identify(dir.write("other.z64", fake_z64()));
  CHECK(not_in_db.format == DumpFormat::n64_z64);
  CHECK_FALSE(not_in_db.match.has_value());
  CHECK_FALSE(not_in_db.message.empty());

  const IdentifyReport garbage = importer.identify(dir.write("notes.txt", as_bytes("just some text")));
  CHECK(garbage.format == DumpFormat::unknown);
  CHECK(garbage.sha256.empty());

  const IdentifyReport rvz = importer.identify(dir.write("x.rvz", with_magic_at(0, {'R', 'V', 'Z', 0x01})));
  CHECK(rvz.format == DumpFormat::rvz);
  CHECK_FALSE(rvz.match.has_value());
  CHECK(rvz.sha256.empty());

  const IdentifyReport missing = importer.identify(dir.path() / "does_not_exist.z64");
  CHECK_FALSE(missing.match.has_value());
  CHECK_FALSE(missing.message.empty());
}
