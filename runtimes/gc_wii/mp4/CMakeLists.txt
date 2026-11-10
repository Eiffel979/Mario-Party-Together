# runtimes/gc_wii/mp4: the MP4 game module (P1-15, ADR 0008 amendment after P1-06, ADR 0009).
#
# Clang side. Builds mp4_game (a shared library) from Party Board's game sources, its RELs and msm at
# the pin in decomp.lock, on our SDK layer, the GX layer and MTX, and MusyX (runtimes/gc_wii/musyx).
# It exports only the C ABI in include/mpt/mp4/game_abi.h. Nothing from the game sources is committed: the pinned
# checkouts come from mpt_fetch_sources (or the asset-import recipe), and our changes to them are
# patch files in patches/.
#
# Compile-only builds (CI, unit tests) need no dump: the game sources hold code, and the data
# compiled into the console DOL/RELs is read at runtime from the user's extracted disc (dolassets).

set(MPT_MP4_PARTYBOARD_DIR "" CACHE PATH "Verified Party Board checkout (default: <MPT_SOURCE_CACHE>/partyboard/<commit>)")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/decomp.lock" _lock)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/decomp.lock")
string(JSON _n LENGTH "${_lock}" sources)
math(EXPR _last "${_n} - 1")
foreach(_i RANGE ${_last})
  string(JSON _name GET "${_lock}" sources ${_i} name)
  string(JSON _commit GET "${_lock}" sources ${_i} commit)
  set(_mp4_commit_${_name} "${_commit}")
endforeach()
if(NOT MPT_MP4_PARTYBOARD_DIR)
  set(MPT_MP4_PARTYBOARD_DIR "${MPT_SOURCE_CACHE}/partyboard/${_mp4_commit_partyboard}")
endif()

function(_mpt_mp4_unavailable)
  string(JOIN "" why ${ARGV})
  message(FATAL_ERROR "runtimes/gc_wii/mp4: ${why} Fetch the sources with mpt_fetch_sources --lock "
                      "runtimes/gc_wii/mp4/decomp.lock --cache <MPT_SOURCE_CACHE>.")
endfunction()
if(NOT EXISTS "${MPT_MP4_PARTYBOARD_DIR}/files.cmake")
  _mpt_mp4_unavailable("no Party Board checkout at ${MPT_MP4_PARTYBOARD_DIR}.")
endif()
if(NOT TARGET mpt_musyx_core)
  _mpt_mp4_unavailable("MusyX (runtimes/gc_wii/musyx) is not built; set MPT_MUSYX_DIR or MPT_SOURCE_CACHE.")
endif()
if(NOT TARGET mpt_gx_frontend)
  message(FATAL_ERROR "runtimes/gc_wii/mp4 needs the GX layer (runtimes/gc_wii/gx); configure it first.")
endif()

# ---- Patched copies of the pins -------------------------------------------------------------------
find_package(Git REQUIRED)
function(_mpt_mp4_patched_copy name src dest patch_dir)
  file(GLOB _patches CONFIGURE_DEPENDS "${patch_dir}/*.patch")
  list(SORT _patches)
  set(_stamp_text "${src}")
  foreach(_p IN LISTS _patches)
    file(SHA256 "${_p}" _h)
    string(APPEND _stamp_text ";${_h}")
  endforeach()
  string(SHA256 _stamp "${_stamp_text}")
  set(_stamp_file "${dest}.stamp")
  set(_old "")
  if(EXISTS "${_stamp_file}")
    file(READ "${_stamp_file}" _old)
  endif()
  if(_old STREQUAL _stamp AND EXISTS "${dest}")
    return()
  endif()
  message(STATUS "runtimes/gc_wii/mp4: preparing ${name} (${src}) with ${patch_dir}")
  file(REMOVE_RECURSE "${dest}")
  file(MAKE_DIRECTORY "${dest}")
  foreach(_sub IN LISTS ARGN)
    if(EXISTS "${src}/${_sub}")
      get_filename_component(_parent "${dest}/${_sub}" DIRECTORY)
      file(COPY "${src}/${_sub}" DESTINATION "${_parent}")
    endif()
  endforeach()
  get_filename_component(_ceiling "${dest}" DIRECTORY)
  foreach(_p IN LISTS _patches)
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_ceiling}"
              "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${_p}"
      WORKING_DIRECTORY "${dest}" RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
      file(REMOVE_RECURSE "${dest}")
      message(FATAL_ERROR "runtimes/gc_wii/mp4: ${_p} does not apply: ${_err}")
    endif()
  endforeach()
  file(WRITE "${_stamp_file}" "${_stamp}")
