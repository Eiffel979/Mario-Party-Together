// Loading the MP4 game module (ADR 0008 section 11). The module is linked at a fixed image base; a
// module that lands anywhere else is refused, because function pointers in save states would be wrong.
// It is loaded once and stays loaded: unloading code that the arena points into gains nothing.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "mpt/mp4/game_abi.h"

namespace mpt::mp4 {

// The module's API table, or nullptr with `error` set. Loading the same path again returns the same
// table; a different path while one is loaded fails.
const MptMp4Api* load_module(const std::filesystem::path& path, std::uintptr_t expected_base, std::string& error);

}  // namespace mpt::mp4
