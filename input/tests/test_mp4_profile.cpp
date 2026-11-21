// The shipped MP4 profile (input/profiles/mp4/default.json) and the E2 input
// coverage table in docs/titles/mp4.md §18, checked against the §12 catalog.
// Synthetic device state only.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <ostream>  // doctest prints std::string_view through operator<<
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "mpt/core/content_id.h"
#include "mpt/input/mapper.h"
#include "mpt/input/names.h"
#include "mpt/input/reserved.h"

using namespace mpt;
using namespace mpt::input;

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE_MESSAGE(in.good(), "cannot open " << path);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

Profile mp4_profile() {
  Profile p;
  const Result r = parse_profile(read_file(MPT_INPUT_PROFILES_DIR "/mp4/default.json"), p);
  REQUIRE_MESSAGE(r.ok(), r.error());
  return p;
}

std::vector<std::string> lines_of(const std::string& text) {
  std::vector<std::string> out;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(line);
  }
  return out;
}

// Table cells of a Markdown row "| a | b | c |", trimmed.
std::vector<std::string> cells(const std::string& row) {
  std::vector<std::string> out;
  std::size_t start = 1;
  for (std::size_t bar = row.find('|', start); bar != std::string::npos; bar = row.find('|', start)) {
    std::string cell = row.substr(start, bar - start);
    const auto b = cell.find_first_not_of(' ');
    const auto e = cell.find_last_not_of(' ');
    out.push_back(b == std::string::npos ? std::string() : cell.substr(b, e - b + 1));
    start = bar + 1;
  }
  return out;
}

std::string unquote(const std::string& cell) {
  return cell.size() >= 2 && cell.front() == '`' && cell.back() == '`' ? cell.substr(1, cell.size() - 2) : cell;
}

// Rows of the first table in the section that starts with `heading`, keyed by
// the ID in the first column.
std::map<std::string, std::vector<std::string>> table_rows(const std::string& heading) {
  std::map<std::string, std::vector<std::string>> rows;
  bool in_section = false;
  for (const std::string& line : lines_of(read_file(MPT_DOCS_DIR "/titles/mp4.md"))) {
    if (line.rfind("#", 0) == 0) {
      if (in_section) break;
      in_section = line.rfind(heading, 0) == 0;
      continue;
    }
    if (!in_section || line.rfind("| `mp4.", 0) != 0) continue;
    const auto c = cells(line);
    REQUIRE(!c.empty());
    CHECK_MESSAGE(rows.emplace(unquote(c[0]), c).second, "duplicate row " << c[0]);
  }
  return rows;
}

// §12: ID -> category text ("4-player (0)", "Extra Room (7)", ...).
std::map<std::string, std::string> catalog_minigames() {
  std::map<std::string, std::string> out;
  for (const auto& [id, c] : table_rows("### Minigames")) {
    REQUIRE(c.size() >= 4);
    out[id] = c[3];
  }
  return out;
}

// §18's table: every minigame outside Extra Room and Story (the E2 proof rows).
bool in_coverage_table(const std::string& category) {
  return category.find("Extra Room") == std::string::npos && category.find("Story") == std::string::npos;
}

// A regular party can launch it. No board code launches the Item minigames;
// they come only from Mini-game mode and Story (docs/titles/mp4.md §18.1).
bool party_eligible(const std::string& category) {
  return in_coverage_table(category) && category.find("Item") == std::string::npos;
}

PortLayout keyboard_on_port0() {
  PortLayout layout;
  layout.seated[0] = true;
  layout.device_port[kKeyboardDevice] = 0;
  return layout;
}

DeviceSnapshot keys(std::initializer_list<std::string_view> held) {
  DeviceSnapshot s;
  s.id = kKeyboardDevice;
  for (std::string_view name : held) s.keyboard.set(*key_from_name(name));
  return s;
}

}  // namespace

TEST_CASE("the MP4 profile parses, is for mp4 and round-trips") {
  const Profile p = mp4_profile();
  REQUIRE(p.title.has_value());
  CHECK(*p.title == TitleId::mp4);
  Profile again;
  REQUIRE(parse_profile(to_json(p), again));
  CHECK(again == p);
  // Base bindings match the built-in GameCube default (MP4 has no Select or Home).
  for (std::size_t c = 0; c < kControlCount; ++c) {
    const auto control = static_cast<Control>(c);
    if (control == Control::select) continue;
    CAPTURE(control_name(control));
    CHECK(p.base.bindings[c] == builtin_default_profile().base.bindings[c]);
  }
  CHECK(p.base.options == builtin_default_profile().base.options);
}