endfunction()

set(MPT_MP4_PB "${CMAKE_CURRENT_BINARY_DIR}/partyboard")
_mpt_mp4_patched_copy(partyboard "${MPT_MP4_PARTYBOARD_DIR}" "${MPT_MP4_PB}"
  "${CMAKE_CURRENT_SOURCE_DIR}/patches/partyboard" files.cmake include src/game src/REL src/libhu src/msm src/port
  libs/dolphin/include/dolphin/thp)

# Party Board's own lists of game and REL sources (files.cmake only sets variables).
include("${MPT_MP4_PB}/files.cmake")
list(FILTER GAME_FILES INCLUDE REGEX "\\.c$")
list(FILTER REL_FILES INCLUDE REGEX "\\.c$")
file(GLOB MPT_MP4_MSM_FILES RELATIVE "${MPT_MP4_PB}" "${MPT_MP4_PB}/src/msm/*.c")

# ---- Flags ---------------------------------------------------------------------------------------
# Game code: Party Board's PC macros (byteswapping, 64-bit layouts) plus MPT_PC, our target macro,
# where its TARGET_PC code assumes Party Board's own port layer. Strict FP and the math redirect come
# from mpt::gc_sdk and mpt::gx_math_redirect (ADR 0008 sections 4 and 5).
set(MPT_MP4_GAME_DEFS TARGET_PC MPT_PC NON_MATCHING OPTIMIZED_TEXTURE_LOADING BYTESWAPPING TARGET_DOL VERSION=0)
# VERSION=0 is GMPE01_00, the only revision Phase 1 supports (plan D3). Party Board builds VERSION=1.
# Warnings: off for Party Board's sources only (pinned upstream code, not ours to fix). Our own code
# (src/, the generated REL table and info records, tests/) builds with the GX layer's warning set and
# as errors. Party Board's and aurora's headers are SYSTEM includes; ours are not.
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(MPT_MP4_NOWARN /w)
  set(MPT_MP4_WARN_FLAGS /W4 /permissive- /utf-8 /WX)
else()
  set(MPT_MP4_NOWARN -w)
  set(MPT_MP4_WARN_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Werror)
endif()

add_library(mpt_mp4_game_flags INTERFACE)
# MUSY_TARGET and MUSY_VERSION_* come from mpt_musyx_core, so msm sees the library's own configuration.
target_compile_definitions(mpt_mp4_game_flags INTERFACE ${MPT_MP4_GAME_DEFS})
target_include_directories(mpt_mp4_game_flags SYSTEM INTERFACE
  "${MPT_MP4_PB}/include" "${MPT_GX_AURORA_PATCHED}/include" "${MPT_GX_AURORA_PATCHED}/include/dolphin"
  # Only the decomp's THP file-format headers (THPSimple.h); the rest of its SDK is ours.
  "${MPT_MP4_PB}/libs/dolphin/include")
