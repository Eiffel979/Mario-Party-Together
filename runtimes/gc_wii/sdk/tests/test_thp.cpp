// THP decoding (E10, ADR 0007 §4): a synthetic movie generated here at test
// time, decoded by the SDK, deterministically, inside the frame step.
#include <cstdlib>
#include <cstring>

#include "harness.h"
#include "mpt_gc/ai.h"
#include "mpt_gc/os.h"
#include "mpt_gc/thp.h"
#include "mpt_gc/vi.h"
#include "thp_encoder.h"

using namespace mpt;
using namespace mpt::gc;
using namespace mpt::gc::test;
namespace enc = mpt::gc::test::thpenc;

namespace {

struct Decoded {
  std::vector<std::uint8_t> y, u, v;  // I8 tiles
};

Decoded decode(const std::vector<std::uint8_t>& jpeg, std::uint32_t w, std::uint32_t h, std::int32_t* rc = nullptr) {
  Decoded d{std::vector<std::uint8_t>(w * h), std::vector<std::uint8_t>(w * h / 4), std::vector<std::uint8_t>(w * h / 4)};
  std::uint32_t dw = 0, dh = 0;
  const std::int32_t r = thp::decodeVideo(jpeg, d.y.data(), d.u.data(), d.v.data(), &dw, &dh);
  if (rc) *rc = r;
  if (r == THP_OK) {
    CHECK(dw == w);
    CHECK(dh == h);
  }
  return d;
}

std::uint64_t mse(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
  std::uint64_t s = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const std::int64_t d = std::int64_t{a[i]} - b[i];
    s += static_cast<std::uint64_t>(d * d);
  }
  return s / a.size();
}

std::uint64_t hashBytes(const std::vector<std::uint8_t>& a) {
  StateHasher h;
  for (std::uint8_t b : a) h.update_u64(b);
  return h.digest();
}

}  // namespace

TEST_CASE("THP video: a synthetic JPEG decodes close to its source") {
  for (std::uint32_t f = 0; f < 4; ++f) {
    const enc::Planes p = enc::picture(64, 48, f);
    std::int32_t rc = -1;
    const Decoded d = decode(enc::encodeJpeg(p), 64, 48, &rc);
    REQUIRE(rc == THP_OK);
    CHECK(mse(enc::untile(d.y.data(), 64, 48), p.y) < 30);
    CHECK(mse(enc::untile(d.u.data(), 32, 24), p.u) < 30);
    CHECK(mse(enc::untile(d.v.data(), 32, 24), p.v) < 30);
  }
}

TEST_CASE("THP video: restart intervals and extra segments decode identically") {
  const enc::Planes p = enc::picture(80, 32, 2);
  const Decoded plain = decode(enc::encodeJpeg(p), 80, 32);
  for (std::uint16_t ri : {1, 2, 3, 7}) {
    std::int32_t rc = -1;
    const Decoded rst = decode(enc::encodeJpeg(p, {ri, 0x22, true}), 80, 32, &rc);
    REQUIRE(rc == THP_OK);
    CHECK(rst.y == plain.y);
    CHECK(rst.u == plain.u);
    CHECK(rst.v == plain.v);
  }
}

TEST_CASE("THP video: golden output (integer IDCT, identical on every platform)") {
  const Decoded d = decode(enc::encodeJpeg(enc::picture(64, 48, 3), {2}), 64, 48);
  StateHasher h;
  h.update_u64(hashBytes(d.y));
  h.update_u64(hashBytes(d.u));
  h.update_u64(hashBytes(d.v));
  CHECK(h.digest() == 18305061132633912001ull);  // golden
}

