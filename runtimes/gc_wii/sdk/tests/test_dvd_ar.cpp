// DVD over a synthetic in-memory file source, and ARAM/ARQ (E10, ADR 0007 §4).
#include <cstring>

#include "harness.h"
#include "mpt/gc/file_source.h"
#include "mpt_gc/ar.h"
#include "mpt_gc/dvd.h"
#include "mpt_gc/os.h"

using namespace mpt;
using namespace mpt::gc;
using namespace mpt::gc::test;

namespace {

std::vector<std::byte> pattern(std::size_t n, unsigned seed) {
  std::vector<std::byte> v(n);
  for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::byte>((i * 31 + seed) & 0xFF);
  return v;
}

MemoryFileSource makeDisc() {
  return MemoryFileSource({
      {"Z.dat", pattern(10, 4)},
      {"dir/sub/C.TXT", pattern(300, 3)},
      {"a.bin", pattern(100, 1)},
      {"dir/b.bin", pattern(4096, 2)},
  });
}

}  // namespace

TEST_CASE("entries are numbered in FST order, case-insensitively sorted per directory") {
  const MemoryFileSource disc = makeDisc();
  const auto e = disc.entries();
  REQUIRE(e.size() == 7);
  CHECK(e[0].path.empty());
  CHECK(e[1].path == "a.bin");
  CHECK(e[2].path == "dir");
  CHECK(e[3].path == "dir/b.bin");
  CHECK(e[4].path == "dir/sub");
  CHECK(e[5].path == "dir/sub/C.TXT");
  CHECK(e[6].path == "Z.dat");

  std::vector<s32> nums;
  Game g{[&] {
    for (const char* p : {"/a.bin", "dir", "/dir/b.bin", "/DIR/SUB/c.txt", "z.DAT", "/missing", "/dir/sub/../b.bin", "/"})
      nums.push_back(DVDConvertPathToEntrynum(p));
  }};
  SdkConfig cfg;
  cfg.files = &disc;
  auto sdk = boot(g, cfg);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(nums == std::vector<s32>{1, 2, 3, 5, 6, -1, 3, 0});
}

TEST_CASE("DVD reads are synchronous, offset-correct and zero past the end of file") {
  const MemoryFileSource disc = makeDisc();
  const auto expect = pattern(4096, 2);
  bool ok = true;
  Game g{[&] {
    static DVDFileInfo fi;
    static std::byte buf[512];
    CHECK(DVDOpen("dir/b.bin", &fi));
    CHECK(fi.length == 4096);
    CHECK(DVDRead(&fi, buf, 256, 1024) == 256);
    ok = ok && std::memcmp(buf, expect.data() + 1024, 256) == 0;
    CHECK(DVDGetFileInfoStatus(&fi) == DVD_STATE_END);
    std::memset(buf, 0xAA, sizeof buf);
    CHECK(DVDRead(&fi, buf, 512, 4096 - 64) == 512);  // rounded-up read past EOF
    ok = ok && std::memcmp(buf, expect.data() + 4096 - 64, 64) == 0;
    for (int i = 64; i < 512; ++i) ok = ok && buf[i] == std::byte{0};
    CHECK(DVDClose(&fi));
    CHECK_FALSE(DVDOpen("dir", &fi));  // a directory
    CHECK_FALSE(DVDOpen("nope.bin", &fi));
    CHECK(DVDFastOpen(5, &fi));
    CHECK(fi.length == 300);
    CHECK_FALSE(DVDFastOpen(4, &fi));
  }};
  SdkConfig cfg;
  cfg.files = &disc;
  auto sdk = boot(g, cfg);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(ok);
}

namespace {
int g_cb_calls = 0;
s32 g_cb_result = 0;
void onRead(s32 result, DVDFileInfo*) {
  ++g_cb_calls;
  g_cb_result = result;
}
}  // namespace

TEST_CASE("async DVD callbacks run at the next SDK call boundary by default") {
  const MemoryFileSource disc = makeDisc();
  int before = -1, after = -1;
  Game g{[&] {
    static DVDFileInfo fi;
    static std::byte buf[64];
    g_cb_calls = 0;
    DVDOpen("a.bin", &fi);
    CHECK(DVDReadAsync(&fi, buf, 64, 0, &onRead));
    before = g_cb_calls;  // the data is there, the callback has not run
    CHECK(buf[1] == std::byte{(31 + 1) & 0xFF});
    DVDGetCommandBlockStatus(&fi.cb);  // any SDK call
    after = g_cb_calls;
  }};
  SdkConfig cfg;
  cfg.files = &disc;
  auto sdk = boot(g, cfg);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(before == 0);
  CHECK(after == 1);
  CHECK(g_cb_result == 64);
}