target_include_directories(mpt_mp4_game_flags INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(mpt_mp4_game_flags INTERFACE mpt::gc_sdk mpt::gx_math_redirect mpt_gx_frontend mpt_musyx_core)
# Every transcendental the game's C code calls goes to our deterministic library, never the host CRT
# or glibc (ADR 0008 section 5); mp4_no_host_math checks the result at link level.
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  target_compile_options(mpt_mp4_game_flags INTERFACE "$<$<COMPILE_LANGUAGE:C>:/FImpt_gc/libm_redirect.h>")
else()
  target_compile_options(mpt_mp4_game_flags INTERFACE "$<$<COMPILE_LANGUAGE:C>:SHELL:-include mpt_gc/libm_redirect.h>")
endif()

# ---- Game core (the DOL), libhu, msm --------------------------------------------------------------
# Game sources Party Board leaves out because its port layer replaces them; we use the originals:
# audio (HuAud* over msm, ADR 0007 section 4), THP playback, and the soft-reset check.
list(APPEND GAME_FILES src/game/audio.c src/game/THPDraw.c src/game/THPSimple.c src/game/thpmain.c src/game/sreset.c)
list(TRANSFORM GAME_FILES PREPEND "${MPT_MP4_PB}/")
list(TRANSFORM MPT_MP4_MSM_FILES PREPEND "${MPT_MP4_PB}/")
# Our replacements for Party Board's port layer (include/mpt_mp4_port.h) and its coroutines.
set(MPT_MP4_PORT_FILES src/port.c src/sdk_bridge.c src/libco.c src/layout_check.c src/game_reads.c src/msm_swap.c src/msm_diag.c)
# Test only: src/layout_probe.c, first in the link, moves parts of the module so a Windows run can check
# that state_hash does not depend on the module layout (docs/titles/mp4.md 20). OFF in every normal build;
# the recipe passes it only from a test build (host/CMakeLists.txt), and the mode is part of the build ID.
#  - ON:     8 KiB of code, 4 KiB + 24 bytes at the start of the game core's data and of its rodata;
#  - rodata: the rodata padding only, which moves only the game core's read-only data (and the sections
#            after .rdata by whole pages).
set(MPT_MP4_LAYOUT_PROBE OFF CACHE STRING "Test only: OFF, ON or rodata; shift parts of the module to check state_hash's layout independence")
set_property(CACHE MPT_MP4_LAYOUT_PROBE PROPERTY STRINGS OFF ON rodata)
set(_mp4_probe "")
set(_mp4_probe_defs "")
if(MPT_MP4_LAYOUT_PROBE STREQUAL "ON")
  set(_mp4_probe_defs MPT_MP4_LAYOUT_PROBE_TU MPT_MP4_LAYOUT_PROBE_CODE MPT_MP4_LAYOUT_PROBE_DATA MPT_MP4_LAYOUT_PROBE_RODATA)
elseif(MPT_MP4_LAYOUT_PROBE STREQUAL "rodata")
  set(_mp4_probe_defs MPT_MP4_LAYOUT_PROBE_TU MPT_MP4_LAYOUT_PROBE_RODATA)
elseif(NOT MPT_MP4_LAYOUT_PROBE STREQUAL "OFF")
  message(FATAL_ERROR "MPT_MP4_LAYOUT_PROBE must be OFF, ON or rodata, not '${MPT_MP4_LAYOUT_PROBE}'")
endif()
if(_mp4_probe_defs)
  set(_mp4_probe src/layout_probe.c)
  list(APPEND MPT_MP4_PORT_FILES src/layout_probe.c)
endif()
add_library(mpt_mp4_dol OBJECT ${_mp4_probe} ${GAME_FILES} ${MPT_MP4_MSM_FILES} ${MPT_MP4_PORT_FILES})
target_link_libraries(mpt_mp4_dol PRIVATE mpt_mp4_game_flags)
set_source_files_properties(${GAME_FILES} ${MPT_MP4_MSM_FILES} TARGET_DIRECTORY mpt_mp4_dol
                            PROPERTIES COMPILE_OPTIONS "${MPT_MP4_NOWARN}")
set_source_files_properties(${MPT_MP4_PORT_FILES} TARGET_DIRECTORY mpt_mp4_dol
                            PROPERTIES COMPILE_OPTIONS "${MPT_MP4_WARN_FLAGS}")
if(_mp4_probe_defs)
  set_source_files_properties(src/layout_probe.c TARGET_DIRECTORY mpt_mp4_dol
                              PROPERTIES COMPILE_DEFINITIONS "${_mp4_probe_defs}")
endif()

# Party Board port code we reuse unchanged: the 64-bit HSF model-format conversion (P1-15 piece 4).
# Its only file-scope data is a host container (std::unordered_set sVisitedPtrs, cleared but never
# filled). That is host state, not game state, so the file is built without game_section.h: inside
# the game data range the container's heap pointers differ per process, which split the state hash,
# and a load_state would restore them over a live container.
add_library(mpt_mp4_byteswap OBJECT "${MPT_MP4_PB}/src/port/byteswap.cpp")
target_link_libraries(mpt_mp4_byteswap PRIVATE mpt_mp4_game_flags)
target_compile_options(mpt_mp4_byteswap PRIVATE ${MPT_MP4_NOWARN})

# ---- RELs: compiled separately, then isolated (tools/MptMp4Rel.cmake) ----------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/tools/MptMp4Rel.cmake")
mpt_mp4_rel_setup()

set(MPT_MP4_RELS "")
foreach(_f IN LISTS REL_FILES)
  if(_f MATCHES "^src/REL/([^/]+)/")
    list(APPEND MPT_MP4_RELS "${CMAKE_MATCH_1}")
  endif()
endforeach()
list(REMOVE_DUPLICATES MPT_MP4_RELS)

# The game core's writable data goes to its own range too (src/game_section.h, src/game_data.c).
if(MPT_MP4_REL_FORMAT STREQUAL "coff")
  set(_game_fi "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/game_section.h")
else()
  set(_game_fi "SHELL:-include \"${CMAKE_CURRENT_SOURCE_DIR}/src/game_section.h\"")  # SHELL: so CMake keeps each -include
endif()
target_compile_options(mpt_mp4_dol PRIVATE ${MPT_MP4_NOBSS} ${_game_fi})

foreach(_rel IN LISTS MPT_MP4_RELS)
  set(_srcs ${REL_FILES})
  list(FILTER _srcs INCLUDE REGEX "^src/REL/${_rel}/")
  list(TRANSFORM _srcs PREPEND "${MPT_MP4_PB}/")
  if(_rel MATCHES "^w..Dll$")
    list(APPEND _srcs "${MPT_MP4_PB}/src/REL/board_executor.c")
  endif()
  mpt_mp4_add_rel(${_rel} SET MPT_MP4_REL DIR "${CMAKE_CURRENT_BINARY_DIR}/rels" TARGET_PREFIX mpt_mp4_rel_
                  SOURCES ${_srcs} LINK mpt_mp4_game_flags OPTIONS ${MPT_MP4_NOWARN})
endforeach()
mpt_mp4_write_rel_table(MPT_MP4_REL "${CMAKE_CURRENT_BINARY_DIR}/generated/mpt_mp4_rel_table.c")

# ---- The game module -----------------------------------------------------------------------------
add_library(mp4_game SHARED
  $<TARGET_OBJECTS:mpt_mp4_dol>
  $<TARGET_OBJECTS:mpt_mp4_byteswap>
  ${MPT_MP4_REL_INFOS}
  "${CMAKE_CURRENT_BINARY_DIR}/generated/mpt_mp4_rel_table.c"
  src/rel_loader.c
  src/libco_stats.c
  src/game_data.c
  src/abi.cpp
  src/aram_checks.cpp
  src/copy_trap.cpp)
include("${CMAKE_CURRENT_SOURCE_DIR}/tools/MptMp4ModuleSymbols.cmake")
target_link_libraries(mp4_game PRIVATE mpt_mp4_game_flags ${MPT_MP4_REL_LIBS} mpt::gx mpt::musyx mpt_mp4_module_symbols)
# The shared libraries the module needs at run time (webgpu_dawn with the Dawn backend), one per line,
# for the asset-import recipe to install next to it. Empty on platforms without DLLs.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mp4_game_runtime_deps.txt"
     CONTENT "$<JOIN:$<TARGET_RUNTIME_DLLS:mp4_game>,\n>\n")
