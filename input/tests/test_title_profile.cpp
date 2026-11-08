// Loading a title's installed profile into the Mapper (title_profile.h), on
// synthetic profile directories created under the build tree. No game data.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ostream>  // doctest prints std::string_view through operator<<
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "mpt/input/mapper.h"
#include "mpt/input/names.h"
#include "mpt/input/title_profile.h"

using namespace mpt;
using namespace mpt::input;
namespace fs = std::filesystem;

namespace {

// A fresh, empty directory per test case.
fs::path scratch(std::string_view name) {
  const fs::path dir = fs::path(MPT_INPUT_TEST_SCRATCH_DIR) / fs::path(name);
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  return dir;
}

void write_file(const fs::path& path, std::string_view text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  REQUIRE(out.good());
  out << text;
}

// A small valid mp4 profile: A on Z only, and an override that moves A to X.
std::string synthetic_profile(std::string_view title = "mp4", std::string_view minigame = "mp4.minigame.synthetic_one") {
  std::string text = R"({
  "format": "mpt.input_profile",
  "version": 1,
  "name": "Synthetic",
)";
  if (!title.empty()) text += "  \"title\": \"" + std::string(title) + "\",\n";
  text += R"(  "bindings": { "a": ["key:z"], "b": ["key:j"] },
  "minigames": {
    ")" + std::string(minigame) + R"(": { "note": "test", "bindings": { "a": ["key:x"] } }
  }
}
)";
  return text;
}

DeviceSnapshot keys(std::initializer_list<std::string_view> held) {
  DeviceSnapshot s;
  s.id = kKeyboardDevice;
  for (std::string_view name : held) {
    const auto key = key_from_name(name);
    REQUIRE(key.has_value());
    s.keyboard.set(*key);
  }
  return s;
}

PortLayout keyboard_on_port0() {
  PortLayout layout;
  layout.seated[0] = true;
  layout.device_port[kKeyboardDevice] = 0;
  return layout;
}

bool a_pressed(const Mapper& mapper, std::initializer_list<std::string_view> held) {
  const std::vector<DeviceSnapshot> devices{keys(held)};
  return (mapper.map(0, devices, keyboard_on_port0()).pads[0].buttons & kButtonA) != 0;
}

struct Collect {
  std::vector<std::string> lines;
  WarningSink sink() {
    return [this](std::string_view m) { lines.emplace_back(m); };
  }
};

}  // namespace

TEST_CASE("title profile path is <dir>/<title>/default.json") {
  CHECK(title_profile_path(fs::path("base"), TitleId::mp4) == fs::path("base") / "mp4" / "default.json");
  CHECK(title_profile_path(fs::path("base"), TitleId::smp) == fs::path("base") / "smp" / "default.json");
}

TEST_CASE("an installed profile is found and loaded on every port") {
  const fs::path dir = scratch("found");
  write_file(dir / "mp4" / "default.json", synthetic_profile());
  const std::vector<fs::path> dirs{dir};

  Mapper mapper;
  Collect warnings;
  const TitleProfileLoad load = load_title_profile(mapper, TitleId::mp4, dirs, warnings.sink());
  CHECK(load.from_file());
  CHECK(load.source == dir / "mp4" / "default.json");
  CHECK(load.warnings.empty());
  CHECK(warnings.lines.empty());
  CHECK(load.profile.name == "Synthetic");
  for (std::size_t port = 0; port < kMaxPlayers; ++port) CHECK(*mapper.profile(port) == load.profile);
  CHECK(a_pressed(mapper, {"z"}));
  CHECK_FALSE(a_pressed(mapper, {"k"}));  // the built-in default's A key is gone
}