TEST_CASE("the MP4 profile binds every GameCube control on keyboard and gamepad") {
  const Profile p = mp4_profile();
  for (Control c : {Control::a, Control::b, Control::x, Control::y, Control::z, Control::start, Control::dpad_up,
                    Control::dpad_down, Control::dpad_left, Control::dpad_right, Control::stick_up,
                    Control::stick_down, Control::stick_left, Control::stick_right, Control::c_stick_up,
                    Control::c_stick_down, Control::c_stick_left, Control::c_stick_right, Control::trigger_l,
                    Control::trigger_r}) {
    CAPTURE(control_name(c));
    bool keyboard = false;
    bool gamepad = false;
    for (const Source& s : p.base.bindings[static_cast<std::size_t>(c)]) {
      (s.is_keyboard() ? keyboard : gamepad) = true;
    }
    CHECK(keyboard);
    CHECK(gamepad);
  }
  // L/R: keys directly; gamepads through the analog trigger's click threshold.
  CHECK(!p.base.bindings[static_cast<std::size_t>(Control::l)].empty());
  CHECK(!p.base.bindings[static_cast<std::size_t>(Control::r)].empty());
  CHECK(p.base.options.trigger_button_threshold > 0);
}

TEST_CASE("the MP4 profile binds no reserved input") {
  const Profile p = mp4_profile();
  auto check = [](const std::vector<Source>& sources) {
    for (const Source& s : sources) {
      CAPTURE(source_to_string(s));
      CHECK_FALSE(is_reserved(s));
    }
  };
  for (const auto& list : p.base.bindings) check(list);
  for (const auto& [id, over] : p.minigames) {
    for (const auto& list : over.bindings) {
      if (list) check(*list);
    }
  }
}

TEST_CASE("the MP4 profile binds Start and leaves the pause chord modifier Back unbound") {
  // Back + Start is the gamepad pause chord (reserved.h): Back is held for a few
  // frames before Start, so it must press nothing in the game.
  const Profile p = mp4_profile();
  const Source back{Source::Kind::pad_button, static_cast<std::uint16_t>(kHostPauseChordModifier)};
  const Source start{Source::Kind::pad_button, static_cast<std::uint16_t>(kHostPauseChordButton)};
  const auto& start_list = p.base.bindings[static_cast<std::size_t>(Control::start)];
  CHECK(std::find(start_list.begin(), start_list.end(), start) != start_list.end());
  auto check = [&](const std::vector<Source>& sources) {
    for (const Source& s : sources) CHECK_FALSE(s == back);
  };
  for (const auto& list : p.base.bindings) check(list);
  for (const auto& [id, over] : p.minigames) {
    CAPTURE(id);
    for (const auto& list : over.bindings) {
      if (list) check(*list);
    }
  }
}

TEST_CASE("every MP4 override is a catalog minigame with a note and a coverage row") {
  const Profile p = mp4_profile();
  const auto catalog = catalog_minigames();
  const auto coverage = table_rows("## 18. Input coverage");
  REQUIRE_FALSE(p.minigames.empty());
  for (const auto& [id, over] : p.minigames) {
    CAPTURE(id);
    const auto parsed = ContentId::parse(id);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind() == "minigame");
    CHECK(parsed->title() == TitleId::mp4);
    CHECK(catalog.count(id) == 1);
    CHECK_FALSE(over.note.empty());
    const auto row = coverage.find(id);
    REQUIRE(row != coverage.end());
    REQUIRE(row->second.size() >= 7);
    CHECK(row->second[5].rfind("override", 0) == 0);  // the Keyboard column names the override
  }
}

