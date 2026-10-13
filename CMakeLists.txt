cmake_minimum_required(VERSION 3.25)

project(MarioPartyTogether
  VERSION 0.0.0
  DESCRIPTION "Host shell that runs mainline Mario Party titles natively from user-supplied dumps"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MPT_BUILD_TESTS "Build unit tests" ON)
option(MPT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Common warning set, applied per target via mpt_set_warnings().
function(mpt_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    if(MPT_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
    if(MPT_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# Title registration for the shell (P1-06 a). A runtime's CMakeLists.txt calls
#   mpt_register_title(TARGET <library> FUNCTION <name>)
# where <library> defines `void <name>(mpt::shell::ShellTitles&)` (core/shell/titles.h).
# The shell's register_titles() is generated from these calls at the end of this file.
function(mpt_register_title)
  cmake_parse_arguments(ARG "" "TARGET;FUNCTION" "" ${ARGN})
  if(NOT ARG_TARGET OR NOT ARG_FUNCTION)
    message(FATAL_ERROR "mpt_register_title needs TARGET and FUNCTION")
  endif()
  set_property(GLOBAL APPEND PROPERTY MPT_TITLE_TARGETS ${ARG_TARGET})
  set_property(GLOBAL APPEND PROPERTY MPT_TITLE_FUNCTIONS ${ARG_FUNCTION})
endfunction()

find_package(SDL3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# asset-import first: core/ reads MPT_KNOWN_DUMPS_JSON from it.
add_subdirectory(asset-import)
add_subdirectory(core)
add_subdirectory(input)
# GX renderer for runtimes/gc_wii (ADR 0007 §6, P1-14 gate amendment). `null` needs no GPU
# and no Dawn build; `dawn` needs Dawn installed at MPT_DAWN_PREFIX by
# runtimes/gc_wii/gx/tools/build_dawn.cmake.
set(MPT_GX_BACKEND "null" CACHE STRING "GX renderer for runtimes/gc_wii: null or dawn")
set_property(CACHE MPT_GX_BACKEND PROPERTY STRINGS null dawn)
set(MPT_DAWN_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/external/dawn" CACHE PATH
  "Dawn install prefix made by runtimes/gc_wii/gx/tools/build_dawn.cmake")
option(MPT_REQUIRE_GX "Fail the configure if the GX layer cannot be built (always on for MPT_GX_BACKEND=dawn)" OFF)

# GameCube/Wii SDK layer: built with Clang as its own project (ADR 0008 §4).
add_subdirectory(runtimes/gc_wii)

# Every runtime is added above, so the shell's title list is complete.
mpt_generate_title_registration(mpt_shell)

if(MPT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