TEST_CASE("async DVD callbacks can run before the call returns (per-title option)") {
  const MemoryFileSource disc = makeDisc();
  int before = -1;
  Game g{[&] {
    static DVDFileInfo fi;
    static std::byte buf[16];
    g_cb_calls = 0;
    DVDOpen("a.bin", &fi);
    DVDReadAsync(&fi, buf, 16, 0, &onRead);
    before = g_cb_calls;
  }};
  SdkConfig cfg;
  cfg.files = &disc;
  cfg.io_callbacks_before_return = true;
  auto sdk = boot(g, cfg);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(before == 1);
}

TEST_CASE("DVD directories: change, current and listing") {
  const MemoryFileSource disc = makeDisc();
  std::vector<std::string> listing;
  std::string cwd;
  s32 rel = -2;
  Game g{[&] {
    CHECK(DVDChangeDir("/dir"));
    rel = DVDConvertPathToEntrynum("sub/c.txt");
    char path[64];
    CHECK(DVDGetCurrentDir(path, sizeof path));
    cwd = path;
    static DVDDir d;
    static DVDDirEntry ent;
    CHECK(DVDOpenDir(".", &d));
    while (DVDReadDir(&d, &ent)) listing.push_back(std::string(ent.isDir ? "d:" : "f:") + ent.name);
    DVDCloseDir(&d);
    CHECK_FALSE(DVDChangeDir("b.bin"));
  }};
  SdkConfig cfg;
  cfg.files = &disc;
  auto sdk = boot(g, cfg);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(rel == 5);
  CHECK(cwd == "/dir");
  CHECK(listing == std::vector<std::string>{"f:b.bin", "d:sub"});
}

namespace {
int g_arq_calls = 0;
uintptr_t g_arq_req = 0;
void onArq(uintptr_t req) {
  ++g_arq_calls;
  g_arq_req = req;
}
}  // namespace

TEST_CASE("ARAM: stack allocation, synchronous DMA both ways, ARQ callbacks") {
  bool ok = true;
  u32 a1 = 0, a2 = 0, top = 0, freed = 0;
  Game g{[&] {
    static u32 stack[8];
    static std::byte src[256], dst[256];
    for (int i = 0; i < 256; ++i) src[i] = static_cast<std::byte>(i);
    CHECK(ARInit(stack, 8) == AR_RESERVED_BYTES);
    a1 = ARAlloc(0x1000);
    a2 = ARAlloc(0x200);
    ARStartDMA(ARAM_DIR_MRAM_TO_ARAM, reinterpret_cast<uintptr_t>(src), a2, 256);
    CHECK(ARGetDMAStatus() == 0);
    ARStartDMA(ARAM_DIR_ARAM_TO_MRAM, reinterpret_cast<uintptr_t>(dst), a2, 256);
    ok = std::memcmp(src, dst, 256) == 0;
    std::memset(dst, 0, sizeof dst);
    static ARQRequest req;
    g_arq_calls = 0;
    ARQInit();
    ARQPostRequest(&req, 7, ARQ_TYPE_ARAM_TO_MRAM, ARQ_PRIORITY_HIGH, a2, reinterpret_cast<uintptr_t>(dst), 128,
                   &onArq);
    ok = ok && std::memcmp(src, dst, 128) == 0 && dst[128] == std::byte{0};
    OSGetTick();  // boundary: the ARQ callback runs
    top = ARFree(&freed);
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(ok);
  CHECK(a1 == AR_RESERVED_BYTES);
  CHECK(a2 == AR_RESERVED_BYTES + 0x1000);
  CHECK(freed == 0x200);
  CHECK(top == a2);
  CHECK(g_arq_calls == 1);
  CHECK(g_arq_req != 0);
  // ARAM is part of the arena.
  CHECK(sdk->aram()[a2 + 5] == std::byte{5});
}

TEST_CASE("ARAM DMA outside ARAM is a fault") {
  Game g{[] {
    static std::byte b[64];
    ARInit(nullptr, 0);
    ARStartDMA(ARAM_DIR_MRAM_TO_ARAM, reinterpret_cast<uintptr_t>(b), 16u * 1024 * 1024 - 32, 64);
  }};
  auto sdk = boot(g);
  CHECK(step(*sdk) == StepStatus::faulted);
}
