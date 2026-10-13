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
# where <library> defines `void <name>(mpt::shell::ShellTitles&)` (core/shell/titles.h)
# at global scope, with C++ linkage. The shell's register_titles() is generated from
# these calls at the end of this file.
#
# <library> is the title's HOST-SIDE runtime and adapter, built by this tree's compiler
# like the shell. Code that ADR 0008 requires to be built with Clang (the SDK layer, the
# GX front end and the game module) is never a registration target: the host side reaches
# it only through the game module's C ABI (ADR 0008, amendment after P1-06).
function(mpt_register_title)
  cmake_parse_arguments(ARG "" "TARGET;FUNCTION" "" ${ARGN})
  if(NOT ARG_TARGET OR NOT ARG_FUNCTION)
    message(FATAL_ERROR "mpt_register_title needs TARGET and FUNCTION")
  endif()
  if(NOT ARG_FUNCTION MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR "mpt_register_title: FUNCTION '${ARG_FUNCTION}' must be a plain identifier "
                        "(it is declared at global scope in the generated file)")
  endif()
  if(NOT TARGET ${ARG_TARGET})
    message(FATAL_ERROR "mpt_register_title: '${ARG_TARGET}' is not a target (call it after add_library)")
  endif()
  get_target_property(_imported ${ARG_TARGET} IMPORTED)
  if(_imported)
    message(FATAL_ERROR "mpt_register_title: '${ARG_TARGET}' is imported; register the host-side "
                        "runtime library built by this tree instead")
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
# GameCube/Wii SDK layer: built with Clang as its own project (ADR 0008 §4).
add_subdirectory(runtimes/gc_wii)

# Every runtime is added above, so the shell's title list is complete.
mpt_generate_title_registration(mpt_shell)

if(MPT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
