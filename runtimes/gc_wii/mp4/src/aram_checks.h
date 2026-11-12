// MP4's per-step ARAM checks (ADR 0008, "Gate status after the implementation", finding 2, and "Gate
// status after the writer fixes", point 3), as pure functions so a test can drive them without a dump
// (tests/test_aram_checks.cpp). abi.cpp's step() faults the run (MPT_MP4_STEP_FAULTED) on any message.
#pragma once

#include <cstdint>
#include <string>

namespace mpt::mp4 {

// msm's ARAM region on PC: [ARGetBaseAddress(), HU_AMEM_BASE). msmSysInit only checks that msm's ARAM
// fits below HU_AMEM_BASE; our MusyX (MUSY_TARGET_PC) starts no DMA there and HuARMalloc hands out
// nothing below its floor, so the region stays as zero as the arena was at boot, and the state hash
// leaves it out. The SDK faults any DMA, either direction, that overlaps it (SdkConfig::aram_guards);
// a fault there means the exclusion no longer holds and goes back to architect.
inline constexpr char kMsmRegionLabel[] = "ARAM msm region";

struct AramRange {
  std::uint32_t begin = 0;  // inclusive
  std::uint32_t end = 0;    // exclusive
};

// The guarded range, from the SDK's ARGetBaseAddress() and HuARMalloc's floor (HU_AMEM_BASE).
AramRange msmRegion(std::uint32_t aram_base, std::uint32_t armem_floor);

// The alias check: HuARMalloc's in-use blocks (addr[i], size[i], i < n, in address order) lie in
// [floor, end), apart from each other, and the SDK's ARAlloc range [0, top) ends at or below floor.
// `max` is the capacity of the arrays the block list was read into. Empty when all of that holds;
// otherwise a message starting "ARAM alias check: ".
std::string aramAliasCheck(const std::uint32_t* addr, const std::uint32_t* size, std::int32_t n, std::int32_t max,
                           std::uint64_t floor, std::uint64_t top, std::uint64_t end);

}  // namespace mpt::mp4
