// CARD over the save image: round trips, stable serial, generation, errors
// (E10, ADR 0004). Synthetic file contents only.
#include <cstring>

#include "harness.h"
#include "mpt_gc/card.h"
#include "mpt_gc/os.h"

using namespace mpt;
using namespace mpt::gc;
using namespace mpt::gc::test;

namespace {

constexpr u32 kSector = 8192;
alignas(32) std::byte g_work[CARD_WORKAREA_SIZE];

SdkConfig cardConfig(std::optional<std::vector<std::byte>> image = std::nullopt) {
  SdkConfig cfg;
  cfg.card.game_code = {'T', 'E', 'S', 'T'};
  cfg.card.maker_code = {'9', '9'};
  cfg.card.image = std::move(image);
  return cfg;
}

std::vector<std::byte> fill(std::size_t n, unsigned seed) {
  std::vector<std::byte> v(n);
  for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::byte>((i * 7 + seed) & 0xFF);
  return v;
}

}  // namespace

TEST_CASE("a blank card is formatted, empty and has the stable serial") {
  u64 serial = 0;
  s32 bytes = 0, files = 0, mem = 0, sector = 0;
  Game g{[&] {
    CHECK(CARDProbeEx(CARD_SLOT_A, &mem, &sector) == CARD_RESULT_READY);
    CHECK(CARDProbeEx(CARD_SLOT_B, nullptr, nullptr) == CARD_RESULT_NOCARD);
    CHECK(CARDMount(CARD_SLOT_A, g_work, nullptr) == CARD_RESULT_READY);
    CHECK(CARDCheck(CARD_SLOT_A) == CARD_RESULT_READY);
    CHECK(CARDGetSerialNo(CARD_SLOT_A, &serial) == CARD_RESULT_READY);
    CHECK(CARDFreeBlocks(CARD_SLOT_A, &bytes, &files) == CARD_RESULT_READY);
  }};
  auto sdk = boot(g, cardConfig());
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(serial == kStableCardSerial);
  CHECK(mem == 16);
  CHECK(sector == static_cast<s32>(kSector));
  CHECK(bytes == static_cast<s32>(251 * kSector));
  CHECK(files == CARD_MAX_FILE);
  CHECK(sdk->saveMediaGeneration() == 0);
}

TEST_CASE("CARD round trip through the save image, with stable serial and generation") {
  const auto payload = fill(2 * kSector, 3);
  std::vector<std::byte> image;
  std::uint32_t saved_time = 0;
  std::int64_t now_at_create = 0;
  {
    Game g{[&] {
      static CARDFileInfo fi;
      CARDMount(CARD_SLOT_A, g_work, nullptr);
      now_at_create = OSGetTime();
      CHECK(CARDCreate(CARD_SLOT_A, "save_one", 2 * kSector, &fi) == CARD_RESULT_READY);
      CHECK(CARDWrite(&fi, payload.data(), 2 * kSector, 0) == CARD_RESULT_READY);
      CARDStat stat{};
      CHECK(CARDGetStatus(CARD_SLOT_A, fi.fileNo, &stat) == CARD_RESULT_READY);
      stat.iconAddr = 0;
      stat.bannerFormat = 1;
      stat.iconFormat = 0x0002;  // icon 0 RGB5A3
      stat.iconSpeed = 0x0003;
      stat.commentAddr = 0x1000;
      CHECK(CARDSetStatus(CARD_SLOT_A, fi.fileNo, &stat) == CARD_RESULT_READY);
      saved_time = stat.time;
      CARDClose(&fi);
    }};
    auto sdk = boot(g, cardConfig());
    CHECK(step(*sdk) == StepStatus::halted);
    CHECK(sdk->saveMediaGeneration() == 3);  // create, write, set status
    image = sdk->cardImage();
  }
  CHECK(saved_time == static_cast<std::uint32_t>(now_at_create / kTimerClockHz));
  CHECK(image.size() == 8 + 4 + 2 + 2 + 8 + 4 + (4 + 4 + 2 + 2 + 32 + 4 + 4 + 2 + 2 + 4 + 2) + 2 * kSector);

  // A second session (another process or machine) loads the image.
  std::vector<std::byte> back(2 * kSector);
  u64 serial = 0;
  CARDStat stat{};
  Game g2{[&] {
    static CARDFileInfo fi;
    CARDMount(CARD_SLOT_A, g_work, nullptr);
    CHECK(CARDGetSerialNo(CARD_SLOT_A, &serial) == CARD_RESULT_READY);
    CHECK(CARDOpen(CARD_SLOT_A, "save_one", &fi) == CARD_RESULT_READY);
    CHECK(fi.length == static_cast<s32>(2 * kSector));
    CHECK(CARDRead(&fi, back.data(), static_cast<s32>(2 * kSector), 0) == CARD_RESULT_READY);
    CHECK(CARDGetStatus(CARD_SLOT_A, fi.fileNo, &stat) == CARD_RESULT_READY);
  }};
  SdkConfig cfg = cardConfig(image);
  cfg.seed = 99;  // a different session seed does not change the card
  auto sdk2 = boot(g2, cfg);
  CHECK(step(*sdk2) == StepStatus::halted);
  CHECK(back == payload);
  CHECK(serial == kStableCardSerial);
  CHECK(stat.time == saved_time);
  CHECK(std::string(stat.fileName) == "save_one");
  CHECK(std::memcmp(stat.gameName, "TEST", 4) == 0);
  CHECK(stat.offsetBanner == 0);
  CHECK(stat.offsetBannerTlut == 96 * 32);
  CHECK(stat.offsetIcon[0] == 96 * 32 + 512);
  CHECK(stat.offsetIcon[1] == 0xFFFFFFFFu);
  CHECK(stat.offsetData == 96 * 32 + 512 + 32 * 32 * 2);
  CHECK(sdk2->saveMediaGeneration() == 0);  // reads do not commit
  CHECK(sdk2->cardImage() == image);        // the image survives unchanged
}

