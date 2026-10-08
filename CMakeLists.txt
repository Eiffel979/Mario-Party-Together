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

find_package(SDL3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# asset-import first: core/ reads MPT_KNOWN_DUMPS_JSON from it.
add_subdirectory(asset-import)
add_subdirectory(core)

if(MPT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
