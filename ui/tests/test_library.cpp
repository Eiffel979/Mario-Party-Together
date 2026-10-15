#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "mpt/ui/model/library.h"

using namespace mpt;
using namespace mpt::ui;

namespace {

VerifiedDump dump(TitleId title, const char* region, char fill) {
  VerifiedDump d;
  d.title = title;
  d.region = region;
  d.revision = "Rev 0";
  d.sha256 = std::string(64, fill);
  d.build_dir = "build";
  return d;
}

std::filesystem::path scratch(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / "mpt_ui_model_tests" / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

}  // namespace

TEST_CASE("the library round-trips paths and hashes, never the VerifiedDump") {
  Library library;
  library.add(dump(TitleId::mp4, "USA", 'a'), std::filesystem::path(u8"D:/Spiele/Disc \u00e9.iso"));
  library.add(dump(TitleId::mp1, "USA", 'b'), "D:/n64/one.z64");
  REQUIRE(library.entries().size() == 2);
  CHECK(library.entries()[0].title == TitleId::mp1);  // sorted by title
  CHECK(library.entries()[1].verified.has_value());
  CHECK(library.entries()[1].display_name() == "Mario Party 4");
  CHECK(library.entries()[1].detail() == "USA, Rev 0");

  std::string error;
  const std::optional<Library> back = Library::parse(library.to_json(), error);
  REQUIRE(back);
  REQUIRE(back->entries().size() == 2);
  CHECK(back->entries()[1].dump_path == std::filesystem::path(u8"D:/Spiele/Disc \u00e9.iso"));
  CHECK(back->entries()[1].sha256 == std::string(64, 'a'));
  CHECK_FALSE(back->entries()[1].verified);  // re-checked through asset-import after a restart
}

TEST_CASE("adding the same title and region replaces the entry") {
  Library library;
  library.add(dump(TitleId::mp4, "USA", 'a'), "old.iso");
  library.add(dump(TitleId::mp4, "USA", 'c'), "new.iso");
  REQUIRE(library.entries().size() == 1);
  CHECK(library.entries()[0].dump_path == "new.iso");
}

TEST_CASE("save and load, and a damaged file starts empty with a warning") {
  const auto dir = scratch("library");
  Library library;
  library.add(dump(TitleId::mp4, "USA", 'a'), "x.iso");
  std::string error;
  REQUIRE(library.save(dir / "library.json", error));
  std::string warning;
  CHECK(Library::load(dir / "library.json", warning).entries().size() == 1);
  CHECK(warning.empty());
  CHECK(Library::load(dir / "missing.json", warning).entries().empty());
  CHECK(warning.empty());

  std::ofstream(dir / "bad.json") << "{\"format\":\"mpt.library\",\"version\":1,\"titles\":[{\"title\":\"zz\"}]}";
  CHECK(Library::load(dir / "bad.json", warning).entries().empty());
  CHECK_FALSE(warning.empty());
}

TEST_CASE("wrong-typed fields are a damaged library, never an exception") {
  const std::string sha = "\"" + std::string(64, 'a') + "\"";
  const std::string good_entry = "{\"title\":\"mp4\",\"region\":\"USA\",\"revision\":\"Rev 0\",\"sha256\":" + sha +
                                 ",\"path\":\"x.iso\"}";
  const char* const bad_docs[] = {
      "{\"format\":1,\"version\":1,\"titles\":[]}",
      "{\"format\":\"mpt.library\",\"version\":\"1\",\"titles\":[]}",
      "{\"format\":\"mpt.library\",\"version\":1.5,\"titles\":[]}",
      "{\"format\":\"mpt.library\",\"version\":1,\"titles\":{}}",
      "{\"format\":\"mpt.library\",\"version\":1,\"titles\":[1]}",
      "[]",
  };
  const char* const bad_fields[] = {
      "{\"title\":1,\"region\":\"USA\",\"sha256\":SHA,\"path\":\"x.iso\"}",
      "{\"title\":\"mp4\",\"region\":[],\"sha256\":SHA,\"path\":\"x.iso\"}",
      "{\"title\":\"mp4\",\"region\":\"USA\",\"revision\":{},\"sha256\":SHA,\"path\":\"x.iso\"}",
      "{\"title\":\"mp4\",\"region\":\"USA\",\"sha256\":12,\"path\":\"x.iso\"}",
      "{\"title\":\"mp4\",\"region\":\"USA\",\"sha256\":SHA,\"path\":true}",
      "{\"title\":null,\"region\":\"USA\",\"sha256\":SHA,\"path\":\"x.iso\"}",
  };
  std::vector<std::string> cases(std::begin(bad_docs), std::end(bad_docs));
  for (const char* field : bad_fields) {
    std::string entry = field;
    if (const auto at = entry.find("SHA"); at != std::string::npos) entry.replace(at, 3, sha);
    cases.push_back("{\"format\":\"mpt.library\",\"version\":1,\"titles\":[" + good_entry + "," + entry + "]}");
  }
  const auto dir = scratch("library_types");
  for (const std::string& text : cases) {
    CAPTURE(text);
    std::string error;
    std::optional<Library> parsed;
    CHECK_NOTHROW(parsed = Library::parse(text, error));
    CHECK_FALSE(parsed);
    CHECK_FALSE(error.empty());
    std::ofstream(dir / "lib.json", std::ios::trunc) << text;
    std::string warning;
    CHECK(Library::load(dir / "lib.json", warning).entries().empty());
    CHECK_FALSE(warning.empty());
  }
}
