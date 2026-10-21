#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view through operator<<
#include <string>

#include "mpt/input/profile.h"

using namespace mpt::input;

namespace {

// A small synthetic profile exercising every section of the format.
constexpr const char* kSample = R"({
  "format": "mpt.input_profile",
  "version": 1,
  "name": "Test profile",
  "title": "mp4",
  "options": { "stick_deadzone_percent": 20, "trigger_button_threshold": 200 },
  "bindings": {
    "a": ["key:k", "button:south"],
    "stick_up": ["key:w", "axis:left_y-"],
    "trigger_l": ["axis:left_trigger"]
  },
  "minigames": {
    "mp4.minigame.test_mash": {
      "note": "mash A on either face button",
      "options": { "normalize_digital_diagonals": false, "stick_half_affects_triggers": true },
      "bindings": { "a": ["button:south", "button:east"], "stick_up": [] }
    }
  }
})";

std::size_t at(Control c) { return static_cast<std::size_t>(c); }

std::string with_replaced(std::string text, const std::string& from, const std::string& to) {
  const auto pos = text.find(from);
  REQUIRE(pos != std::string::npos);
  text.replace(pos, from.size(), to);
  return text;
}

}  // namespace

TEST_CASE("profile parses every section") {
  Profile p;
  REQUIRE(parse_profile(kSample, p));
  CHECK(p.name == "Test profile");
  CHECK(p.title == mpt::TitleId::mp4);
  CHECK(p.base.options.stick_deadzone_percent == 20);
  CHECK(p.base.options.trigger_button_threshold == 200);
  CHECK(p.base.options.stick_half_percent == Options{}.stick_half_percent);  // unlisted options keep defaults
  CHECK(p.base.bindings[at(Control::a)].size() == 2);
  CHECK(p.base.bindings[at(Control::b)].empty());  // unlisted controls are unbound
  REQUIRE(p.minigames.count("mp4.minigame.test_mash") == 1);
  CHECK(p.minigames.at("mp4.minigame.test_mash").note == "mash A on either face button");
}

TEST_CASE("per-minigame overrides replace listed controls and options only") {
  Profile p;
  REQUIRE(parse_profile(kSample, p));

  const ResolvedProfile base = p.resolve();
  CHECK(base == p.base);
  CHECK(p.resolve("mp4.minigame.unknown") == p.base);

  const ResolvedProfile mash = p.resolve("mp4.minigame.test_mash");
  CHECK(mash.options.normalize_digital_diagonals == false);
  CHECK(mash.options.stick_half_affects_triggers == true);
  CHECK(base.options.stick_half_affects_triggers == false);  // default
  CHECK(mash.options.stick_deadzone_percent == 20);  // inherited
  REQUIRE(mash.bindings[at(Control::a)].size() == 2);
  CHECK(source_to_string(mash.bindings[at(Control::a)][1]) == "button:east");
  CHECK(mash.bindings[at(Control::stick_up)].empty());  // [] unbinds
  CHECK(mash.bindings[at(Control::trigger_l)] == p.base.bindings[at(Control::trigger_l)]);  // inherited
}

TEST_CASE("profile JSON round-trips") {
  Profile p;
  REQUIRE(parse_profile(kSample, p));
  Profile again;
  REQUIRE(parse_profile(to_json(p), again));
  CHECK(again == p);
  CHECK(to_json(again) == to_json(p));  // canonical output is stable
}

TEST_CASE("a profile with an unnamed key code round-trips") {
  Profile p;
  p.name = "unnamed key";
  p.base.bindings[at(Control::a)] = {Source{Source::Kind::key, 100}};  // HID 0x64 has no name
  const std::string text = to_json(p);
  CHECK(text.find("\"key:#100\"") != std::string::npos);
  Profile again;
  REQUIRE(parse_profile(text, again));
  CHECK(again == p);
}

TEST_CASE("the built-in default profile is valid and round-trips") {
  const Profile& p = builtin_default_profile();
  CHECK_FALSE(p.title.has_value());
  CHECK(p.base.options.trigger_button_threshold == 230);
  for (Control c : {Control::a, Control::b, Control::x, Control::y, Control::z, Control::start, Control::stick_up,
                    Control::stick_down, Control::stick_left, Control::stick_right, Control::c_stick_up,
                    Control::trigger_l, Control::trigger_r, Control::stick_half}) {
    CAPTURE(control_name(c));
    CHECK_FALSE(p.base.bindings[at(c)].empty());
  }
  Profile again;
  REQUIRE(parse_profile(to_json(p), again));
  CHECK(again == p);
}

