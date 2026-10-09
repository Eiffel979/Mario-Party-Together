#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mpt {

// A title's persistent save image (memory card, EEPROM, flash), owned by the host
// (ADR 0004). Runtimes see it only as bytes and never touch the filesystem for
// persistence: CARD/EEPROM/flash APIs are served from this in-memory image.
//
// The image is game state: it is carried in SaveState and covered by stateHash()
// wherever it can affect future frames. The initial image is a determinism input
// (ADR 0002 rule 2 as amended by ADR 0004 rule 3).
//
// Save images are user data that contain game-made content (icons, banners). They
// are never committed, never embedded in committed replays and never uploaded.
struct SaveMedia {
  std::uint32_t format_version = 0;  // runtime-defined layout version
  std::vector<std::byte> data{};       // opaque, runtime-defined (e.g. a memory card image)

  friend bool operator==(const SaveMedia&, const SaveMedia&) = default;
};

}  // namespace mpt