TEST_CASE("a missing profile falls back to the built-in default with a warning") {
  const fs::path dir = scratch("missing");
  const std::vector<fs::path> dirs{dir, dir / "does_not_exist"};

  Mapper mapper;
  mapper.set_profile_all(Profile{});  // something else, to see the fallback is installed
  Collect warnings;
  const TitleProfileLoad load = load_title_profile(mapper, TitleId::mp4, dirs, warnings.sink());
  CHECK_FALSE(load.from_file());
  CHECK(load.profile == builtin_default_profile());
  CHECK(*mapper.profile(0) == builtin_default_profile());
  REQUIRE(warnings.lines.size() == 1);
  CHECK(warnings.lines == load.warnings);
  CHECK(warnings.lines[0].find("built-in default") != std::string::npos);
  CHECK(a_pressed(mapper, {"k"}));

  // No search directories at all is the same fallback.
  const TitleProfileLoad none = find_title_profile(TitleId::mp4, {});
  CHECK(none.profile == builtin_default_profile());
  CHECK(none.warnings.size() == 1);
}

TEST_CASE("an invalid profile falls back with a warning naming the file") {
  const char* const kCases[] = {
      "{ not json",
      R"({"format": "mpt.input_profile", "version": 1, "name": "x"})",  // no bindings
      R"({"format": "mpt.input_profile", "version": 1, "name": "x", "bindings": {"a": ["key:nope"]}})",
  };
  for (const char* text : kCases) {
    CAPTURE(text);
    const fs::path dir = scratch("invalid");
    write_file(dir / "mp4" / "default.json", text);
    const std::vector<fs::path> dirs{dir};
    Mapper mapper;
    Collect warnings;
    const TitleProfileLoad load = load_title_profile(mapper, TitleId::mp4, dirs, warnings.sink());
    CHECK_FALSE(load.from_file());
    CHECK(*mapper.profile(0) == builtin_default_profile());
    REQUIRE(warnings.lines.size() == 2);  // the bad file, then the fallback
    CHECK(warnings.lines[0].find("ignoring mp4 input profile") != std::string::npos);
    CHECK(warnings.lines[0].find("default.json") != std::string::npos);
    CHECK(warnings.lines[1].find("built-in default") != std::string::npos);
  }
}

TEST_CASE("profiles for another title are rejected") {
  SUBCASE("title field") {
    const fs::path dir = scratch("other_title");
    write_file(dir / "mp4" / "default.json", synthetic_profile("mp5", "mp5.minigame.synthetic_one"));
    const std::vector<fs::path> dirs{dir};
    const TitleProfileLoad load = find_title_profile(TitleId::mp4, dirs);
    CHECK_FALSE(load.from_file());
    REQUIRE(load.warnings.size() == 2);
    CHECK(load.warnings[0].find("made for title 'mp5'") != std::string::npos);
  }
  SUBCASE("untitled profile with another title's override") {
    const fs::path dir = scratch("other_override");
    write_file(dir / "mp4" / "default.json", synthetic_profile("", "mp5.minigame.synthetic_one"));
    const std::vector<fs::path> dirs{dir};
    const TitleProfileLoad load = find_title_profile(TitleId::mp4, dirs);
    CHECK_FALSE(load.from_file());
    REQUIRE(load.warnings.size() == 2);
    CHECK(load.warnings[0].find("belongs to another title") != std::string::npos);
  }
  SUBCASE("untitled profile with matching overrides is accepted") {
    const fs::path dir = scratch("untitled");
    write_file(dir / "mp4" / "default.json", synthetic_profile(""));
    const std::vector<fs::path> dirs{dir};
    const TitleProfileLoad load = find_title_profile(TitleId::mp4, dirs);
    CHECK(load.from_file());
    CHECK(load.warnings.empty());
  }
}

TEST_CASE("search directories are tried in order; a bad file does not hide a good one") {
  const fs::path bad = scratch("order_bad");
  const fs::path good = scratch("order_good");
  const fs::path later = scratch("order_later");
  write_file(bad / "mp4" / "default.json", "[]");
  fs::create_directories(bad / "mp4" / "default.json.d");  // unrelated entries are ignored
  write_file(good / "mp4" / "default.json", synthetic_profile());
  std::string other = synthetic_profile();
  other.replace(other.find("Synthetic"), 9, "Later");
  write_file(later / "mp4" / "default.json", other);
  const std::vector<fs::path> dirs{scratch("order_empty"), bad, good, later};

  const TitleProfileLoad load = find_title_profile(TitleId::mp4, dirs);
  CHECK(load.source == good / "mp4" / "default.json");
  CHECK(load.profile.name == "Synthetic");
  REQUIRE(load.warnings.size() == 1);
  CHECK(load.warnings[0].find("ignoring") != std::string::npos);
}