TEST_CASE("the same card operations give byte-identical images") {
  auto run = [] {
    Game g{[] {
      static CARDFileInfo fi;
      const auto data = fill(kSector, 9);
      CARDMount(CARD_SLOT_A, g_work, nullptr);
      CARDCreate(CARD_SLOT_A, "a", kSector, &fi);
      CARDWrite(&fi, data.data(), kSector, 0);
      CARDCreate(CARD_SLOT_A, "b", 3 * kSector, &fi);
      CARDDelete(CARD_SLOT_A, "a");
    }};
    auto sdk = boot(g, cardConfig());
    CHECK(step(*sdk) == StepStatus::halted);
    return sdk->cardImage();
  };
  CHECK(run() == run());
}

TEST_CASE("CARD errors") {
  Game g{[&] {
    static CARDFileInfo fi;
    static std::byte buf[kSector];
    CHECK(CARDOpen(CARD_SLOT_A, "x", &fi) == CARD_RESULT_NOCARD);  // not mounted
    CARDMount(CARD_SLOT_A, g_work, nullptr);
    CHECK(CARDMount(CARD_SLOT_B, g_work, nullptr) == CARD_RESULT_NOCARD);
    CHECK(CARDOpen(CARD_SLOT_A, "missing", &fi) == CARD_RESULT_NOFILE);
    CHECK(CARDGetResultCode(CARD_SLOT_A) == CARD_RESULT_NOFILE);
    CHECK(CARDCreate(CARD_SLOT_A, "123456789012345678901234567890123", kSector, &fi) == CARD_RESULT_NAMETOOLONG);
    CHECK(CARDCreate(CARD_SLOT_A, "12345678901234567890123456789012", kSector, &fi) == CARD_RESULT_READY);
    CHECK(CARDOpen(CARD_SLOT_A, "12345678901234567890123456789012", &fi) == CARD_RESULT_READY);
    CHECK(CARDCreate(CARD_SLOT_A, "f", kSector + 1, &fi) == CARD_RESULT_FATAL_ERROR);
    CHECK(CARDCreate(CARD_SLOT_A, "f", kSector, &fi) == CARD_RESULT_READY);
    CHECK(CARDCreate(CARD_SLOT_A, "f", kSector, &fi) == CARD_RESULT_EXIST);
    CHECK(CARDOpen(CARD_SLOT_A, "f", &fi) == CARD_RESULT_READY);
    CHECK(CARDWrite(&fi, buf, 512, 0) == CARD_RESULT_FATAL_ERROR);  // writes are whole sectors
    CHECK(CARDWrite(&fi, buf, kSector, kSector) == CARD_RESULT_LIMIT);
    CHECK(CARDRead(&fi, buf, 100, 0) == CARD_RESULT_FATAL_ERROR);   // reads are 512-byte units
    CHECK(CARDRead(&fi, buf, 512, 512) == CARD_RESULT_READY);
    CHECK(CARDCreate(CARD_SLOT_A, "huge", 300 * kSector, &fi) == CARD_RESULT_INSSPACE);
    CHECK(CARDRename(CARD_SLOT_A, "f", "g") == CARD_RESULT_READY);
    CHECK(CARDOpen(CARD_SLOT_A, "f", &fi) == CARD_RESULT_NOFILE);
    CHECK(CARDDelete(CARD_SLOT_A, "g") == CARD_RESULT_READY);
    CHECK(CARDDelete(CARD_SLOT_A, "g") == CARD_RESULT_NOFILE);
  }};
  auto sdk = boot(g, cardConfig());
  CHECK(step(*sdk) == StepStatus::halted);
}