TEST_CASE("THP video: malformed and unsupported streams fail cleanly") {
  const enc::Planes p = enc::picture(64, 48, 1);
  const auto good = enc::encodeJpeg(p);
  std::int32_t rc = 0;
  for (std::size_t cut : {std::size_t{0}, std::size_t{3}, std::size_t{40}, good.size() / 2, good.size() - 3}) {
    std::vector<std::uint8_t> bad(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut));
    decode(bad, 64, 48, &rc);
    CHECK(rc == THP_ERROR_BAD_STREAM);
  }
  decode(enc::encodeJpeg(p, {0, 0x11}), 64, 48, &rc);  // 4:4:4 luma sampling
  CHECK(rc == THP_ERROR_UNSUPPORTED);
  auto bad = enc::encodeJpeg(enc::picture(64, 48, 1));
  bad[0] = 0x00;
  decode(bad, 64, 48, &rc);
  CHECK(rc == THP_ERROR_BAD_STREAM);
  // Restart marker out of sequence.
  auto rst = enc::encodeJpeg(p, {1});
  for (std::size_t i = 0; i + 1 < rst.size(); ++i)
    if (rst[i] == 0xFF && rst[i + 1] == 0xD1) {
      rst[i + 1] = 0xD5;
      break;
    }
  decode(rst, 64, 48, &rc);
  CHECK(rc == THP_ERROR_BAD_STREAM);
}

TEST_CASE("THP audio: DSP-ADPCM decodes exactly, in both layouts") {
  const auto lp = enc::tone(0, 533, 73, 9000), rp = enc::tone(5, 533, 41, -5000);
  const auto l = enc::encodeAdpcm(lp, 0, 0), r = enc::encodeAdpcm(rp, 100, -50);
  const auto frame = enc::audioFrame(l, r, 533);
  std::vector<std::int16_t> inter(533 * 2), sep(533 * 2);
  REQUIRE(thp::decodeAudio(frame, inter.data(), THP_AUDIO_INTERLEAVE) == 533);
  REQUIRE(thp::decodeAudio(frame, sep.data(), THP_AUDIO_NOINTERLEAVE) == 533);
  std::int64_t err = 0;
  for (std::size_t i = 0; i < 533; ++i) {
    CHECK(inter[2 * i] == l.decoded[i]);
    CHECK(inter[2 * i + 1] == r.decoded[i]);
    CHECK(sep[i] == l.decoded[i]);
    CHECK(sep[533 + i] == r.decoded[i]);
    err += std::abs(l.decoded[i] - lp[i]);
  }
  CHECK(err / 533 < 400);  // the encoder is a real (if slow) ADPCM encoder
  auto truncated = frame;
  truncated.resize(100);
  CHECK(thp::decodeAudio(truncated, inter.data(), THP_AUDIO_INTERLEAVE) == 0);
  StateHasher h;
  for (std::int16_t s : inter) h.update_u64(static_cast<std::uint16_t>(s));
  CHECK(h.digest() == 2999549158242287633ull);  // golden
}

TEST_CASE("THP container: header, components and the frame chain") {
  const enc::Movie m = enc::movie(64, 48, 6, 534);
  const std::span<const std::uint8_t> file(m.file);
  thp::Info info;
  REQUIRE(thp::parse(file, info).ok());
  CHECK(info.version == thp::kVersion11);
  CHECK(info.num_frames == 6);
  CHECK(info.frame_rate_bits == std::bit_cast<std::uint32_t>(29.97f));
  CHECK(info.has_video);
  CHECK(info.width == 64);
  CHECK(info.height == 48);
  CHECK(info.has_audio);
  CHECK(info.channels == 2);
  CHECK(info.frequency == 32000);
  CHECK(info.num_tracks == 1);
  std::uint64_t off = info.movie_data_offset;
  std::uint32_t size = info.first_frame_size;
  std::vector<std::int16_t> audio;
  for (std::uint32_t f = 0; f < info.num_frames; ++f) {
    thp::Frame fr;
    REQUIRE(thp::frameAt(file, info, off, size, fr).ok());
    if (f + 1 == info.num_frames) CHECK(off == info.final_frame_offset);
    CHECK(std::vector<std::uint8_t>(fr.video.begin(), fr.video.end()) == m.jpegs[f]);
    REQUIRE(fr.audio_tracks == 1);
    std::vector<std::int16_t> buf(534 * 2);
    REQUIRE(thp::decodeAudio(fr.audio[0], buf.data(), THP_AUDIO_NOINTERLEAVE) == 534);
    audio.insert(audio.end(), buf.begin(), buf.begin() + 534);
    off += size;
    size = fr.next_size;
  }
  CHECK(audio == m.left);
  CHECK(size == info.first_frame_size);  // the chain loops

  auto bad = m.file;
  bad[1] = 'X';
  CHECK_FALSE(thp::parse(bad, info).ok());
  bad = m.file;
  bad[43] = 0xF0;  // movie data offset past the end
  CHECK_FALSE(thp::parse(bad, info).ok());
  bad = m.file;
  bad.resize(60);
  CHECK_FALSE(thp::parse(bad, info).ok());
  REQUIRE(thp::parse(file, info).ok());
  thp::Frame fr;
  CHECK_FALSE(thp::frameAt(file, info, m.file.size() - 8, 64, fr).ok());
}

