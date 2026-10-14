// Synthetic MusyX sound data, generated at test time (E10). Nothing here comes
// from a game: two tiny groups written in the GameCube MusyX layouts that musyx's
// PC loader validates (hw_pc_assets.c at the pin), all big-endian.
//
//   project: group 1 (SFX)  macros {1, 3}, samples {5, 6}, FX 10 -> macro 1, FX 11 -> macro 3
//            group 2 (song) macros {2},    samples {7},    program 0 -> macro 2, song 1
//   pool:    macro 1  START SAMPLE 5; WAIT for the sample's end; END
//            macro 2  START SAMPLE 7; WAIT for key-off;          END
//            macro 3  START SAMPLE 6; WAIT 120 ms;               END
//   samples: 5 = 1600 frames PCM16 one-shot (50 ms at 32 kHz, root key 60)
//            6, 7 = the same 320-frame looping PCM16 tone
//   song 1:  one track on MIDI channel 0, a pattern of notes and controller-16
//            changes, looping back to its start.
#pragma once

#include <cstdint>
#include <vector>

namespace mpt::gc::test::musyxdata {

struct Writer {
  std::vector<std::uint8_t> b;
  std::size_t size() const { return b.size(); }
  void u8(std::uint32_t v) { b.push_back(static_cast<std::uint8_t>(v)); }
  void u16(std::uint32_t v) {
    u8(v >> 8);
    u8(v);
  }
  void u32(std::uint32_t v) {
    u16(v >> 16);
    u16(v);
  }
  void align(std::size_t a) {
    while (b.size() % a) u8(0);
  }
  void put32(std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
  }
};

inline constexpr std::uint16_t kGroupSfx = 1, kGroupSong = 2, kSongId = 1;
inline constexpr std::uint16_t kFxOneShot = 10, kFxTimed = 11;
inline constexpr std::uint32_t kOneShotFrames = 1600, kLoopFrames = 320;
inline constexpr std::uint8_t kController = 16;  // general purpose 1

struct Data {
  std::vector<std::uint8_t> project, pool, sdir_sfx, sdir_song, samples, song;
};

inline Data make() {
  Data d;

  // ---- samples: PCM16, big-endian ------------------------------------------------------------
  Writer s;
  for (std::uint32_t i = 0; i < kOneShotFrames; ++i) s.u16(static_cast<std::uint16_t>(((i / 40) % 2 ? 9000 : -9000)));
  const std::uint32_t loop_off = static_cast<std::uint32_t>(s.size());
  for (std::uint32_t i = 0; i < kLoopFrames; ++i) {
    const std::int32_t tri = static_cast<std::int32_t>(i % 80) - 40;
    s.u16(static_cast<std::uint16_t>(tri * 300));
  }
  d.samples = s.b;

  // ---- sample directories (32-byte entries, 0xFFFF terminated) --------------------------------
  auto entry = [](Writer& w, std::uint16_t id, std::uint32_t off, std::uint32_t frames, std::uint32_t loop,
                  std::uint32_t loop_len) {
    w.u16(id);
    w.u16(0);
    w.u32(off);
    w.u32(0);                       // runtime address
    w.u32((60u << 24) | 32000u);    // root key 60, 32 kHz
    w.u32(frames | (2u << 24));     // SAMPLE_TYPE_PCM16
    w.u32(loop);
    w.u32(loop_len);
    w.u32(0);                       // no ADPCM metadata
  };
  Writer sd1;
  entry(sd1, 5, 0, kOneShotFrames, 0, 0);
  entry(sd1, 6, loop_off, kLoopFrames, 0, kLoopFrames);
  sd1.u16(0xFFFF);
  sd1.u16(0);
  d.sdir_sfx = sd1.b;
  Writer sd2;
  entry(sd2, 7, loop_off, kLoopFrames, 0, kLoopFrames);
  sd2.u16(0xFFFF);
  sd2.u16(0);
  d.sdir_song = sd2.b;

  // ---- pool: macros only ---------------------------------------------------------------------
  Writer p;
  p.u32(16);  // macros
  p.u32(0);   // curves
  p.u32(0);   // keymaps
  p.u32(0);   // layers
  auto macro = [&p](std::uint16_t id, const std::vector<std::uint32_t>& steps) {
    p.u32(8 + static_cast<std::uint32_t>(steps.size()) * 4);
    p.u16(id);
    p.u16(0);
    for (std::uint32_t w : steps) p.u32(w);
  };
  constexpr std::uint32_t kStartSample = 0x10, kWait = 0x04, kWaitMs = 0x07, kEnd = 0x00;
  macro(1, {kStartSample | (5u << 8), 0, kWait | (1u << 24), 0xFFFFu << 16, kEnd, 0});
  macro(2, {kStartSample | (7u << 8), 0, kWait | (1u << 8), 0xFFFFu << 16, kEnd, 0});
  macro(3, {kStartSample | (6u << 8), 0, kWaitMs, 120u << 16, kEnd, 0});
  p.u32(0xFFFFFFFF);
  d.pool = p.b;

  // ---- project --------------------------------------------------------------------------------
  Writer j;
  // Group 1 (SFX): 32-byte header, then lists and the FX table.
  const std::size_t g1 = j.size();
  for (int i = 0; i < 8; ++i) j.u32(0);
  j.b[g1 + 5] = kGroupSfx;
  j.b[g1 + 7] = 1;  // type 1: SFX
  auto list = [&j](std::size_t field, const std::vector<std::uint16_t>& ids) {
    j.put32(field, static_cast<std::uint32_t>(j.size()));
    for (std::uint16_t id : ids) j.u16(id);
    j.u16(0xFFFF);
  };
  list(g1 + 8, {1, 3});   // macros
  list(g1 + 12, {5, 6});  // samples
  list(g1 + 16, {});      // curves
  list(g1 + 20, {});      // keymaps
  list(g1 + 24, {});      // layers
  j.align(4);
  j.put32(g1 + 28, static_cast<std::uint32_t>(j.size()));
  j.u16(2);
  j.u16(0);
  auto fx = [&j](std::uint16_t id, std::uint16_t mac) {
    j.u16(id);
    j.u16(mac);
    j.u8(4);    // max voices
    j.u8(0x40); // priority
    j.u8(127);  // volume
    j.u8(64);   // panning
    j.u8(60);   // key
    j.u8(0);    // volume group
  };
  fx(kFxOneShot, 1);
  fx(kFxTimed, 3);
  j.align(4);
  // Group 2 (song): 40-byte header.
  const std::size_t g2 = j.size();
  j.put32(g1, static_cast<std::uint32_t>(g2));
  for (int i = 0; i < 10; ++i) j.u32(0);
  j.b[g2 + 5] = kGroupSong;
  j.b[g2 + 7] = 0;  // type 0: song
  list(g2 + 8, {2});
  list(g2 + 12, {7});
  list(g2 + 16, {});
  list(g2 + 20, {});
  list(g2 + 24, {});
  j.align(2);
  j.put32(g2 + 28, static_cast<std::uint32_t>(j.size()));  // normal page: program 0 -> macro 2
  j.u16(2);
  j.u8(0x40);
  j.u8(8);
  j.u8(0);
  j.u8(0);
  j.u16(0);
  j.u8(0);
  j.u8(0);
  j.u8(0xFF);
  j.u8(0);
  j.put32(g2 + 32, static_cast<std::uint32_t>(j.size()));  // drum page: empty
  j.u16(0);
  j.u8(0);
  j.u8(0);
  j.u8(0xFF);
  j.u8(0);
  j.align(4);
  j.put32(g2 + 36, static_cast<std::uint32_t>(j.size()));  // MIDI setup for song 1
  j.u16(kSongId);
  j.u16(0);
  for (int ch = 0; ch < 16; ++ch) {
    j.u8(0);    // program
    j.u8(100);  // volume
    j.u8(64);   // panning
    j.u8(0);    // reverb
    j.u8(0);    // chorus
  }
  const std::size_t end = j.size();
  j.put32(g2, static_cast<std::uint32_t>(end));
  j.u32(0xFFFFFFFF);  // end of groups
  d.project = j.b;

  // ---- song (arrangement) ------------------------------------------------------------------------
  Writer a;
  for (int i = 0; i < 6; ++i) a.u32(0);  // header: tTab, pTab, tmTab, mTrack, info, (single section)
  a.put32(16, 120);                       // info: 120 bpm
  const std::uint32_t ttab = static_cast<std::uint32_t>(a.size());
  a.put32(0, ttab);
  for (int t = 0; t < 64; ++t) a.u32(0);
  const std::uint32_t tmtab = static_cast<std::uint32_t>(a.size());
  a.put32(8, tmtab);
  for (int t = 0; t < 64; ++t) a.u8(0);  // every track on MIDI channel 0
  a.align(4);
  const std::uint32_t ptab = static_cast<std::uint32_t>(a.size());
  a.put32(4, ptab);
  a.u32(0);  // pattern 0's offset, patched below
  // Track 0: pattern 0 at time 0, then jump back to entry 0 at time 2400.
  const std::uint32_t track = static_cast<std::uint32_t>(a.size());
  a.put32(ttab, track);
  a.u32(0);
  a.u8(0);     // program change to 0
  a.u8(0xFF);  // no volume change
  a.u16(0);
  a.u16(0);  // pattern 0
  a.u8(0);
  a.u8(0);
  a.u32(2400);
  a.u32(0);
  a.u16(0xFFFE);  // jump
  a.u16(0);       // to entry 0
  // Pattern 0.
  const std::uint32_t pat = static_cast<std::uint32_t>(a.size());
  a.put32(ptab, pat);
  a.u32(8);  // header length
  a.u32(0);  // no pitch-bend stream
  a.u32(0);  // no modulation stream
  auto ctrl = [&a](std::uint16_t dt, std::uint8_t value) {
    a.u16(dt);
    a.u8(0x80 | value);
    a.u8(0x80 | kController);
  };
  auto note = [&a](std::uint16_t dt, std::uint8_t key, std::uint8_t vel, std::uint16_t len) {
    a.u16(dt);
    a.u8(key);
    a.u8(vel);
    a.u16(len);
  };
  ctrl(0, 64);
  note(0, 60, 100, 600);
  ctrl(800, 100);
  note(0, 64, 90, 400);
  note(800, 67, 80, 300);
  ctrl(0, 3);
  a.u16(600);
  a.u8(0xFF);
  a.u8(0xFF);
  a.align(4);
  d.song = a.b;
  return d;
}

}  // namespace mpt::gc::test::musyxdata
