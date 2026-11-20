// The dump lane's E2 game-side proof (M4-07, tests/dump_lane/e2.h), checked without a dump: its rows are
// docs/titles/mp4.md section 18's, each row injects every control its "Inputs read" cell lists, the
// injection is back at idle on every checkpoint, the trigger values reach what section 18 needs after
// PADClamp, and the verdict only counts a divergence inside both minigame windows.
//
// Synthetic data only. No game material.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../dump_lane/e2.h"

using namespace mpt;
namespace e2 = mpt::dump_lane::e2;

namespace {

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

// Section 18's coverage table: ID -> "Inputs read" cell.
std::map<std::string, std::string> section18() {
  std::ifstream in(MPT_DOCS_DIR "/titles/mp4.md", std::ios::binary);
  REQUIRE(in.good());
  std::map<std::string, std::string> out;
  bool in_section = false;
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("#", 0) == 0) {
      if (in_section) break;  // the next heading, including a ### subsection
      in_section = line.rfind("## 18. Input coverage", 0) == 0;
      continue;
    }
    if (!in_section || line.rfind("| `mp4.", 0) != 0) continue;
    const auto c = cells(line);
    REQUIRE(c.size() >= 4);
    out[c[0].substr(1, c[0].size() - 2)] = c[3];
  }
  return out;
}

bool has(const std::string& text, const char* word) { return text.find(word) != std::string::npos; }

// The controls a cell names, read the way section 18 writes them.
std::set<e2::Control> controls_named(const std::string& cell) {
  using C = e2::Control;
  std::set<C> out;
  std::istringstream items(cell);
  for (std::string item; std::getline(items, item, ',');) {
    item.erase(0, item.find_first_not_of(' '));
    if (item.rfind("any of ", 0) == 0) item.erase(0, 7);
    if (item.rfind("stick", 0) == 0) out.insert(C::stick);
    else if (item.rfind("digital buttons", 0) == 0) out.insert(C::a);  // a digital button stands for them
    else if (item.rfind("A", 0) == 0) out.insert(C::a);
    else if (item.rfind("B", 0) == 0) out.insert(C::b);
    else if (item.rfind("X", 0) == 0) out.insert(C::x);
    else if (item.rfind("Y", 0) == 0) out.insert(C::y);
    else if (item.rfind("L click", 0) == 0) out.insert(C::l_click);
    else if (item.rfind("R click", 0) == 0) out.insert(C::r_click);
    else if (item.rfind("L/R", 0) == 0) out.insert({C::l_analog, C::r_analog});
    else if (item.rfind("L past", 0) == 0) out.insert(C::l_analog);
    else if (item.rfind("R past", 0) == 0 || item.rfind("R throttle", 0) == 0) out.insert(C::r_analog);
    else if (item.rfind("R pressure", 0) == 0) {
      out.insert(C::r_half);  // "R pressure (held part-way) and R click"
      if (has(item, "R click")) out.insert(C::r_click);
    } else FAIL_CHECK("unknown item '" << item << "' in '" << cell << "'");
  }
  return out;
}

// PADClamp's trigger window (runtimes/gc_wii/sdk, pinned by test_pad_vi.cpp) and the SDK's L/R rule:
// a digital press or raw 255 is full travel.
int clamped(std::uint8_t raw) { return raw <= 30 ? 0 : std::min<int>(raw, 180) - 30; }

}  // namespace

TEST_CASE("E2 rows are section 18's rows, with the controls each row lists") {
  const auto doc = section18();
  REQUIRE(doc.size() == 49);
  std::set<std::string> ids;
  for (const e2::Row& row : e2::rows()) {
    const std::string id(row.id);
    CAPTURE(id);
    CHECK(ids.insert(id).second);
    const auto it = doc.find(id);
    REQUIRE(it != doc.end());
    const std::set<e2::Control> listed(row.controls.begin(), row.controls.end());
    CHECK(listed.size() == row.controls.size());
    CHECK(listed == controls_named(it->second));
    CHECK(listed.count(e2::Control::z) == 0);  // Z is the null control, never a listed one
    CHECK(row.number >= 401);
    CHECK(row.number <= 463);
    CHECK(row.r_half_vs_full == (id == "mp4.minigame.bowsers_bigger_blast"));
  }
  CHECK(ids.size() == doc.size());
}