TEST_CASE("the coverage table has the E2 rows, Item rows marked not party-eligible, and their sign-off") {
  const auto catalog = catalog_minigames();
  CHECK(catalog.size() == 61);
  const auto coverage = table_rows("## 18. Input coverage");
  std::set<std::string> expected;
  std::set<std::string> eligible;
  for (const auto& [id, category] : catalog) {
    if (!in_coverage_table(category)) continue;
    expected.insert(id);
    if (party_eligible(category)) eligible.insert(id);
  }
  CHECK(expected.size() == 49);
  CHECK(eligible.size() == 44);  // the five Item minigames are not launched by any board (§18.1)
  // §18.1 (PR #91, merge c8e6ace; corrected at d7c1fb0, 3cf989f and 9c51eb0):
  // the rows accepted under M4-E Option C (§18.1 at b1b2bd7), and the one row
  // not proven, Goomba's Chip Flip (an Item row, outside Option C).
  const std::set<std::string> accepted = {
      "mp4.minigame.mario_medley", "mp4.minigame.long_claw_of_the_law", "mp4.minigame.makin_waves",
      "mp4.minigame.pair_a_sailing", "mp4.minigame.dungeon_duos", "mp4.minigame.balloon_of_doom"};
  const std::set<std::string> not_proven = {"mp4.minigame.goombas_chip_flip"};
  std::set<std::string> listed;
  int proven = 0;
  int accepted_rows = 0;
  int not_proven_rows = 0;
  for (const auto& [id, c] : coverage) {
    CAPTURE(id);
    listed.insert(id);
    REQUIRE(catalog.count(id) == 1);
    REQUIRE(c.size() == 8);
    CHECK(!c[3].empty());  // inputs read
    CHECK(!c[5].empty());  // keyboard
    CHECK(!c[6].empty());  // gamepad
    // Category: Item rows say they are not party-eligible, no other row does.
    const bool marked = c[2].find("not party-eligible") != std::string::npos;
    CHECK(marked == (eligible.count(id) == 0));
    // Sign-off cites the game-side proof and its merge, and says proven or not.
    CHECK(c[7].find("\xC2\xA7" "18.1") != std::string::npos);
    CHECK(c[7].find("\xC2\xA7" "18.1 @ `") != std::string::npos);
    if (accepted.count(id) == 1) {
      CHECK(eligible.count(id) == 1);  // Option C covers party-eligible rows only
      CHECK(c[7] == "accepted (M4-E Option C): game side partly proven, reason \xC2\xA7" "18.1 @ `b1b2bd7`");
      ++accepted_rows;
    } else if (not_proven.count(id) == 1) {
      CHECK(c[7].rfind("not proven: ", 0) == 0);
      CHECK(c[7].find("PR #91, merge `c8e6ace`") != std::string::npos);
      ++not_proven_rows;
    } else if (id == "mp4.minigame.take_a_breather") {
      // Proven after its inputs were corrected to the L and R clicks.
      CHECK(c[7] == "proven: device @ `9dac583`, game side \xC2\xA7" "18.1 @ `9c51eb0` (rerun at `bda4902`; "
                    "PR #91, merge `c8e6ace`)");
      ++proven;
    } else {
      CHECK(c[7] == "proven: device @ `9dac583`, game side \xC2\xA7" "18.1 @ `3cf989f` (PR #91, merge `c8e6ace`)");
      ++proven;
    }
    // A row says "override" exactly when the profile has one.
    const bool overridden = c[5].rfind("override", 0) == 0 || c[6].rfind("override", 0) == 0;
    CHECK(overridden == (mp4_profile().minigames.count(id) == 1));
  }
  CHECK(listed == expected);
  CHECK(proven == 42);
  CHECK(accepted_rows == 6);
  CHECK(not_proven_rows == 1);
}

TEST_CASE("Bowser's Bigger Blast: lshift+E holds R part-way, E commits") {
  Mapper mapper;
  mapper.set_profile_all(mp4_profile());
  const PortLayout layout = keyboard_on_port0();
  const std::vector<DeviceSnapshot> half{keys({"lshift", "e"})};
  const std::vector<DeviceSnapshot> full{keys({"e"})};

  // Outside the minigame: the base mapping, a full press with the R click.
  PadState p = mapper.map(0, half, layout).pads[0];
  CHECK(p.trigger_r == 255);
  CHECK((p.buttons & kButtonR) != 0);

  mapper.set_minigame("mp4.minigame.bowsers_bigger_blast");
  p = mapper.map(1, half, layout).pads[0];
  CHECK(p.trigger_r == 128);
  CHECK((p.buttons & kButtonR) == 0);
  p = mapper.map(2, full, layout).pads[0];
  CHECK(p.trigger_r == 255);
  CHECK((p.buttons & kButtonR) != 0);  // from trigger_button_threshold
  // Q (L) is untouched by the override.
  p = mapper.map(3, std::vector<DeviceSnapshot>{keys({"q"})}, layout).pads[0];
  CHECK((p.buttons & kButtonL) != 0);

  mapper.set_minigame("");
  CHECK(mapper.map(4, half, layout).pads[0].trigger_r == 255);
}