target_compile_options(mp4_game PRIVATE ${MPT_MP4_WARN_FLAGS})
set_target_properties(mp4_game PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden PREFIX "")
target_include_directories(mp4_game PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Fixed bases and the main RAM size (ADR 0008 section 11 and its amendment, PR #44).
include("${CMAKE_CURRENT_SOURCE_DIR}/layout.cmake")

# The build fingerprint (build_id.cmake, ADR 0009 amendment after P1-16): one id for save states,
# replays, golden trails and the recipe's cache key. The recipe passes MPT_MP4_SOURCE_STATES (what its
# resolver found for each pin) and MPT_MP4_RECIPE_ARGS_HASH; a developer build without them records its
# sources as unverified. Every input is a configure dependency, so a content change re-runs this.
set(MPT_MP4_SOURCE_STATES "" CACHE STRING "Set by the asset-import recipe: <pin>=pinned|unpinned:<fingerprint>, ...")
set(MPT_MP4_RECIPE_ARGS_HASH "none" CACHE STRING "Set by the asset-import recipe: a hash of its module-build arguments")
# The layout probe's mode reaches the build ID only through the recipe's arguments hash (host/CMakeLists.txt
# passes MPT_MP4_LAYOUT_PROBE among them). A standalone module build has no such hash, so its probe
# build would share a normal build's ID (#66 review): it is refused.
if(_mp4_probe_defs AND MPT_MP4_RECIPE_ARGS_HASH STREQUAL "none")
  message(FATAL_ERROR "MPT_MP4_LAYOUT_PROBE=${MPT_MP4_LAYOUT_PROBE} needs the asset-import recipe, whose arguments "
                      "hash puts the mode in the build ID; set MPT_MP4_RECIPE_LAYOUT_PROBE on a host build instead")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/build_id.cmake")
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(_mp4_msvc_frontend TRUE)
else()
  set(_mp4_msvc_frontend FALSE)
endif()
mpt_mp4_build_id(ID _mp4_id MANIFEST _mp4_manifest INPUTS _mp4_id_inputs
  GC_WII_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." CORE_INCLUDE_DIR "${MPT_CORE_INCLUDE_DIR}"
  COMPILER "${CMAKE_C_COMPILER}" BUILD_TYPE "${CMAKE_BUILD_TYPE}" GX_BACKEND "${MPT_GX_BACKEND}"
  MSVC_FRONTEND ${_mp4_msvc_frontend} RECIPE_ARGS_HASH "${MPT_MP4_RECIPE_ARGS_HASH}" SOURCE_STATES ${MPT_MP4_SOURCE_STATES})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_mp4_id_inputs})
