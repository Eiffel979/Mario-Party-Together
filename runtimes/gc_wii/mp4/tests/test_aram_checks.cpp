// MP4's per-step ARAM checks (src/aram_checks.h) on synthetic block lists, no dump:
//  - the alias check (ADR 0008, "Gate status after the implementation", finding 2; qa's #77 follow-up):
//    HuARMalloc blocks that overlap each other, start below HU_AMEM_BASE, run past ARAM's end, or sit
//    under an ARAlloc range that grew past the floor give a message, which abi.cpp's step() turns into
//    MPT_MP4_STEP_FAULTED; a valid list gives none;
//  - msm's guarded region ("Gate status after the writer fixes", point 3) is [ARGetBaseAddress(),
//    HU_AMEM_BASE). The SDK's DMA guard itself is tested in sdk/tests/test_dvd_ar.cpp, both directions.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>

#include "aram_checks.h"

namespace {

int g_failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

constexpr std::uint32_t kReserved = 0x4000;    // AR_RESERVED_BYTES, what ARGetBaseAddress() returns
constexpr std::uint32_t kFloor = 0x808000;     // HU_AMEM_BASE
constexpr std::uint64_t kEnd = 16u << 20;      // ARAM's size
constexpr std::int32_t kMax = 8;

bool faults(const std::string& message) { return message.rfind("ARAM alias check: ", 0) == 0; }

std::string check(std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> blocks, std::uint64_t top = kReserved,
                  std::int32_t n_override = -2) {
  std::uint32_t addr[kMax] = {}, size[kMax] = {};
  std::int32_t n = 0;
  for (const auto& [a, s] : blocks) {
    addr[n] = a;
    size[n] = s;
    ++n;
  }
  if (n_override != -2) n = n_override;
  return mpt::mp4::aramAliasCheck(addr, size, n, kMax, kFloor, top, kEnd);
}

}  // namespace

int main() {
  // Valid: nothing allocated; blocks from the floor up, adjacent, the last one ending at ARAM's end.
  CHECK(check({}).empty());
  CHECK(check({{kFloor, 0x400}, {kFloor + 0x400, 0x40}, {kFloor + 0x1000, 0x100}}).empty());
  CHECK(check({{kFloor, 0x100}, {static_cast<std::uint32_t>(kEnd - 0x200), 0x200}}).empty());
  CHECK(check({{kFloor, 0x100}}, kFloor).empty());  // ARAlloc's range may end exactly at the floor

  // Below the floor: a block in msm's region (the pre-0030 TARGET_PC base 0x20 was in the SDK's own).
  CHECK(faults(check({{0x20, 0x100}})));
  CHECK(faults(check({{kFloor - 0x20, 0x40}})));  // straddles the floor
  // Overlapping blocks, and a list out of address order.
  CHECK(faults(check({{kFloor, 0x400}, {kFloor + 0x3E0, 0x40}})));
  CHECK(faults(check({{kFloor + 0x400, 0x40}, {kFloor, 0x40}})));
  // Past ARAM's end.
  CHECK(faults(check({{static_cast<std::uint32_t>(kEnd - 0x100), 0x200}})));
  // The SDK's ARAlloc range grew past HuARMalloc's floor.
  CHECK(faults(check({{kFloor, 0x100}}, kFloor + 1)));
  // A broken list: a negative count, or more blocks than the arrays hold.
  CHECK(faults(check({}, kReserved, -1)));
  CHECK(faults(check({}, kReserved, kMax + 1)));

  // msm's region: from the SDK's ARAM base up to HuARMalloc's floor.
  const mpt::mp4::AramRange msm = mpt::mp4::msmRegion(kReserved, kFloor);
  CHECK(msm.begin == kReserved && msm.end == kFloor);
  CHECK(std::strcmp(mpt::mp4::kMsmRegionLabel, "ARAM msm region") == 0);

  std::printf("mp4_aram_checks: %d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
