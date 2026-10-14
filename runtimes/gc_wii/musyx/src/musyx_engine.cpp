// C++ side of the MusyX AudioEngine (see mpt/gc/musyx_engine.h).
#include "mpt/gc/musyx_engine.h"

#include <cstring>

#include "mpt/gc/sdk.h"
#include "musyx_glue.h"

namespace mpt::gc {
namespace {

struct Regions {
  std::span<std::byte> data, bss;
};

Regions regions() {
  unsigned char *dlo, *dhi, *blo, *bhi;
  mpt_musyx_state_bounds(&dlo, &dhi, &blo, &bhi);
  return {{reinterpret_cast<std::byte*>(dlo), static_cast<std::size_t>(dhi - dlo)},
          {reinterpret_cast<std::byte*>(blo), static_cast<std::size_t>(bhi - blo)}};
}

// The statics' image at process start. Taken before musyx runs at all, because
// every MusyxEngine entry point captures it first.
const std::vector<std::byte>& pristine() {
  static const std::vector<std::byte> image = [] {
    const Regions r = regions();
    std::vector<std::byte> v(r.data.begin(), r.data.end());
    v.insert(v.end(), r.bss.begin(), r.bss.end());
    return v;
  }();
  return image;
}

void render(void*, std::int16_t* out, std::uint32_t frames) { mpt_musyx_render(out, frames); }

void hashPut(void* self, std::uint64_t v) { static_cast<StateHasher*>(self)->update_u64(v); }
std::uint64_t hashPtr(void*, const void* p) { return hashGamePointer(p); }
std::uint64_t hashFn(void*, const void* f) { return hashGameFunction(f); }

void hash(void*, StateHasher& h) {
  const MptMusyxHasher mh{&h, &hashPut, &hashPtr, &hashFn};
  mpt_musyx_hash(&mh);
}

}  // namespace

AudioEngine MusyxEngine::engine() {
  (void)pristine();
  AudioEngine e;
  e.render = &render;
  e.hash = &hash;
  return e;
}

std::vector<std::span<std::byte>> MusyxEngine::stateRegions() {
  (void)pristine();
  const Regions r = regions();
  std::vector<std::span<std::byte>> out;
  if (!r.data.empty()) out.push_back(r.data);
  if (!r.bss.empty()) out.push_back(r.bss);
  return out;
}

void MusyxEngine::reset() {
  const std::vector<std::byte>& image = pristine();
  const Regions r = regions();
  std::memcpy(r.data.data(), image.data(), r.data.size());
  std::memcpy(r.bss.data(), image.data() + r.data.size(), r.bss.size());
  mpt_musyx_configure();
}

}  // namespace mpt::gc
