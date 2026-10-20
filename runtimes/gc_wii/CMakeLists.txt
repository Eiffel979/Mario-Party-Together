# runtimes/gc_wii: the shared GameCube/Wii SDK replacement layer (owner: runtime-gc-wii).
#
# ADR 0008 §4: game-facing code is built with Clang on every platform (clang-cl on
# Windows), strict IEEE floating point and no FMA contraction. The rest of the repo
# builds with the presets' compiler (MSVC on Windows), so:
#   - configured with Clang (directly, or as its own project): build the targets here;
#   - included from a non-Clang build: build this directory as a separate CMake
#     project with Clang (ExternalProject) and register its tests with CTest.
cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(MptGcWii LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  option(MPT_BUILD_TESTS "Build unit tests" ON)
  option(MPT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
  if(MPT_BUILD_TESTS)
    enable_testing()
  endif()
endif()

set(MPT_MUSYX_DIR "" CACHE PATH "musyx checkout at the commit in runtimes/gc_wii/mp4/decomp.lock (see musyx/CMakeLists.txt)")
option(MPT_REQUIRE_MUSYX "Fail the configure if MPT_MUSYX_DIR is not a pinned musyx checkout" OFF)
option(MPT_BUILD_MP4_GAME_MODULE "Build the MP4 game module from the pinned sources in mp4/decomp.lock (compile-only without a dump)" OFF)
option(MPT_REQUIRE_GC_WII "Fail the configure if the GameCube SDK layer cannot be built (no Clang); CI sets this" OFF)

set(MPT_CORE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../core/include" CACHE PATH
  "core/ headers (the SDK uses header-only core types: InputFrame, DeterministicRng, StateHasher, Result)")

# Host tools (the verified source fetcher for aurora.lock/dawn.lock) need asset-import, which only
# the root project has. They are not game code, so they build with the root's compiler.
if(TARGET mpt::asset_import)
  add_subdirectory(gx/tools)
  add_subdirectory(mp4/recipe)  # asset-import's MP4 build recipe
  add_subdirectory(mp4/host)    # Mp4Runtime, Mp4Adapter and their shell registration (P1-16)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  enable_language(C)
  if(MPT_BUILD_TESTS)
    enable_testing()  # the root enables testing after its subdirectories; register ours too
  endif()
  add_subdirectory(sdk)
  add_subdirectory(gx)
  add_subdirectory(musyx)
  if(MPT_BUILD_MP4_GAME_MODULE)
    add_subdirectory(mp4)
  elseif(MPT_BUILD_TESTS)
    # The game module's own machinery tests need no Party Board checkout, so they always run.
    add_subdirectory(mp4/tests/standalone mp4/tests/standalone)
  endif()
  return()
endif()

# ---- Not Clang: build ourselves with Clang as a separate project ---------------------
if(WIN32)
  get_filename_component(_mpt_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  find_program(MPT_GC_WII_CLANG_CXX NAMES clang-cl
    HINTS "${_mpt_cl_dir}/../../../../../Llvm/x64/bin" "$ENV{ProgramFiles}/LLVM/bin")
  set(MPT_GC_WII_CLANG_C "${MPT_GC_WII_CLANG_CXX}" CACHE FILEPATH "C compiler for runtimes/gc_wii")
else()
  find_program(MPT_GC_WII_CLANG_CXX NAMES clang++ clang++-18 clang++-17)
  find_program(MPT_GC_WII_CLANG_C NAMES clang clang-18 clang-17)
endif()

if(NOT MPT_GC_WII_CLANG_CXX OR NOT MPT_GC_WII_CLANG_C)
  if(MPT_REQUIRE_GC_WII)
    message(FATAL_ERROR "runtimes/gc_wii: no Clang toolchain found and MPT_REQUIRE_GC_WII is ON (ADR 0008 §4). "
                        "Install Clang (clang-cl on Windows) or set MPT_GC_WII_CLANG_CXX / MPT_GC_WII_CLANG_C.")
  endif()
  message(WARNING "runtimes/gc_wii: no Clang toolchain found (ADR 0008 §4 requires it); the GameCube SDK layer is not built. "
                  "Set MPT_GC_WII_CLANG_CXX / MPT_GC_WII_CLANG_C.")
  return()
endif()
message(STATUS "runtimes/gc_wii: building with ${MPT_GC_WII_CLANG_CXX}")

include(ExternalProject)
set(_mpt_gc_bin "${CMAKE_CURRENT_BINARY_DIR}/clang")
set(_mpt_gc_prefix "")
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
  set(_mpt_gc_prefix "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()
ExternalProject_Add(mpt_gc_wii_clang
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
  BINARY_DIR "${_mpt_gc_bin}"
  CMAKE_GENERATOR "${CMAKE_GENERATOR}"
  CMAKE_CACHE_ARGS
    "-DCMAKE_C_COMPILER:FILEPATH=${MPT_GC_WII_CLANG_C}"
    "-DCMAKE_CXX_COMPILER:FILEPATH=${MPT_GC_WII_CLANG_CXX}"
    "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
    "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_PREFIX_PATH:PATH=${_mpt_gc_prefix}"
    # Without vcpkg's toolchain nothing maps RelWithDebInfo/MinSizeRel onto vcpkg's release
    # packages, and CMake falls back to the debug ones (fmtd.lib): _ITERATOR_DEBUG_LEVEL mismatch.
    "-DCMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO:STRING=Release"
    "-DCMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL:STRING=Release"
    "-DMPT_CORE_INCLUDE_DIR:PATH=${MPT_CORE_INCLUDE_DIR}"
    "-DMPT_BUILD_TESTS:BOOL=${MPT_BUILD_TESTS}"
    "-DMPT_WARNINGS_AS_ERRORS:BOOL=${MPT_WARNINGS_AS_ERRORS}"
    "-DMPT_REQUIRE_GC_WII:BOOL=${MPT_REQUIRE_GC_WII}"
    # GX layer (runtimes/gc_wii/gx, ADR 0007 section 6)
    "-DMPT_GX_BACKEND:STRING=${MPT_GX_BACKEND}"
    "-DMPT_DAWN_PREFIX:PATH=${MPT_DAWN_PREFIX}"
    "-DMPT_REQUIRE_GX:BOOL=${MPT_REQUIRE_GX}"
    "-DMPT_SOURCE_CACHE:PATH=${MPT_SOURCE_CACHE}"
    "-DMPT_AURORA_DIR:PATH=${MPT_AURORA_DIR}"
    "-DMPT_MUSYX_DIR:PATH=${MPT_MUSYX_DIR}"
    "-DMPT_REQUIRE_MUSYX:BOOL=${MPT_REQUIRE_MUSYX}"
    "-DMPT_BUILD_MP4_GAME_MODULE:BOOL=${MPT_BUILD_MP4_GAME_MODULE}"
  INSTALL_COMMAND ""
  BUILD_ALWAYS ON
  USES_TERMINAL_BUILD ON)

if(MPT_BUILD_TESTS)
  enable_testing()  # the root enables testing after its subdirectories
  add_test(NAME gc_wii_sdk
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${_mpt_gc_bin}" --output-on-failure --no-tests=error)
endif()
