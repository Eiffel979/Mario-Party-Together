#include "aram_checks.h"

namespace mpt::mp4 {

AramRange msmRegion(std::uint32_t aram_base, std::uint32_t armem_floor) { return {aram_base, armem_floor}; }

std::string aramAliasCheck(const std::uint32_t* addr, const std::uint32_t* size, std::int32_t n, std::int32_t max,
                           std::uint64_t floor, std::uint64_t top, std::uint64_t end) {
  if (n < 0 || n > max) return "ARAM alias check: HuARMalloc's block list is broken";
  if (top > floor)
    return "ARAM alias check: the SDK's ARAlloc range ends at " + std::to_string(top) +
           ", above HuARMalloc's floor " + std::to_string(floor);
  std::uint64_t prev_end = floor;
  for (std::int32_t i = 0; i < n; ++i) {
    const std::uint64_t a = addr[i], e = a + size[i];
    if (a < prev_end || e > end)
      return "ARAM alias check: HuARMalloc block [" + std::to_string(a) + ", " + std::to_string(e) +
             ") overlaps msm's ARAM, the SDK's ARAlloc range or another block";
    prev_end = e;
  }
  return {};
}

}  // namespace mpt::mp4