TEST_CASE("profile errors are reported and leave the output unchanged") {
  struct Case {
    std::string json;
    const char* expect;  // substring of the error message
  };
  const std::string s = kSample;
  const Case cases[] = {
      {"{", "not valid JSON"},
      {"[]", "top level"},
      {with_replaced(s, "\"mpt.input_profile\"", "\"other\""), "format"},
      {with_replaced(s, "\"version\": 1", "\"version\": 2"), "unsupported version"},
      {with_replaced(s, "\"version\": 1", "\"version\": \"1\""), "version"},
      {with_replaced(s, "\"name\": \"Test profile\"", "\"name\": \"\""), "name"},
      {with_replaced(s, "\"title\": \"mp4\"", "\"title\": \"mp99\""), "unknown title"},
      {with_replaced(s, "\"title\"", "\"titel\""), "unknown key 'titel'"},
      {with_replaced(s, "\"stick_deadzone_percent\": 20", "\"stick_deadzone_percent\": 95"), "0..90"},
      {with_replaced(s, "\"stick_deadzone_percent\": 20", "\"stick_deadzone_percent\": 2.5"), "integer"},
      {with_replaced(s, "\"stick_deadzone_percent\"", "\"deadzone\""), "unknown option 'deadzone'"},
      {with_replaced(s, "\"normalize_digital_diagonals\": false", "\"normalize_digital_diagonals\": 0"),
       "true or false"},
      {with_replaced(s, "\"stick_half_affects_triggers\": true", "\"stick_half_affects_triggers\": 1"),
       "true or false"},
      {with_replaced(s, "\"a\": [\"key:k\"", "\"jump\": [\"key:k\""), "unknown control 'jump'"},
      {with_replaced(s, "\"key:k\"", "\"key:kk\""), "unknown source 'key:kk'"},
      {with_replaced(s, "\"key:k\", \"button:south\"", "\"key:k\", \"key:k\""), "duplicate source"},
      {with_replaced(s, "\"trigger_l\": [\"axis:left_trigger\"]", "\"trigger_l\": \"axis:left_trigger\""),
       "array"},
      {with_replaced(s, "mp4.minigame.test_mash", "mp4.board.test_mash"), "kind 'minigame'"},
      {with_replaced(s, "mp4.minigame.test_mash", "mp3.minigame.test_mash"), "another title"},
      {with_replaced(s, "\"note\"", "\"comment\""), "unknown key 'comment'"},
      {with_replaced(s, "\"bindings\": {\n    \"a\"", "\"bindingz\": {\n    \"a\""), "unknown key 'bindingz'"},
      // Duplicate keys at every level: top level, options, bindings, a minigame entry.
      {with_replaced(s, "\"version\": 1,", "\"version\": 1, \"version\": 1,"), "duplicate key 'version'"},
      {with_replaced(s, "\"trigger_button_threshold\": 200", "\"trigger_button_threshold\": 200, "
                                                              "\"trigger_button_threshold\": 100"),
       "duplicate key 'trigger_button_threshold'"},
      {with_replaced(s, "\"trigger_l\": [\"axis:left_trigger\"]",
                     "\"trigger_l\": [\"axis:left_trigger\"], \"a\": [\"key:j\"]"),
       "duplicate key 'a'"},
      {with_replaced(s, "\"stick_up\": [] }", "\"stick_up\": [], \"stick_up\": [] }"), "duplicate key 'stick_up'"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.json);
    Profile p;
    p.name = "untouched";
    const mpt::Result r = parse_profile(c.json, p);
    REQUIRE_FALSE(r.ok());
    CAPTURE(r.error());
    CHECK(r.error().find(c.expect) != std::string::npos);
    CHECK(p.name == "untouched");
  }
}

TEST_CASE("the same key in sibling or nested objects is not a duplicate") {
  Profile p;
  const mpt::Result r = parse_profile(R"({"format":"mpt.input_profile","version":1,"name":"x",
      "bindings":{"a":["key:k"]},
      "minigames":{
        "mp4.minigame.one":{"note":"n","bindings":{"a":["key:j"]}},
        "mp4.minigame.two":{"note":"n","bindings":{"a":["key:l"]}}}})",
                                      p);
  CAPTURE(r.error());
  REQUIRE(r.ok());
  CHECK(p.minigames.size() == 2);
}

TEST_CASE("bindings section is required; title is optional") {
  Profile p;
  CHECK_FALSE(parse_profile(R"({"format":"mpt.input_profile","version":1,"name":"x"})", p).ok());
  REQUIRE(parse_profile(R"({"format":"mpt.input_profile","version":1,"name":"x","bindings":{}})", p));
  CHECK_FALSE(p.title.has_value());
  CHECK(p.base.options == Options{});
}
