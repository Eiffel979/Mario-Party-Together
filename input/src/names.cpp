#include "mpt/input/names.h"

#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace mpt::input {
namespace {

struct KeyName {
  std::uint16_t code;
  std::string_view name;
};

// USB HID Usage Tables, keyboard/keypad page (0x07). Identical to SDL scancodes.
constexpr KeyName kKeys[] = {
    {4, "a"},    {5, "b"},    {6, "c"},    {7, "d"},    {8, "e"},    {9, "f"},    {10, "g"},
    {11, "h"},   {12, "i"},   {13, "j"},   {14, "k"},   {15, "l"},   {16, "m"},   {17, "n"},
    {18, "o"},   {19, "p"},   {20, "q"},   {21, "r"},   {22, "s"},   {23, "t"},   {24, "u"},
    {25, "v"},   {26, "w"},   {27, "x"},   {28, "y"},   {29, "z"},
    {30, "1"},   {31, "2"},   {32, "3"},   {33, "4"},   {34, "5"},   {35, "6"},   {36, "7"},
    {37, "8"},   {38, "9"},   {39, "0"},
    {40, "enter"},        {41, "escape"},       {42, "backspace"},    {43, "tab"},
    {44, "space"},        {45, "minus"},        {46, "equals"},       {47, "left_bracket"},
    {48, "right_bracket"}, {49, "backslash"},   {51, "semicolon"},    {52, "apostrophe"},
    {53, "grave"},        {54, "comma"},        {55, "period"},       {56, "slash"},
    {57, "caps_lock"},
    {58, "f1"},  {59, "f2"},  {60, "f3"},  {61, "f4"},  {62, "f5"},  {63, "f6"},
    {64, "f7"},  {65, "f8"},  {66, "f9"},  {67, "f10"}, {68, "f11"}, {69, "f12"},
    {70, "print_screen"}, {71, "scroll_lock"},  {72, "pause"},        {73, "insert"},
    {74, "home"},         {75, "page_up"},      {76, "delete"},       {77, "end"},
    {78, "page_down"},    {79, "right"},        {80, "left"},         {81, "down"},
    {82, "up"},           {83, "num_lock"},     {84, "kp_divide"},    {85, "kp_multiply"},
    {86, "kp_minus"},     {87, "kp_plus"},      {88, "kp_enter"},
    {89, "kp_1"}, {90, "kp_2"}, {91, "kp_3"}, {92, "kp_4"}, {93, "kp_5"},
    {94, "kp_6"}, {95, "kp_7"}, {96, "kp_8"}, {97, "kp_9"}, {98, "kp_0"},
    {99, "kp_period"},
    {224, "lctrl"}, {225, "lshift"}, {226, "lalt"}, {227, "lgui"},
    {228, "rctrl"}, {229, "rshift"}, {230, "ralt"}, {231, "rgui"},
};

constexpr std::array<std::string_view, kPadButtonCount> kPadButtons = {
    "south",         "east",         "west",          "north",        "back",          "guide",
    "start",         "left_stick",   "right_stick",   "left_shoulder", "right_shoulder", "dpad_up",
    "dpad_down",     "dpad_left",    "dpad_right",    "misc1",        "right_paddle1", "left_paddle1",
    "right_paddle2", "left_paddle2", "touchpad",
};

constexpr std::array<std::string_view, kPadAxisCount> kPadAxes = {
    "left_x", "left_y", "right_x", "right_y", "left_trigger", "right_trigger",
};

}  // namespace

std::optional<std::uint16_t> key_from_name(std::string_view name) {
  for (const KeyName& k : kKeys) {
    if (k.name == name) return k.code;
  }
  return std::nullopt;
}

std::string_view key_name(std::uint16_t key) {
  for (const KeyName& k : kKeys) {
    if (k.code == key) return k.name;
  }
  return {};
}

std::optional<PadButton> pad_button_from_name(std::string_view name) {
  for (std::size_t i = 0; i < kPadButtons.size(); ++i) {
    if (kPadButtons[i] == name) return static_cast<PadButton>(i);
  }
  return std::nullopt;
}

std::string_view pad_button_name(PadButton button) {
  const auto i = static_cast<std::size_t>(button);
  return i < kPadButtons.size() ? kPadButtons[i] : std::string_view{};
}

std::optional<PadAxis> pad_axis_from_name(std::string_view name) {
  for (std::size_t i = 0; i < kPadAxes.size(); ++i) {
    if (kPadAxes[i] == name) return static_cast<PadAxis>(i);
  }
  return std::nullopt;
}

std::string_view pad_axis_name(PadAxis axis) {
  const auto i = static_cast<std::size_t>(axis);
  return i < kPadAxes.size() ? kPadAxes[i] : std::string_view{};
}

}  // namespace mpt::input
