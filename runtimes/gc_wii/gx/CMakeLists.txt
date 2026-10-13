# runtimes/gc_wii/gx: aurora's GX front end, MTX and renderer at the pin, without aurora_core
# (ADR 0007 section 6 and its P1-14 gate amendment). Built only with Clang (see ../CMakeLists.txt).
#
# Inputs (all outside the repo, never committed):
#   MPT_AURORA_DIR   the verified aurora checkout (mpt_fetch_sources --lock aurora.lock); by default
#                    <MPT_SOURCE_CACHE>/aurora/<locked commit>
#   MPT_DAWN_PREFIX  Dawn installed by tools/build_dawn.cmake: a full build for MPT_GX_BACKEND=dawn,
#                    the headers-only mode is enough for null
# aurora's own CMakeLists.txt is never used: it builds aurora_core and downloads Dawn without a hash.

set(MPT_SOURCE_CACHE "${CMAKE_CURRENT_SOURCE_DIR}/../../../external/sources" CACHE PATH
  "Cache of verified external sources (mpt_fetch_sources --cache)")
set(MPT_GX_BACKEND "null" CACHE STRING "GX renderer: null or dawn")
set_property(CACHE MPT_GX_BACKEND PROPERTY STRINGS null dawn)
set(MPT_DAWN_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/../../../external/dawn" CACHE PATH "Dawn install prefix")
option(MPT_REQUIRE_GX "Fail the configure if the GX layer cannot be built (no aurora checkout or no Dawn)" OFF)
# The parent project forwards these even when unset; empty means the default.
get_filename_component(_repo "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
if(NOT MPT_SOURCE_CACHE)
  set(MPT_SOURCE_CACHE "${_repo}/external/sources")
endif()
if(NOT MPT_GX_BACKEND)
  set(MPT_GX_BACKEND null)
endif()
if(NOT MPT_DAWN_PREFIX)
  set(MPT_DAWN_PREFIX "${_repo}/external/dawn")
endif()
if(NOT MPT_GX_BACKEND MATCHES "^(null|dawn)$")
  message(FATAL_ERROR "MPT_GX_BACKEND must be null or dawn, not '${MPT_GX_BACKEND}'")
endif()

# ---- The pin ------------------------------------------------------------------------------------
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/aurora.lock" _lock)
string(JSON _aurora_commit GET "${_lock}" sources 0 commit)
string(JSON _aurora_tree GET "${_lock}" sources 0 tree)
set(MPT_AURORA_DIR "" CACHE PATH "Verified aurora checkout at the pin (default: <MPT_SOURCE_CACHE>/aurora/<commit>)")
if(NOT MPT_AURORA_DIR)
  set(MPT_AURORA_DIR "${MPT_SOURCE_CACHE}/aurora/${_aurora_commit}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/aurora.lock")

function(_mpt_gx_unavailable why)
  if(MPT_REQUIRE_GX)
    message(FATAL_ERROR "runtimes/gc_wii/gx: ${why}")
  endif()
  message(WARNING "runtimes/gc_wii/gx: ${why} The GX layer is not built.")
endfunction()

if(NOT EXISTS "${MPT_AURORA_DIR}/lib/gx/gx.cpp")
  _mpt_gx_unavailable("no aurora checkout at MPT_AURORA_DIR (${MPT_AURORA_DIR}). Fetch it with "
    "mpt_fetch_sources --lock runtimes/gc_wii/gx/aurora.lock --cache <MPT_SOURCE_CACHE>.")
  return()
endif()

# Is it the pinned commit, unmodified? (The resolver checks fetched copies; this catches overrides.)
find_package(Git QUIET)
set(MPT_GX_AURORA_PINNED FALSE)
if(GIT_EXECUTABLE)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${MPT_AURORA_DIR}" rev-parse HEAD "HEAD^{tree}"
    OUTPUT_VARIABLE _ids OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _rc ERROR_QUIET)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${MPT_AURORA_DIR}" status --porcelain --untracked-files=no
    OUTPUT_VARIABLE _dirty OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  string(REPLACE "\n" ";" _ids "${_ids}")
  if(_rc EQUAL 0 AND _ids STREQUAL "${_aurora_commit};${_aurora_tree}" AND _dirty STREQUAL "")
    set(MPT_GX_AURORA_PINNED TRUE)
  endif()
endif()
if(NOT MPT_GX_AURORA_PINNED)
  message(WARNING "runtimes/gc_wii/gx: ${MPT_AURORA_DIR} is not a clean checkout of the pinned aurora "
                  "${_aurora_commit}. The build is unpinned (ADR 0009 section 1).")
endif()

# ---- Source lists (explicit; a pin bump that moves a file fails here) ---------------------------
set(MPT_GX_FRONTEND_SOURCES
  lib/dolphin/gx/GXBump.cpp lib/dolphin/gx/GXCull.cpp lib/dolphin/gx/GXCpu2Efb.cpp lib/dolphin/gx/GXDispList.cpp
  lib/dolphin/gx/GXDraw.cpp lib/dolphin/gx/GXExtra.cpp lib/dolphin/gx/GXFifo.cpp lib/dolphin/gx/GXFrameBuffer.cpp
  lib/dolphin/gx/GXGeometry.cpp lib/dolphin/gx/GXGet.cpp lib/dolphin/gx/GXLighting.cpp lib/dolphin/gx/GXManage.cpp
  lib/dolphin/gx/GXPerf.cpp lib/dolphin/gx/GXPixel.cpp lib/dolphin/gx/GXTev.cpp lib/dolphin/gx/GXTexture.cpp
  lib/dolphin/gx/GXTransform.cpp lib/dolphin/gx/GXVert.cpp lib/dolphin/gx/GXAurora.cpp
  lib/gx/fifo.cpp lib/gx/command_processor.cpp)
set(MPT_GX_MTX_SOURCES
  lib/dolphin/mtx/mtx.c lib/dolphin/mtx/mtxstack.c lib/dolphin/mtx/mtxvec.c lib/dolphin/mtx/mtx44.c
  lib/dolphin/mtx/vec.c lib/dolphin/mtx/quat.c)
set(MPT_GX_RENDERER_SOURCES
  lib/gfx/clear.cpp lib/gfx/common.cpp lib/gfx/depth_peek.cpp lib/gfx/pipeline_cache.cpp lib/gfx/render_worker.cpp
  lib/gfx/dds_io.cpp lib/gfx/tex_copy_conv.cpp lib/gfx/tex_palette_conv.cpp lib/gfx/texture.cpp
  lib/gfx/texture_format.cpp lib/gfx/texture_convert.cpp lib/gfx/texture_replacement.cpp lib/gfx/png_io.cpp
  lib/gx/gx.cpp lib/gx/pipeline.cpp lib/gx/shader.cpp lib/gx/shader_info.cpp
  lib/webgpu/gpu.cpp lib/webgpu/gpu_cache.cpp)
set(MPT_GX_NULL_SOURCES tests/gx_test_stubs.cpp)

set(_missing)
foreach(_f IN LISTS MPT_GX_FRONTEND_SOURCES MPT_GX_MTX_SOURCES MPT_GX_RENDERER_SOURCES MPT_GX_NULL_SOURCES)
  if(NOT EXISTS "${MPT_AURORA_DIR}/${_f}")
    list(APPEND _missing "${_f}")
  endif()
endforeach()
if(_missing)
  message(FATAL_ERROR "runtimes/gc_wii/gx: the aurora checkout lacks listed sources (a pin bump moved them?): ${_missing}")
endif()

# ---- Patched copy of the pin --------------------------------------------------------------------
# The verified checkout stays pristine; the patch set is applied to a copy in the build tree.
file(GLOB _patches CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/patches/aurora/*.patch")
list(SORT _patches)
set(_stamp_text "${_aurora_commit}")
foreach(_p IN LISTS _patches)
  file(SHA256 "${_p}" _h)
  string(APPEND _stamp_text ";${_h}")
endforeach()
string(SHA256 _stamp "${_stamp_text}")
set(MPT_GX_AURORA "${CMAKE_CURRENT_BINARY_DIR}/aurora")
set(_stamp_file "${CMAKE_CURRENT_BINARY_DIR}/aurora.stamp")
set(_old_stamp "")
if(EXISTS "${_stamp_file}")
  file(READ "${_stamp_file}" _old_stamp)
endif()
if(NOT _old_stamp STREQUAL _stamp OR NOT EXISTS "${MPT_GX_AURORA}/lib")
  if(NOT GIT_EXECUTABLE)
    message(FATAL_ERROR "runtimes/gc_wii/gx: git is needed to apply the aurora patch set")
  endif()
  message(STATUS "runtimes/gc_wii/gx: applying ${CMAKE_CURRENT_LIST_DIR}/patches/aurora to aurora ${_aurora_commit}")
  file(REMOVE_RECURSE "${MPT_GX_AURORA}")
  file(COPY "${MPT_AURORA_DIR}/include" "${MPT_AURORA_DIR}/lib" DESTINATION "${MPT_GX_AURORA}")
  file(COPY "${MPT_AURORA_DIR}/tests/gx_test_stubs.cpp" DESTINATION "${MPT_GX_AURORA}/tests")
  foreach(_p IN LISTS _patches)
    # GIT_CEILING_DIRECTORIES keeps git from treating our own repository as the patch root.
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_CURRENT_BINARY_DIR}"
              "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${_p}"
      WORKING_DIRECTORY "${MPT_GX_AURORA}" RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
      file(REMOVE_RECURSE "${MPT_GX_AURORA}")
      message(FATAL_ERROR "runtimes/gc_wii/gx: ${_p} does not apply: ${_err}")
    endif()
  endforeach()
  file(WRITE "${_stamp_file}" "${_stamp}")
endif()
list(TRANSFORM MPT_GX_FRONTEND_SOURCES PREPEND "${MPT_GX_AURORA}/")
list(TRANSFORM MPT_GX_MTX_SOURCES PREPEND "${MPT_GX_AURORA}/")
list(TRANSFORM MPT_GX_RENDERER_SOURCES PREPEND "${MPT_GX_AURORA}/")
list(TRANSFORM MPT_GX_NULL_SOURCES PREPEND "${MPT_GX_AURORA}/")

# ---- Dawn (headers for every backend, the library only for dawn) --------------------------------
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/dawn.lock" _dawn_lock_sha)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/dawn.lock")
set(_recipe "cmake -DMPT_FETCH_TOOL=<mpt_fetch_sources> -DMPT_SOURCE_CACHE=<cache> -DMPT_DAWN_PREFIX=${MPT_DAWN_PREFIX}")
if(MPT_GX_BACKEND STREQUAL "null")
  string(APPEND _recipe " -DMPT_DAWN_HEADERS_ONLY=ON")
endif()
string(APPEND _recipe " -P runtimes/gc_wii/gx/tools/build_dawn.cmake")
unset(MPT_DAWN_STAMP_LOCK_SHA256)
if(EXISTS "${MPT_DAWN_PREFIX}/mpt_dawn_stamp.cmake")
  include("${MPT_DAWN_PREFIX}/mpt_dawn_stamp.cmake")
endif()
if(NOT MPT_DAWN_STAMP_LOCK_SHA256)
  _mpt_gx_unavailable("no Dawn at MPT_DAWN_PREFIX (${MPT_DAWN_PREFIX}). Make it with: ${_recipe}")
  return()
endif()
if(NOT MPT_DAWN_STAMP_LOCK_SHA256 STREQUAL _dawn_lock_sha)
  _mpt_gx_unavailable("the Dawn at ${MPT_DAWN_PREFIX} was built from a different dawn.lock. Rebuild it with: ${_recipe}")
  return()
endif()
if(MPT_GX_BACKEND STREQUAL "dawn" AND NOT MPT_DAWN_STAMP_MODE STREQUAL "full")
  _mpt_gx_unavailable("MPT_GX_BACKEND=dawn needs a full Dawn build, but ${MPT_DAWN_PREFIX} has headers only. "
                      "Build it with: ${_recipe}")
  return()
endif()
add_library(mpt_gx_dawn_headers INTERFACE)
target_include_directories(mpt_gx_dawn_headers SYSTEM INTERFACE "${MPT_DAWN_PREFIX}/include")

# ---- vcpkg libraries (gc-wii manifest feature) --------------------------------------------------
find_package(absl CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
# Tracy: headers only. TRACY_ENABLE is never defined, so every zone macro compiles to nothing, and
# TracyClient is never linked (ADR 0007 section 6).
get_target_property(_tracy_inc Tracy::TracyClient INTERFACE_INCLUDE_DIRECTORIES)
add_library(mpt_gx_tracy_headers INTERFACE)
target_include_directories(mpt_gx_tracy_headers SYSTEM INTERFACE ${_tracy_inc})

set(_aurora_defs AURORA TARGET_PC AURORA_ENABLE_GX WEBGPU_DAWN NOMINMAX)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(MPT_GX_WARN_FLAGS /W4 /permissive- /utf-8)
  if(MPT_WARNINGS_AS_ERRORS)
    list(APPEND MPT_GX_WARN_FLAGS /WX)
  endif()
  set(_aurora_nowarn /w)
  set(_force_include "/FI${CMAKE_CURRENT_SOURCE_DIR}/include/mpt_gx/math_redirect.h")
else()
  set(MPT_GX_WARN_FLAGS -Wall -Wextra -Wpedantic -Wshadow)
  if(MPT_WARNINGS_AS_ERRORS)
    list(APPEND MPT_GX_WARN_FLAGS -Werror)
  endif()
  set(_aurora_nowarn -w)
  set(_force_include -include "${CMAKE_CURRENT_SOURCE_DIR}/include/mpt_gx/math_redirect.h")
endif()

# Forced math redirect for code that includes aurora's Dolphin headers (ppc_math.h's inline frsqrte
# path calls fmaf): link this into anything compiled as game code, as the front end itself is.
add_library(mpt_gx_math_redirect INTERFACE)
target_compile_options(mpt_gx_math_redirect INTERFACE ${_force_include})
add_library(mpt::gx_math_redirect ALIAS mpt_gx_math_redirect)

# ---- Front end + MTX: game-visible, game module, ADR 0008 flags ---------------------------------
add_library(mpt_gx_frontend STATIC ${MPT_GX_FRONTEND_SOURCES} ${MPT_GX_MTX_SOURCES} src/sdk_additions.cpp)
add_library(mpt::gx_frontend ALIAS mpt_gx_frontend)
target_include_directories(mpt_gx_frontend
  PUBLIC include "${MPT_GX_AURORA}/include"
  PRIVATE "${MPT_GX_AURORA}/lib" "${MPT_GX_AURORA}/lib/dolphin/gx")
target_compile_definitions(mpt_gx_frontend PUBLIC ${_aurora_defs})
target_link_libraries(mpt_gx_frontend
  PUBLIC mpt::gc_sdk mpt_gx_dawn_headers mpt_gx_tracy_headers fmt::fmt xxHash::xxhash
         absl::flat_hash_map absl::flat_hash_set absl::btree absl::hash)
# aurora's sources: no warnings (not our code), the math redirect forced in.
set_source_files_properties(${MPT_GX_FRONTEND_SOURCES} ${MPT_GX_MTX_SOURCES} TARGET_DIRECTORY mpt_gx_frontend
  PROPERTIES COMPILE_OPTIONS "${_aurora_nowarn};${_force_include}")
target_compile_options(mpt_gx_frontend PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${MPT_GX_WARN_FLAGS}>")
# Renderer internals the front end's headers reference.
target_include_directories(mpt_gx_frontend PUBLIC "$<BUILD_INTERFACE:${MPT_GX_AURORA}/lib>")

# ---- Renderers ----------------------------------------------------------------------------------
# The null renderer is always built (it is small and needs no Dawn), so tests can compare null against
# real. mpt::gx is the one MPT_GX_BACKEND selects; a program links exactly one renderer.
add_library(mpt_gx_renderer_null STATIC ${MPT_GX_NULL_SOURCES} src/null_renderer.cpp)
set_source_files_properties(${MPT_GX_NULL_SOURCES} TARGET_DIRECTORY mpt_gx_renderer_null
  PROPERTIES COMPILE_OPTIONS "${_aurora_nowarn}")
target_link_libraries(mpt_gx_renderer_null PUBLIC mpt_gx_frontend)
target_include_directories(mpt_gx_renderer_null PRIVATE "${MPT_GX_AURORA}/lib" "${MPT_GX_AURORA}/lib/dolphin/gx")
target_compile_options(mpt_gx_renderer_null PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${MPT_GX_WARN_FLAGS}>")

if(MPT_GX_BACKEND STREQUAL "dawn")
  find_package(Dawn CONFIG REQUIRED PATHS "${MPT_DAWN_PREFIX}" NO_DEFAULT_PATH)
  find_package(SDL3 CONFIG REQUIRED)
  find_package(PNG REQUIRED)
  find_package(unofficial-sqlite3 CONFIG REQUIRED)
  add_library(mpt_gx_renderer_dawn STATIC ${MPT_GX_RENDERER_SOURCES} src/dawn_renderer.cpp)
  set_source_files_properties(${MPT_GX_RENDERER_SOURCES} TARGET_DIRECTORY mpt_gx_renderer_dawn
    PROPERTIES COMPILE_OPTIONS "${_aurora_nowarn}")
  target_link_libraries(mpt_gx_renderer_dawn
    PUBLIC mpt_gx_frontend dawn::webgpu_dawn
    PRIVATE SDL3::SDL3 PNG::PNG unofficial::sqlite3::sqlite3 absl::flat_hash_map absl::flat_hash_set absl::hash)
  target_include_directories(mpt_gx_renderer_dawn PRIVATE "${MPT_GX_AURORA}/lib" "${MPT_GX_AURORA}/lib/dolphin/gx")
  target_compile_options(mpt_gx_renderer_dawn PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${MPT_GX_WARN_FLAGS}>")
  add_library(mpt::gx ALIAS mpt_gx_renderer_dawn)
else()
  add_library(mpt::gx ALIAS mpt_gx_renderer_null)
endif()
message(STATUS "runtimes/gc_wii/gx: GX layer with the ${MPT_GX_BACKEND} renderer (aurora pinned: ${MPT_GX_AURORA_PINNED})")

# Copies webgpu_dawn.dll next to an executable (Windows).
function(mpt_gx_copy_runtime_dlls target)
  if(WIN32 AND MPT_GX_BACKEND STREQUAL "dawn")
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
      COMMAND_EXPAND_LISTS)
  endif()
endfunction()

if(MPT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