TEST_CASE("a directory where the profile file should be is skipped with a warning") {
  const fs::path dir = scratch("dir_not_file");
  fs::create_directories(dir / "mp4" / "default.json");
  const std::vector<fs::path> dirs{dir};
  const TitleProfileLoad load = find_title_profile(TitleId::mp4, dirs);
  CHECK_FALSE(load.from_file());
  REQUIRE(load.warnings.size() == 2);
  CHECK(load.warnings[0].find("not a regular file") != std::string::npos);
}

TEST_CASE("an oversized profile is rejected unread") {
  const fs::path dir = scratch("oversized");
  write_file(dir / "mp4" / "default.json", std::string(kMaxProfileFileBytes + 1, ' '));
  const std::vector<fs::path> dirs{dir};
  const TitleProfileLoad load = find_title_profile(TitleId::mp4, dirs);
  CHECK_FALSE(load.from_file());
  REQUIRE(load.warnings.size() == 2);
  CHECK(load.warnings[0].find("larger than") != std::string::npos);
}

TEST_CASE("a loaded override applies after set_minigame for that minigame") {
  const fs::path dir = scratch("override");
  write_file(dir / "mp4" / "default.json", synthetic_profile());
  const std::vector<fs::path> dirs{dir};
  Mapper mapper;
  REQUIRE(load_title_profile(mapper, TitleId::mp4, dirs, nullptr).from_file());

  CHECK(a_pressed(mapper, {"z"}));
  CHECK_FALSE(a_pressed(mapper, {"x"}));

  mapper.set_minigame("mp4.minigame.synthetic_one");
  CHECK(a_pressed(mapper, {"x"}));
  CHECK_FALSE(a_pressed(mapper, {"z"}));
  {  // controls the override does not list are inherited
    const std::vector<DeviceSnapshot> devices{keys({"j"})};
    CHECK((mapper.map(0, devices, keyboard_on_port0()).pads[0].buttons & kButtonB) != 0);
  }

  mapper.set_minigame("mp4.minigame.some_other_one");
  CHECK(a_pressed(mapper, {"z"}));
  mapper.set_minigame("");
  CHECK(a_pressed(mapper, {"z"}));
}

TEST_CASE("loading while a minigame is selected applies its override at once") {
  const fs::path dir = scratch("override_mid");
  write_file(dir / "mp4" / "default.json", synthetic_profile());
  const std::vector<fs::path> dirs{dir};
  Mapper mapper;
  mapper.set_minigame("mp4.minigame.synthetic_one");
  REQUIRE(load_title_profile(mapper, TitleId::mp4, dirs, nullptr).from_file());
  CHECK(a_pressed(mapper, {"x"}));
}

TEST_CASE("a throwing warning sink does not escape") {
  const std::vector<fs::path> dirs{scratch("sink_throws")};
  Mapper mapper;
  const WarningSink throwing = [](std::string_view) { throw std::runtime_error("log failed"); };
  TitleProfileLoad load;
  CHECK_NOTHROW(load = load_title_profile(mapper, TitleId::mp4, dirs, throwing));
  CHECK(load.profile == builtin_default_profile());
}

TEST_CASE("the shipped mp4 profile loads from the source profiles directory") {
  const std::vector<fs::path> dirs{fs::path(MPT_INPUT_PROFILES_DIR)};
  Mapper mapper;
  const TitleProfileLoad load = load_title_profile(mapper, TitleId::mp4, dirs, nullptr);
  CHECK(load.from_file());
  CHECK(load.warnings.empty());
  CHECK(load.profile.title == TitleId::mp4);
  CHECK(load.profile.minigames.count("mp4.minigame.bowsers_bigger_blast") == 1);
  // A title without a shipped profile falls back.
  CHECK_FALSE(find_title_profile(TitleId::mp1, dirs).from_file());
}