set(MPT_MP4_BUILD_ID "${_mp4_id}" CACHE INTERNAL "MP4 game module build fingerprint")
mpt_mp4_tree_state("${CMAKE_CURRENT_SOURCE_DIR}/.." "${MPT_CORE_INCLUDE_DIR}" _mp4_tree_state)
# The manifest: every component with its hash, then the commit and the dirty flag. The recipe installs
# it next to the module and checks its id against runtime_build_version.
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mp4_game_build_id.txt" CONTENT "${_mp4_manifest}${_mp4_tree_state}" @ONLY)
target_compile_definitions(mp4_game PRIVATE "MPT_MP4_BUILD_ID=\"${MPT_MP4_BUILD_ID}\""
                           MPT_MP4_MAIN_RAM_BYTES=${MPT_MP4_MAIN_RAM_BYTES}u)
message(STATUS "runtimes/gc_wii/mp4: build ID ${MPT_MP4_BUILD_ID}")

# Every object library, for compile-only builds.
add_custom_target(mpt_mp4_objects)
add_dependencies(mpt_mp4_objects mpt_mp4_dol mpt_mp4_byteswap)
foreach(_rel IN LISTS MPT_MP4_RELS)
  add_dependencies(mpt_mp4_objects mpt_mp4_rel_${_rel})
