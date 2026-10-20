#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "../support/temp_dir.h"
#include "mpt/asset_import/dump_format.h"
#include "mpt/asset_import/importer.h"
#include "mpt/asset_import/known_dumps.h"
#include "mpt/asset_import/sha1.h"
#include "mpt/core/sha256.h"

using namespace mpt;
using namespace mpt::asset_import;

// All dumps in these tests are synthetic: a format magic followed by generated
// bytes. Never add real game data here (CLAUDE.md rule 1).

namespace {

std::span<const std::byte> as_bytes(std::string_view s) { return std::as_bytes(std::span(s.data(), s.size())); }

std::string sha256_of(std::span<const std::byte> data) {
  Sha256 h;
  h.update(data);
  return to_hex(h.finish());
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
    // Per process and per object: the address alone repeats across processes.
    path_ = mpt::test::make_temp_dir("mpt_test");
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

std::string sha1_of(std::span<const std::byte> data) {
  Sha1 h;
  h.update(data);
  return h.finish_hex();
}

KnownDumps known_with(const std::string& sha256, const std::string& sha1, std::uint64_t size) {
  std::string error;
  const std::string json =
      R"({"version":2,"dumps":[{"title":"mp1","region":"USA","revision":"1.0","supported":true,"sha256":")" + sha256 +
      R"(","sha1":")" + sha1 + R"(","size":)" + std::to_string(size) + "}]}";
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
  CHECK(to_hex(pieces.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
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

TEST_CASE("SHA-1 is computed alongside SHA-256 and matches FIPS 180-4") {
  CHECK(sha1_of(as_bytes("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST_CASE("known_dumps.json schema v2 is validated") {
  const std::string sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  const std::string sha1 = "da39a3ee5e6b4b0d3255bfef95601890afd80709";
  const std::string supported = R"({"title":"mp1","region":"USA","revision":"1.0","supported":true,"sha256":")" +
                                sha256 + R"(","sha1":")" + sha1 + R"(","size":1)";
  auto doc = [](const std::string& entries) { return R"({"version":2,"dumps":[)" + entries + "]}"; };
  auto rejected = [](const std::string& json, std::string_view needle) {
    std::string error;
    CHECK_FALSE(KnownDumps::parse(json, error).has_value());
    CHECK_MESSAGE(error.find(needle) != std::string::npos, "error was: " << error);
  };

  std::string error;
  CHECK(KnownDumps::parse(doc(""), error).has_value());
  CHECK(KnownDumps::parse(doc(supported + "}"), error).has_value());

  rejected("not json", "not a JSON object");
  rejected(R"({"version":1,"dumps":[]})", "unsupported version");
  rejected(R"({"version":2})", "\"dumps\"");
  rejected(R"({"version":2,"dumps":[],"extra":1})", "unknown field 'extra'");
  rejected(doc(supported + R"(,"crc":"x"})"), "unknown field 'crc'");
  rejected(doc(R"({"title":"mp99","region":"USA","revision":"1.0","supported":false,"disc_id":"GABE01","reason":"r"})"),
           "title");
  rejected(doc(R"({"title":"mp1","region":"USA","revision":"1.0","sha256":")" + sha256 + R"("})"), "supported");
  rejected(doc(R"({"title":"mp1","region":"USA","revision":"1.0","supported":true,"sha256":")" + sha256 +
               R"(","size":1})"),
           "needs sha256, sha1 and size");
  rejected(doc(R"({"title":"mp1","region":"USA","revision":"1.0","supported":true,"sha256":")" + sha256 +
               R"(","sha1":")" + sha1 + R"(","size":0})"),
           "size");
  rejected(doc(supported + "}," + supported + "}"), "duplicates");
  rejected(doc(R"({"title":"mp1","region":"USA","revision":"1.0","supported":true,"sha256":"ABC","sha1":")" + sha1 +
               R"(","size":1})"),
           "sha256");
  rejected(doc(supported + R"(,"md5":"xyz"})"), "md5");
  rejected(doc(supported + R"(,"reason":"r"})"), "must not have a reason");
  rejected(doc(R"({"title":"mp4","region":"EUR","revision":"any","supported":false,"disc_id":"GABP01"})"),
           "needs a reason");
  rejected(doc(R"({"title":"mp4","region":"EUR","revision":"any","supported":false,"reason":"r"})"),
           "sha1 or a disc_id");
  rejected(doc(R"({"title":"mp4","region":"EUR","revision":"any","supported":false,"disc_id":"gabp01","reason":"r"})"),
           "disc_id");
  rejected(doc(R"({"title":"mp4","region":"USA","revision":"x","supported":false,"disc_id":"GABE01","disc_version":256,)"
               R"("reason":"r"})"),
           "disc_version");
  rejected(doc(R"({"title":"mp4","region":"USA","revision":"x","supported":false,"sha1":")" + sha1 +
               R"(","disc_version":1,"reason":"r"})"),
           "needs a disc_id");
}

TEST_CASE("the shipped known_dumps.json is valid and supports exactly GMPE01_00") {
  std::string error;
  const auto known = KnownDumps::load(MPT_KNOWN_DUMPS_JSON, error);
  REQUIRE_MESSAGE(known.has_value(), error);
  std::vector<const KnownDump*> supported;
  for (const KnownDump& dump : known->entries()) {
    if (dump.supported) supported.push_back(&dump);
  }
  REQUIRE(supported.size() == 1);
  const KnownDump& mp4 = *supported[0];
  CHECK(mp4.title == TitleId::mp4);
  CHECK(mp4.game_version == "GMPE01_00");
  CHECK(mp4.disc_id == "GMPE01");
  CHECK(mp4.disc_version == std::uint8_t{0});
  CHECK(mp4.size == 1'459'978'240u);  // a full GameCube disc
  CHECK(mp4.sha256.size() == 64);
  CHECK(mp4.sha1.size() == 40);

  // E11: the other MP4 revisions and regions are named, not "unknown".
  CHECK_FALSE(known->find_by_disc("GMPE01", 1).empty());
  CHECK_FALSE(known->find_by_disc("GMPP01", 0).empty());
  CHECK_FALSE(known->find_by_disc("GMPJ01", 0).empty());
  CHECK_FALSE(known->find_by_disc("GMPE01", 1)[0]->supported);
}

TEST_CASE("importer identifies a synthetic N64 dump in any byte order") {
  const auto z64 = fake_z64();
  const std::string expected_sha = sha256_of(z64);
  const std::string expected_sha1 = sha1_of(z64);
  const Importer importer(known_with(expected_sha, expected_sha1, z64.size()));
  const TempDir dir;

  const std::pair<const char*, std::vector<std::byte>> cases[] = {
      {"fake.z64", z64}, {"fake.v64", to_v64(z64)}, {"fake.n64", to_n64(z64)}};
  for (const auto& [name, data] : cases) {
    CAPTURE(std::string(name));
    const IdentifyReport report = importer.identify(dir.write(name, data));
    CHECK(is_n64(report.format));
    CHECK(report.sha256 == expected_sha);
    CHECK(report.sha1 == expected_sha1);  // of the normalized image, from the same read
    REQUIRE(report.match.has_value());
    CHECK(report.match->title == TitleId::mp1);
    CHECK(report.match->region == "USA");
  }
}

TEST_CASE("importer rejects unknown, unsupported and unreadable files") {
  const Importer importer(known_with(std::string(64, '0'), std::string(40, '0'), 1));
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
