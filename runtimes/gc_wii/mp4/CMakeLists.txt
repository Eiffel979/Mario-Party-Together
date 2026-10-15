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
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(MPT_MP4_NOWARN /w)
else()
  set(MPT_MP4_NOWARN -w)
endif()

add_library(mpt_mp4_game_flags INTERFACE)
# MUSY_TARGET and MUSY_VERSION_* come from mpt_musyx_core, so msm sees the library's own configuration.
target_compile_definitions(mpt_mp4_game_flags INTERFACE ${MPT_MP4_GAME_DEFS})
target_include_directories(mpt_mp4_game_flags SYSTEM INTERFACE
  "${MPT_MP4_PB}/include" "${MPT_GX_AURORA_PATCHED}/include" "${MPT_GX_AURORA_PATCHED}/include/dolphin"
  "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/generated"
  # Only the decomp's THP file-format headers (THPSimple.h); the rest of its SDK is ours.
  "${MPT_MP4_PB}/libs/dolphin/include")
target_compile_options(mpt_mp4_game_flags INTERFACE ${MPT_MP4_NOWARN})
target_link_libraries(mpt_mp4_game_flags INTERFACE mpt::gc_sdk mpt::gx_math_redirect mpt_gx_frontend mpt_musyx_core)

# ---- Game core (the DOL), libhu, msm --------------------------------------------------------------
# Game sources Party Board leaves out because its port layer replaces them; we use the originals:
# audio (HuAud* over msm, ADR 0007 section 4), THP playback, and the soft-reset check.
list(APPEND GAME_FILES src/game/audio.c src/game/THPDraw.c src/game/THPSimple.c src/game/thpmain.c src/game/sreset.c)
list(TRANSFORM GAME_FILES PREPEND "${MPT_MP4_PB}/")
# Party Board port code we reuse unchanged: the 64-bit HSF model-format conversion (P1-15 piece 4).
list(APPEND GAME_FILES "${MPT_MP4_PB}/src/port/byteswap.cpp")
list(TRANSFORM MPT_MP4_MSM_FILES PREPEND "${MPT_MP4_PB}/")
# Our replacements for Party Board's port layer (include/mpt_mp4_port.h) and its coroutines.
set(MPT_MP4_PORT_FILES src/port.c src/sdk_bridge.c src/libco.c)
add_library(mpt_mp4_dol OBJECT ${GAME_FILES} ${MPT_MP4_MSM_FILES} ${MPT_MP4_PORT_FILES})
target_link_libraries(mpt_mp4_dol PRIVATE mpt_mp4_game_flags)

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
  set(_game_fi -include "${CMAKE_CURRENT_SOURCE_DIR}/src/game_section.h")
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
                  SOURCES ${_srcs} LINK mpt_mp4_game_flags)
endforeach()
mpt_mp4_write_rel_table(MPT_MP4_REL "${CMAKE_CURRENT_BINARY_DIR}/generated/mpt_mp4_rel_table.c")

# ---- The game module -----------------------------------------------------------------------------
add_library(mp4_game SHARED
  $<TARGET_OBJECTS:mpt_mp4_dol>
  ${MPT_MP4_REL_INFOS}
  "${CMAKE_CURRENT_BINARY_DIR}/generated/mpt_mp4_rel_table.c"
  src/rel_loader.c
  src/game_data.c)
target_link_libraries(mp4_game PRIVATE mpt_mp4_game_flags ${MPT_MP4_REL_LIBS} mpt::gx mpt_musyx_core)
set_target_properties(mp4_game PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden PREFIX "")

# Every object library, for compile-only builds.
add_custom_target(mpt_mp4_objects)
add_dependencies(mpt_mp4_objects mpt_mp4_dol)
foreach(_rel IN LISTS MPT_MP4_RELS)
  add_dependencies(mpt_mp4_objects mpt_mp4_rel_${_rel})
endforeach()
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  target_link_options(mp4_game PRIVATE /errorlimit:0)
else()
  target_link_options(mp4_game PRIVATE -Wl,--error-limit=0)
endif()

# Unit checks (synthetic data only).
if(MPT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
