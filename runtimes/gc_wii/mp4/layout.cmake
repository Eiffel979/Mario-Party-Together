# Fixed addresses for fresh-process save states (ADR 0008 section 11), shared by the game module's link
# (mp4/CMakeLists.txt) and the host runtime (mp4/host), which checks them at boot. The same values on
# Windows and Linux, inside the x86-64 user address space and away from where either OS puts images,
# heaps and stacks by default.
#  - The module base (ADR 0008, "Module words: a safe rule must not depend on layout", point 1) has a
#    non-round upper half (0x5B97) and a lower half far from any small integer, so an aligned pair of
#    game s32 fields is very unlikely to read as a module address. The image must end at or below the
#    next 4 GiB boundary (0x5B98'0000'0000), so every module address has the same upper half; the host
#    checks that at boot (host/src/module_loader.cpp).
set(MPT_MP4_MODULE_BASE "0x5B979E3C0000")  # mp4_game.dll / .so image base
set(MPT_MP4_ARENA_BASE "0x400000000000")   # the SDK arena (SdkConfig::arena_base)
# Emulated main RAM for OSAlloc's arena (ADR 0008 amendment, PR #44): a build constant inside the
# module, never a setting. Party Board's TARGET_PC game heaps are 4x the console's (78 MiB); see
# docs/titles/mp4.md section 19 for the sizing.
set(MPT_MP4_MAIN_RAM_BYTES "117440512")      # 112 MiB

# mpt_mp4_fixed_base_link(<target> [BASE <address>])
# Links a shared library at a fixed image base the way the game module is linked (ADR 0008 section
# 11). The module uses it, and so can a stub module in a loader test that needs no game sources.
#  - MSVC-style linkers (lld-link, link.exe): /BASE with /DYNAMICBASE:NO /FIXED. The image has no
#    relocations, so Windows maps it at that base or fails the load. That includes systems where
#    Exploit protection forces ASLR on images without it ("Mandatory ASLR"): the load then fails
#    with the "fixed base in use" error, and Mandatory ASLR must be off for the executable.
#  - ELF: lld with --image-base (GNU ld has no equivalent for shared objects); glibc's loader asks
#    the kernel for that address, and the host checks where the module landed.
#  - Every function keeps its own address (ADR 0008, same ruling, point 4): no identical-code folding
#    (/OPT:NOICF, --icf=none), which lld-link and ld.lld could apply differently, and no incremental
#    link (/INCREMENTAL:NO), whose thunks would make a stored function pointer something other than
#    a function start. Stated explicitly rather than left to each linker's defaults. link.exe (an MSVC
#    host build's stub modules in tests/mp4_loader) honours both flags as lld-link does; the game
#    module itself is only ever linked by lld-link or ld.lld (runtimes/gc_wii is Clang-only).
function(mpt_mp4_fixed_base_link target)
  cmake_parse_arguments(PARSE_ARGV 1 A "" "BASE" "")
  if(NOT A_BASE)
    set(A_BASE "${MPT_MP4_MODULE_BASE}")
  endif()
  if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR MSVC)
    target_link_options(${target} PRIVATE "/BASE:${A_BASE}" /DYNAMICBASE:NO /FIXED /OPT:NOICF /INCREMENTAL:NO)
  else()
    target_link_options(${target} PRIVATE -fuse-ld=lld "-Wl,--image-base=${A_BASE}" -Wl,--icf=none)
  endif()
endfunction()