// ---- Playback inside the frame step --------------------------------------------------------

namespace {

// A tiny movie player in "game code", shaped like MP4's: the OS idle function
// decodes the next frame each frame step; the AI DMA callback feeds movie
// audio to the output. Everything it owns lives in the arena.
struct Player {
  std::uint8_t* file;
  std::uint32_t file_size;
  std::uint32_t offset, size;
  std::uint32_t decoded;     // video frames decoded (playback progress, game-visible)
  std::int32_t last_rc;
  std::uint32_t audio_read, audio_write;  // stereo frames in the FIFO
  std::uint32_t silent_blocks;            // AI blocks played before movie audio started
  std::uint32_t y_hash;
  alignas(32) std::uint8_t y[64 * 48];
  alignas(32) std::uint8_t u[32 * 24];
  alignas(32) std::uint8_t v[32 * 24];
  alignas(32) std::int16_t fifo[8192 * 2];
  alignas(32) std::int16_t block[2][160 * 2];
  std::uint32_t block_index;
};
Player* g_player = nullptr;
const enc::Movie* g_movie = nullptr;

std::uint32_t be32at(const std::uint8_t* p) {
  return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
}

void decodeNext(void*) {
  Player& pl = *g_player;
  if (pl.decoded >= 6) return;
  const std::uint8_t* fr = pl.file + pl.offset;
  const std::uint32_t video_size = be32at(fr + 8);
  pl.last_rc = THPVideoDecode(const_cast<std::uint8_t*>(fr + 16), pl.y, pl.u, pl.v, nullptr);
  std::int16_t pcm[1024 * 2];
  const u32 n = THPAudioDecode(pcm, const_cast<std::uint8_t*>(fr + 16 + video_size), THP_AUDIO_INTERLEAVE);
  for (u32 i = 0; i < n; ++i) {
    const std::uint32_t w = pl.audio_write++ % 8192;
    pl.fifo[2 * w] = pcm[2 * i];
    pl.fifo[2 * w + 1] = pcm[2 * i + 1];
  }
  std::uint32_t hsh = 0;
  for (std::uint8_t b : pl.y) hsh = hsh * 31u + b;
  pl.y_hash = hsh;
  ++pl.decoded;
  const std::uint32_t next = be32at(fr);
  pl.offset += pl.size;
  pl.size = next;
}

void audioCallback() {
  Player& pl = *g_player;
  std::int16_t* b = pl.block[pl.block_index ^= 1];
  if (pl.audio_write - pl.audio_read < 160) {
    std::memset(b, 0, 160 * 2 * sizeof(std::int16_t));
    if (pl.audio_read == 0) ++pl.silent_blocks;
  } else {
    for (std::uint32_t i = 0; i < 160; ++i) {
      const std::uint32_t r = pl.audio_read++ % 8192;
      b[2 * i] = pl.fifo[2 * r];
      b[2 * i + 1] = pl.fifo[2 * r + 1];
    }
  }
  AIInitDMA(reinterpret_cast<uintptr_t>(b), 160 * 2 * sizeof(std::int16_t));
}

void playerMain(void*) {
  Player& pl = *static_cast<Player*>(OSAllocFromArenaLo(sizeof(Player), 32));
  std::memset(&pl, 0, sizeof pl);
  g_player = &pl;
  pl.file_size = static_cast<std::uint32_t>(g_movie->file.size());
  pl.file = static_cast<std::uint8_t*>(OSAllocFromArenaLo(pl.file_size, 32));
  std::memcpy(pl.file, g_movie->file.data(), pl.file_size);  // as if read from the disc
  pl.offset = be32at(pl.file + 40);
  pl.size = be32at(pl.file + 24);
  pl.last_rc = -1;
  CHECK(THPVideoDecode(pl.file, pl.y, pl.u, pl.v, nullptr) == THP_ERROR_NOT_INITIALIZED);
  THPInit();
  AIInit(nullptr);
  AIRegisterDMACallback(&audioCallback);
  AIInitDMA(reinterpret_cast<uintptr_t>(pl.block[0]), sizeof pl.block[0]);
  AIStartDMA();
  OSSetIdleFunction(&decodeNext, nullptr, nullptr, 0);
  for (;;) VIWaitForRetrace();
}

std::vector<std::uint64_t> playTrail(Sdk& sdk, int frames) {
  std::vector<std::uint64_t> out;
  for (int i = 0; i < frames; ++i) {
    REQUIRE(step(sdk) == StepStatus::running);
    StateHasher h;
    sdk.hashState(h);
    h.update_u64(g_player->decoded);
    h.update_u64(g_player->y_hash);
    h.update_u64(g_player->audio_read | (std::uint64_t{g_player->audio_write} << 32));
    out.push_back(h.digest());
  }
  return out;
}

}  // namespace

