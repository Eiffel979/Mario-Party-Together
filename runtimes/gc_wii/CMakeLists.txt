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

set(MPT_CORE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../core/include" CACHE PATH
  "core/ headers (the SDK uses header-only core types: InputFrame, DeterministicRng, StateHasher, Result)")

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  enable_language(C)
  add_subdirectory(sdk)
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
    "-DMPT_CORE_INCLUDE_DIR:PATH=${MPT_CORE_INCLUDE_DIR}"
    "-DMPT_BUILD_TESTS:BOOL=${MPT_BUILD_TESTS}"
    "-DMPT_WARNINGS_AS_ERRORS:BOOL=${MPT_WARNINGS_AS_ERRORS}"
  INSTALL_COMMAND ""
  BUILD_ALWAYS ON
  USES_TERMINAL_BUILD ON)

if(MPT_BUILD_TESTS)
  add_test(NAME gc_wii_sdk
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${_mpt_gc_bin}" --output-on-failure --no-tests=error)
endif()