TEST_CASE("E2 injection is idle on every checkpoint and active only in the burst") {
  const e2::Control all[] = {e2::Control::stick,    e2::Control::a,        e2::Control::b,       e2::Control::x,
                             e2::Control::y,        e2::Control::z,        e2::Control::l_click, e2::Control::r_click,
                             e2::Control::l_analog, e2::Control::r_analog, e2::Control::r_half,  e2::Control::r_full};
  const PadState idle = e2::idle_pad();
  CHECK(idle.connected == 1);
  for (e2::Control c : all) {
    CAPTURE(e2::name(c));
    bool active_seen = false;
    for (std::uint64_t t = 0; t < 4 * e2::kCycle; ++t) {
      const PadState p = e2::inject(c, t);
      CHECK(p.connected == 1);
      const bool is_idle = p.buttons == 0 && p.stick_x == 0 && p.stick_y == 0 && p.trigger_l == 0 && p.trigger_r == 0;
      if (t % e2::kCycle >= e2::kActive) CHECK(is_idle);  // the gap
      if (e2::is_checkpoint(t)) {
        CHECK(is_idle);
        // The gap before a checkpoint is idle the whole way.
        for (std::uint64_t g = t + 1 - (e2::kCycle - e2::kActive); g <= t; ++g) {
          const PadState q = e2::inject(c, g);
          CHECK((q.buttons == 0 && q.stick_x == 0 && q.trigger_l == 0 && q.trigger_r == 0));
        }
      }
      active_seen = active_seen || !is_idle;
    }
    CHECK(active_seen);
  }
  // Buttons pulse inside the burst, so the game sees presses as well as holds.
  int presses = 0;
  for (std::uint64_t t = 1; t < e2::kCycle; ++t)
    presses += (e2::inject(e2::Control::a, t).buttons != 0 && e2::inject(e2::Control::a, t - 1).buttons == 0) ? 1 : 0;
  CHECK(presses >= 2);
  // The stick turns through four full directions, one per cycle.
  std::set<std::pair<int, int>> dirs;
  for (std::uint64_t k = 0; k < 4; ++k) {
    const PadState p = e2::inject(e2::Control::stick, k * e2::kCycle);
    dirs.insert({p.stick_x, p.stick_y});
    CHECK(std::max(std::abs(p.stick_x), std::abs(p.stick_y)) == 32767);
  }
  CHECK(dirs.size() == 4);
}

TEST_CASE("E2 trigger values reach section 18's thresholds after PADClamp") {
  // Analog pulls: past every "/255" threshold (20, 80, 10 against the clamped 0..150 value), past the
  // game's analog-L/R bit (clamped & 0xC0, i.e. 64 and up), and short of the digital click (raw 255).
  const PadState l = e2::inject(e2::Control::l_analog, 0);
  CHECK(l.buttons == 0);
  CHECK(l.trigger_l == e2::kAnalog);
  CHECK(l.trigger_l < 255);
  CHECK(clamped(l.trigger_l) == 150);
  CHECK((clamped(l.trigger_l) & 0xC0) != 0);
  CHECK(clamped(l.trigger_l) > 80);
  // Bowser's Bigger Blast: R=128 is a partial pull (clamped 98, no click), R=255 full travel and click.
  const PadState half = e2::inject(e2::Control::r_half, 0);
  const PadState full = e2::inject(e2::Control::r_full, 0);
  CHECK(half.trigger_r == 128);
  CHECK(clamped(half.trigger_r) == 98);
  CHECK(full.trigger_r == 255);
  CHECK(clamped(full.trigger_r) == 150);
  // Clicks are the digital buttons.
  CHECK(e2::inject(e2::Control::l_click, 0).buttons == kButtonL);
  CHECK(e2::inject(e2::Control::r_click, 0).buttons == kButtonR);
}

TEST_CASE("E2 verdict: a divergence counts only while both runs are in the minigame") {
  using CP = e2::Checkpoint;
  const std::vector<CP> idle = {{31, 1, true}, {63, 2, true}, {95, 3, true}, {127, 4, false}};
  CHECK_FALSE(e2::first_divergence(idle, idle));
  CHECK(e2::first_divergence(idle, {{31, 1, true}, {63, 9, true}, {95, 9, true}}) == 63u);
  // Differences after either run left the minigame do not count.
  CHECK_FALSE(e2::first_divergence(idle, {{31, 1, true}, {63, 2, true}, {95, 3, true}, {127, 9, false}}));
  CHECK_FALSE(e2::first_divergence(idle, {{31, 1, true}, {63, 2, false}, {95, 9, true}}));
  // Misaligned checkpoints prove nothing.
  CHECK_FALSE(e2::first_divergence(idle, {{30, 9, true}}));
  // A shorter run (stopped early) is compared as far as it goes.
  CHECK(e2::first_divergence(idle, {{31, 7, true}}) == 31u);
  CHECK(e2::focus_for_port(2) == 2);
}
