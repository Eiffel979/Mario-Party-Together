// Loading the MP4 game module (ADR 0008 section 11). The module is linked at a fixed image base; a
// module that lands anywhere else is refused, because function pointers in save states would be wrong.
// Each Mp4Runtime holds it from its first boot until it is destroyed; the last one unloads it. Unloading
// while the process still runs matters: the module's static teardown (aurora, Dawn's threads) fails if
// it is left to process exit, when the other threads are already gone.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "mpt/mp4/game_abi.h"

namespace mpt::mp4 {

// The module's API table, or nullptr with `error` set. Loading the same path again returns the same
// table; a different path while one is loaded fails.
const MptMp4Api* load_module(const std::filesystem::path& path, std::uintptr_t expected_base, std::string& error);
// Drops one hold taken by a successful load_module; the last one unloads the module. No game may be
// alive in it.
void release_module();

}  // namespace mpt::mp4