endforeach()
# A fixed image base (ADR 0008 section 11): function pointers the game stores in the arena and in its
# state regions stay valid in a fresh process. Windows: no dynamic base and no relocations, so the
# loader either maps it there or fails. Linux: lld links the .so at that base; glibc's loader asks the
# kernel for that address, and the host runtime checks where it landed.
mpt_mp4_fixed_base_link(mp4_game)  # layout.cmake
mpt_mp4_link_symtab(mp4_game)      # tools/MptMp4ModuleSymbols.cmake
# The state hash names the module's globals and functions by symbol (src/module_symbols.cpp, ADR 0008
# amendment after P1-16), so the image keeps a symbol table: /DEBUG:SYMTAB writes lld-link's COFF table
# (next to the PDB, outside every mapped section, so the layout does not change); ELF keeps .symtab
# unless stripped. No identical-code folding and no incremental link: mpt_mp4_fixed_base_link above.
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  target_link_options(mp4_game PRIVATE /errorlimit:0)
else()
  # Export only mpt_mp4_get_api, as the dllexport does on Windows. The objects are PIE (the distro
  # Clang default), not PIC: they reach the module's own globals PC-relative. In a -shared link lld
  # rejects that against a symbol that could be interposed, which every default-visibility symbol is.
  # Made local here, none can be, so the code stays as on Windows (no GOT) and nothing else is exported.
  set(_mp4_ver "${CMAKE_CURRENT_SOURCE_DIR}/src/mp4_game.ver")
  target_link_options(mp4_game PRIVATE -Wl,--error-limit=0 "-Wl,--version-script=${_mp4_ver}")
  set_property(TARGET mp4_game APPEND PROPERTY LINK_DEPENDS "${_mp4_ver}")
endif()

# Unit checks (synthetic data only).
if(MPT_BUILD_TESTS)
  add_subdirectory(tests)
  # Link checks on the game code (the DOL objects and every isolated REL): no host libm
  # transcendental or fma (ADR 0008 section 5), and no host threads, clocks, RNG or heap (ADR 0008
  # sections 1, 2 and 7). Host-side files (abi.cpp, aram_checks.cpp, rel_loader.c, libco_stats.c, copy_trap.cpp) are not
  # game code.
  # Hundreds of objects: the list goes through a file, not the command line.
  string(REPLACE ";" "\n" _rel_lines "${MPT_MP4_REL_LIBS}")
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mp4_game_code.txt"
       CONTENT "$<JOIN:$<TARGET_OBJECTS:mpt_mp4_dol>,\n>\n$<JOIN:$<TARGET_OBJECTS:mpt_mp4_byteswap>,\n>\n${_rel_lines}\n")
  add_test(NAME mp4_no_host_math
    COMMAND "${CMAKE_COMMAND}" "-DNM=${MPT_MP4_NM}" "-DFILES_FROM=${CMAKE_CURRENT_BINARY_DIR}/mp4_game_code.txt"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/tests/check_no_host_math.cmake")
  add_test(NAME mp4_no_host_services
    COMMAND "${CMAKE_COMMAND}" "-DNM=${MPT_MP4_NM}" "-DFILES_FROM=${CMAKE_CURRENT_BINARY_DIR}/mp4_game_code.txt"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../musyx/check_no_host_services.cmake")
endif()
