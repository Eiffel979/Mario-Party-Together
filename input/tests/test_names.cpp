#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view through operator<<
#include <string>

#include "mpt/core/input.h"
#include "mpt/input/names.h"
#include "mpt/input/profile.h"

using namespace mpt::input;

TEST_CASE("key names round-trip and use HID usage IDs") {
  CHECK(key_from_name("a") == 4);
  CHECK(key_from_name("z") == 29);
  CHECK(key_from_name("0") == 39);
  CHECK(key_from_name("space") == 44);
  CHECK(key_from_name("up") == 82);
  CHECK(key_from_name("lshift") == 225);
  for (std::uint16_t code = 0; code < kKeyCount; ++code) {
    const auto name = key_name(code);
    if (!name.empty()) CHECK(key_from_name(name) == code);
  }
  CHECK_FALSE(key_from_name("A").has_value());
  CHECK_FALSE(key_from_name("shift").has_value());
  CHECK(key_name(0).empty());
}

TEST_CASE("pad button, axis and control names round-trip") {
  for (std::size_t i = 0; i < kPadButtonCount; ++i) {
    const auto b = static_cast<PadButton>(i);
    REQUIRE_FALSE(pad_button_name(b).empty());
    CHECK(pad_button_from_name(pad_button_name(b)) == b);
  }
  for (std::size_t i = 0; i < kPadAxisCount; ++i) {
    const auto a = static_cast<PadAxis>(i);
    REQUIRE_FALSE(pad_axis_name(a).empty());
    CHECK(pad_axis_from_name(pad_axis_name(a)) == a);
  }
  for (std::size_t i = 0; i < kControlCount; ++i) {
    const auto c = static_cast<Control>(i);
    REQUIRE_FALSE(control_name(c).empty());
    CHECK(control_from_name(control_name(c)) == c);
  }
  CHECK_FALSE(pad_button_from_name("a").has_value());
  CHECK_FALSE(control_from_name("trigger").has_value());
}

TEST_CASE("button controls line up with canonical Button bits") {
  auto bit = [](Control c) { return 1u << static_cast<unsigned>(c); };
  CHECK(bit(Control::a) == mpt::kButtonA);
  CHECK(bit(Control::zl) == mpt::kButtonZL);
  CHECK(bit(Control::start) == mpt::kButtonStart);
  CHECK(bit(Control::dpad_right) == mpt::kButtonDpadRight);
  CHECK(bit(Control::c_right) == mpt::kButtonCRight);
  CHECK(bit(Control::home) == mpt::kButtonHome);
  CHECK(kButtonControlCount == 19);
}

TEST_CASE("source strings parse and print canonically") {
  struct Case {
    const char* text;
    Source::Kind kind;
    std::uint16_t code;
    const char* canonical;
  };
  const Case cases[] = {
      {"key:w", Source::Kind::key, 26, "key:w"},
      {"button:south", Source::Kind::pad_button, static_cast<std::uint16_t>(PadButton::south), "button:south"},
      {"axis:left_y-", Source::Kind::pad_axis_negative, static_cast<std::uint16_t>(PadAxis::left_y), "axis:left_y-"},
      {"axis:right_x+", Source::Kind::pad_axis_positive, static_cast<std::uint16_t>(PadAxis::right_x),
       "axis:right_x+"},
      {"axis:left_trigger", Source::Kind::pad_axis_positive, static_cast<std::uint16_t>(PadAxis::left_trigger),
       "axis:left_trigger"},
      {"axis:right_trigger+", Source::Kind::pad_axis_positive, static_cast<std::uint16_t>(PadAxis::right_trigger),
       "axis:right_trigger"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.text);
    const auto s = source_from_string(c.text);
    REQUIRE(s.has_value());
    CHECK(s->kind == c.kind);
    CHECK(s->code == c.code);
    CHECK(source_to_string(*s) == c.canonical);
  }
  for (const char* bad : {"", "w", "key:", "key:W", "key:#", "key:#512", "key:#-1", "key:#4x", "key:# 4", "button:a", "axis:left_x", "axis:left_trigger-", "mouse:left",
                          "axis:left_z+"}) {
    CAPTURE(bad);
    CHECK_FALSE(source_from_string(bad).has_value());
  }
}

TEST_CASE("every key code round-trips through its source string, named or not") {
  CHECK(source_to_string(Source{Source::Kind::key, 100}) == "key:#100");  // HID 0x64, no name
  CHECK(source_from_string("key:#4") == Source{Source::Kind::key, 4});      // numeric form of a named key
  CHECK(source_to_string(Source{Source::Kind::key, 4}) == "key:a");
  for (std::uint16_t code = 0; code < kKeyCount; ++code) {
    const Source s{Source::Kind::key, code};
    const std::string text = source_to_string(s);
    CAPTURE(text);
    CHECK(source_from_string(text) == s);
  }
}
