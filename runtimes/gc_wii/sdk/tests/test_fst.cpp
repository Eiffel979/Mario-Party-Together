// DirectoryFileSource over an extracted disc in asset-import's layout (P1-09):
// sys/fst.bin + files/. The disc is synthetic and generated at test time under
// the build directory (never committed). Entry order must come from the FST.
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "harness.h"
#include "mpt/gc/file_source.h"
#include "mpt_gc/dvd.h"

using namespace mpt;
using namespace mpt::gc;
using namespace mpt::gc::test;
namespace fs = std::filesystem;

namespace {

struct FstNode {
  std::string name;
  bool dir = false;
  std::uint32_t size = 0;      // files
  std::uint32_t children = 0;  // dirs: number of entries inside, recursively
};

void put32(std::vector<std::byte>& v, std::size_t at, std::uint32_t x) {
  for (int i = 0; i < 4; ++i) v[at + static_cast<std::size_t>(i)] = static_cast<std::byte>(x >> (24 - 8 * i));
}

// Builds a big-endian FST from entries listed in FST order (root excluded).
std::vector<std::byte> makeFst(const std::vector<FstNode>& nodes) {
  const auto count = static_cast<std::uint32_t>(nodes.size() + 1);
  std::vector<std::byte> fst(std::size_t{count} * 12);
  std::string strings;
  fst[0] = std::byte{1};
  put32(fst, 8, count);
  std::vector<std::uint32_t> parents{0};
  std::vector<std::uint32_t> ends{count};
  for (std::uint32_t i = 1; i < count; ++i) {
    while (i >= ends.back()) {
      ends.pop_back();
      parents.pop_back();
    }
    const FstNode& n = nodes[i - 1];
    const std::size_t at = std::size_t{i} * 12;
    put32(fst, at, static_cast<std::uint32_t>(strings.size()));
    fst[at] = std::byte{n.dir ? std::uint8_t{1} : std::uint8_t{0}};
    strings += n.name;
    strings.push_back('\0');
    if (n.dir) {
      put32(fst, at + 4, parents.back());
      put32(fst, at + 8, i + 1 + n.children);
      parents.push_back(i);
      ends.push_back(i + 1 + n.children);
    } else {
      put32(fst, at + 4, 0x1000 * i);  // disc offset: unused by the reader
      put32(fst, at + 8, n.size);
    }
  }
  for (char c : strings) fst.push_back(static_cast<std::byte>(c));
  return fst;
}

void writeFile(const fs::path& p, const std::vector<std::byte>& data) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

std::vector<std::byte> content(std::size_t n, unsigned seed) {
  std::vector<std::byte> v(n);
  for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::byte>((i * 13 + seed) & 0xFF);
  return v;
}

// A disc whose FST order is deliberately not sorted: "zeta" before "alpha".
fs::path makeDisc(const std::string& name, const std::vector<FstNode>& nodes, bool write_files = true) {
  const fs::path root = fs::path(MPT_GC_TEST_TMP) / name;
  std::error_code ec;
  fs::remove_all(root, ec);
  writeFile(root / "sys" / "fst.bin", makeFst(nodes));
  if (!write_files) return root;
  std::vector<std::string> dir_stack;
  std::vector<std::uint32_t> remaining;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    while (!remaining.empty() && remaining.back() == 0) {
      remaining.pop_back();
      dir_stack.pop_back();
    }
    if (!remaining.empty()) --remaining.back();
    std::string path;
    for (const auto& d : dir_stack) path += d + "/";
    path += nodes[i].name;
    if (nodes[i].dir) {
      fs::create_directories(root / "files" / path);
      dir_stack.push_back(nodes[i].name);
      remaining.push_back(nodes[i].children);
    } else {
      writeFile(root / "files" / path, content(nodes[i].size, static_cast<unsigned>(i)));
    }
  }
  return root;
}

const std::vector<FstNode> kDisc = {
    {"zeta.bin", false, 64, 0},
    {"data", true, 0, 2},
    {"b.bin", false, 5000, 0},
    {"a.bin", false, 10, 0},
    {"alpha.bin", false, 3, 0},
};

}  // namespace

TEST_CASE("DirectoryFileSource takes entry order and sizes from sys/fst.bin") {
  std::string error;
  auto src = DirectoryFileSource::open(makeDisc("fst_order", kDisc), error);
  REQUIRE_MESSAGE(src, error);
  const auto e = src->entries();
  REQUIRE(e.size() == 6);
  CHECK(e[1].path == "zeta.bin");
  CHECK(e[2].path == "data");
  CHECK(e[2].is_dir);
  CHECK(e[3].path == "data/b.bin");
  CHECK(e[3].size == 5000);
  CHECK(e[4].path == "data/a.bin");
  CHECK(e[5].path == "alpha.bin");

  std::vector<s32> nums;
  bool same = true;
  Game g{[&] {
    nums.push_back(DVDConvertPathToEntrynum("/zeta.bin"));
    nums.push_back(DVDConvertPathToEntrynum("/DATA/A.BIN"));
    nums.push_back(DVDConvertPathToEntrynum("alpha.bin"));
    static DVDFileInfo fi;
    static std::byte buf[5000];
    const auto expect = content(5000, 2);
    CHECK(DVDOpen("data/b.bin", &fi));
    // Alternate between files so cached handles are reused and switched.
    for (int round = 0; round < 3; ++round) {
      CHECK(DVDRead(&fi, buf, 5000, 0) == 5000);
      same = same && std::memcmp(buf, expect.data(), 5000) == 0;
      static DVDFileInfo other;
      CHECK(DVDFastOpen(1 + round % 2 * 3, &other));
      CHECK(DVDRead(&other, buf, 3, 0) == 3);
    }
  }};
  SdkConfig cfg;
  cfg.files = src.get();
  auto sdk = boot(g, cfg);
  CHECK(step(*sdk) == StepStatus::halted);
  CHECK(nums == std::vector<s32>{1, 4, 5});
  CHECK(same);
}

TEST_CASE("DirectoryFileSource rejects unsafe or inconsistent discs") {
  std::string error;
  CHECK_FALSE(DirectoryFileSource::open(makeDisc("fst_dotdot", {{"..", true, 0, 0}}, false), error));
  CHECK(error.find("..") != std::string::npos);
  CHECK_FALSE(DirectoryFileSource::open(makeDisc("fst_dot", {{".", false, 1, 0}}, false), error));
  CHECK_FALSE(DirectoryFileSource::open(makeDisc("fst_slash", {{"a/b", false, 1, 0}}, false), error));
  CHECK_FALSE(DirectoryFileSource::open(makeDisc("fst_bslash", {{"a\\b", false, 1, 0}}, false), error));
  CHECK_FALSE(DirectoryFileSource::open(makeDisc("fst_missing", kDisc, false), error));
  CHECK(error.find("missing") != std::string::npos);

  // Wrong size on disk.
  const fs::path root = makeDisc("fst_size", kDisc);
  writeFile(root / "files" / "zeta.bin", content(63, 0));
  CHECK_FALSE(DirectoryFileSource::open(root, error));

  // A directory extent past its parent's.
  auto fst = makeFst(kDisc);
  put32(fst, 2 * 12 + 8, 99);
  CHECK_FALSE(parse_fst(fst, error));
  CHECK_FALSE(parse_fst({}, error));
  CHECK_FALSE(DirectoryFileSource::open(fs::path(MPT_GC_TEST_TMP) / "no_such_disc", error));
}
