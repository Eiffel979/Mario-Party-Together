// Loading the MP4 game module (ADR 0008 section 11). The module is linked at a fixed image base; a
// module that lands anywhere else is refused, because function pointers in save states would be wrong.
//
// Once loaded, the module stays loaded until the process shuts down (ADR 0008, "Game module kept loaded
// for the process"). A reload would give it a new /GS cookie, and a saved game frame that holds the old
// one fails fast when it returns, so an in-process save-state round trip (E5) needs the same load for
// the whole process. Each Mp4Runtime holds the module from its first boot until it is destroyed, but
// dropping the last hold does not unload it. The module resets its game state at every create, so a
// fresh runtime on the kept module starts as it would on a freshly loaded one.
//
// The shell unloads it at exit, through ShellTitle::shutdown (register.cpp calls shutdown_module), and
// not through static teardown at process exit: the module's teardown (aurora, Dawn's threads) fails
// once the other threads are gone.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "mpt/mp4/game_abi.h"

namespace mpt::mp4 {

// The module's API table, or nullptr with `error` set. `expected_build_id` (empty: any) must equal the
// module's build_id. While a module is loaded (pinned), loading it again returns the same table
// without loading it again; a different path, a different expected build ID, or a module file that
// changed on disk since the load is refused with an error that asks for a restart.
const MptMp4Api* load_module(const std::filesystem::path& path, std::uintptr_t expected_base, std::string& error,
                             const std::string& expected_build_id = {});
// Drops one hold taken by a successful load_module. The module stays loaded (see above).
void release_module();
// At process shutdown, after every runtime is destroyed: unloads the module. Nothing loaded: does
// nothing and returns true. A hold still taken: returns false with `error` set and leaves the module
// loaded, since its code may still be running. A load after a successful shutdown loads it again.
bool shutdown_module(std::string& error);
// The loaded module's build ID, or empty when none is loaded.
std::string loaded_module_build_id();

}  // namespace mpt::mp4