TEST_CASE("THP playback runs inside the frame step, with movie audio in the AI stream") {
  const enc::Movie m = enc::movie(64, 48, 6, 534);
  g_movie = &m;
  std::unique_ptr<Sdk> sdk;
  REQUIRE(Sdk::create(SdkConfig{}, &playerMain, nullptr, sdk).ok());
  std::vector<std::int16_t> pcm;
  for (int f = 0; f < 10; ++f) {
    REQUIRE(step(*sdk) == StepStatus::running);
    // One frame decoded per step, from the step after the idle function was set.
    CHECK(g_player->decoded == static_cast<std::uint32_t>(std::min(f, 6)));
    auto a = sdk->takeAudio();
    pcm.insert(pcm.end(), a.begin(), a.end());
  }
  CHECK(g_player->last_rc == THP_OK);
  // The last decoded picture is the movie's last frame.
  const Decoded ref = decode(m.jpegs[5], 64, 48);
  CHECK(std::memcmp(g_player->y, ref.y.data(), ref.y.size()) == 0);
  // Movie audio reaches the output, in order, after the silent lead-in: the
  // first block (set up before DMA started) plus the blocks the callback
  // filled with silence. A callback's block plays at the following tick.
  const std::size_t lead = (std::size_t{g_player->silent_blocks} + 1) * 160 * 2;
  REQUIRE(pcm.size() > lead + 1000);
  for (std::size_t i = 0; i < 500; ++i) {
    CHECK(pcm[lead + 2 * i] == m.left[i]);
    CHECK(pcm[lead + 2 * i + 1] == m.right[i]);
  }
}

TEST_CASE("THP playback is deterministic and survives save states") {
  const enc::Movie m = enc::movie(64, 48, 6, 534);
  g_movie = &m;
  std::vector<std::uint64_t> a, b;
  {
    std::unique_ptr<Sdk> sdk;
    REQUIRE(Sdk::create(SdkConfig{}, &playerMain, nullptr, sdk).ok());
    a = playTrail(*sdk, 12);
  }
  {
    std::unique_ptr<Sdk> sdk;
    SdkConfig cfg;
    cfg.audio_output = false;  // null audio: the same game-visible state
    REQUIRE(Sdk::create(cfg, &playerMain, nullptr, sdk).ok());
    b = playTrail(*sdk, 4);
    const auto state = sdk->saveState();
    const auto rest1 = playTrail(*sdk, 8);
    REQUIRE(sdk->loadState(state).ok());
    const auto rest2 = playTrail(*sdk, 8);
    CHECK(rest1 == rest2);
    b.insert(b.end(), rest2.begin(), rest2.end());
  }
  CHECK(a == b);
}