TEST_CASE("CARD: fragmented free space is compacted, data preserved") {
  bool ok = false;
  Game g{[&] {
    static CARDFileInfo fi;
    CARDMount(CARD_SLOT_A, g_work, nullptr);
    const auto keep = fill(100 * kSector, 5);
    CHECK(CARDCreate(CARD_SLOT_A, "first", 100 * kSector, &fi) == CARD_RESULT_READY);
    CHECK(CARDCreate(CARD_SLOT_A, "keep", 100 * kSector, &fi) == CARD_RESULT_READY);
    CHECK(CARDWrite(&fi, keep.data(), static_cast<s32>(keep.size()), 0) == CARD_RESULT_READY);
    CHECK(CARDDelete(CARD_SLOT_A, "first") == CARD_RESULT_READY);
    // 151 blocks free, split 100 + 51: a 120-block file needs compaction.
    CHECK(CARDCreate(CARD_SLOT_A, "big", 120 * kSector, &fi) == CARD_RESULT_READY);
    CHECK(CARDOpen(CARD_SLOT_A, "keep", &fi) == CARD_RESULT_READY);
    std::vector<std::byte> back(keep.size());
    CHECK(CARDRead(&fi, back.data(), static_cast<s32>(back.size()), 0) == CARD_RESULT_READY);
    ok = back == keep;
  }};
  auto sdk = boot(g, cardConfig());
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(ok);
}

namespace {
s32 g_card_cb_result = 99;
s32 g_card_cb_chan = 99;
void onCard(s32 chan, s32 result) {
  g_card_cb_chan = chan;
  g_card_cb_result = result;
}
}  // namespace

TEST_CASE("async CARD calls complete synchronously with the callback at the next boundary") {
  s32 before = 0;
  Game g{[&] {
    static CARDFileInfo fi;
    g_card_cb_result = 99;
    CHECK(CARDMountAsync(CARD_SLOT_A, g_work, nullptr, &onCard) == CARD_RESULT_READY);
    before = g_card_cb_result;
    CHECK(CARDCreateAsync(CARD_SLOT_A, "async", kSector, &fi, &onCard) == CARD_RESULT_READY);
    CHECK(g_card_cb_result == CARD_RESULT_READY);  // the mount callback ran at that boundary
  }};
  auto sdk = boot(g, cardConfig());
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(before == 99);
  CHECK(g_card_cb_chan == CARD_SLOT_A);
  CHECK(sdk->saveMediaGeneration() == 1);
}

TEST_CASE("format keeps the stable serial; a card can be absent") {
  Game g{[&] {
    static CARDFileInfo fi;
    CARDMount(CARD_SLOT_A, g_work, nullptr);
    CARDCreate(CARD_SLOT_A, "x", kSector, &fi);
    CHECK(CARDFormat(CARD_SLOT_A) == CARD_RESULT_READY);
    u64 serial = 0;
    CARDGetSerialNo(CARD_SLOT_A, &serial);
    CHECK(serial == kStableCardSerial);
    CHECK(CARDOpen(CARD_SLOT_A, "x", &fi) == CARD_RESULT_NOFILE);
  }};
  auto sdk = boot(g, cardConfig());
  CHECK(step(*sdk) == StepStatus::halted);

  Game none{[] { CHECK(CARDMount(CARD_SLOT_A, g_work, nullptr) == CARD_RESULT_NOCARD); }};
  sdk.reset();
  SdkConfig cfg = cardConfig();
  cfg.card.inserted = false;
  auto sdk2 = boot(none, cfg);
  CHECK(step(*sdk2) == StepStatus::halted);
}

TEST_CASE("malformed card images are rejected at boot") {
  Game g{[] {}};
  std::vector<std::byte> good;
  {
    Game make{[] {
      static CARDFileInfo fi;
      CARDMount(CARD_SLOT_A, g_work, nullptr);
      CARDCreate(CARD_SLOT_A, "x", kSector, &fi);
    }};
    auto sdk = boot(make, cardConfig());
    CHECK(step(*sdk) == StepStatus::halted);
    good = sdk->cardImage();
  }
  auto tryImage = [&](std::vector<std::byte> img) {
    std::unique_ptr<Sdk> sdk;
    return Sdk::create(cardConfig(std::move(img)), &gameMain, &g, sdk).ok();
  };
  CHECK(tryImage(good));
  auto bad = good;
  bad[0] = std::byte{'X'};
  CHECK_FALSE(tryImage(bad));
  bad = good;
  bad.push_back(std::byte{0});
  CHECK_FALSE(tryImage(bad));
  bad = good;
  bad.pop_back();
  CHECK_FALSE(tryImage(bad));
  bad = good;
  bad[8] = std::byte{2};  // format version
  CHECK_FALSE(tryImage(bad));
  CHECK_FALSE(tryImage({}));
}
