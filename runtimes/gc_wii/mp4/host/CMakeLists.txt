# runtimes/gc_wii/mp4/host: Mp4Runtime and its registration (P1-16). Host side: built with the tree's
# compiler, like the shell. It reaches the Clang-built game module only through its C ABI (ADR 0008,
# amendment after P1-06), so nothing here links the SDK, GX or game code.
include("${CMAKE_CURRENT_SOURCE_DIR}/../layout.cmake")

add_library(mpt_mp4_runtime STATIC
  src/mp4_runtime.cpp
  src/module_loader.cpp
  src/module_loader.h
  include/mpt/mp4/runtime.h)
add_library(mpt::mp4_runtime ALIAS mpt_mp4_runtime)
# ../include holds the module's ABI header (mpt/mp4/game_abi.h); the host uses nothing else from it.
target_include_directories(mpt_mp4_runtime PUBLIC include "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(mpt_mp4_runtime PUBLIC mpt::core PRIVATE ${CMAKE_DL_LIBS})
target_compile_definitions(mpt_mp4_runtime PRIVATE
  MPT_MP4_MODULE_BASE=${MPT_MP4_MODULE_BASE}ull MPT_MP4_ARENA_BASE=${MPT_MP4_ARENA_BASE}ull)
mpt_set_warnings(mpt_mp4_runtime)

# The adapter lives in adapters/mp4 (same owner) and needs the runtime target.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../../adapters/mp4" "${CMAKE_BINARY_DIR}/adapters/mp4")

# How the shell builds the game module for a dump: this tree's runtimes/gc_wii with Clang, and the GX
# layer's packages and backend as configured here.
set(MPT_MP4_RECIPE_GC_WII_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
cmake_path(NORMAL_PATH MPT_MP4_RECIPE_GC_WII_SOURCE_DIR)
set(MPT_MP4_RECIPE_CORE_INCLUDE_DIR "${MPT_CORE_INCLUDE_DIR}")
cmake_path(NORMAL_PATH MPT_MP4_RECIPE_CORE_INCLUDE_DIR)
# Clang for the module: this compiler when it is Clang, otherwise the recipe finds it on PATH.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(MPT_MP4_RECIPE_CLANG_C "${CMAKE_C_COMPILER}")
  set(MPT_MP4_RECIPE_CLANG_CXX "${CMAKE_CXX_COMPILER}")
elseif(WIN32)
  get_filename_component(_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  find_program(MPT_MP4_RECIPE_CLANG_CL NAMES clang-cl
    HINTS "${_cl_dir}/../../../../../Llvm/x64/bin" "$ENV{ProgramFiles}/LLVM/bin")
  set(MPT_MP4_RECIPE_CLANG_C "${MPT_MP4_RECIPE_CLANG_CL}")
  set(MPT_MP4_RECIPE_CLANG_CXX "${MPT_MP4_RECIPE_CLANG_CL}")
else()
  find_program(MPT_MP4_RECIPE_CLANG NAMES clang clang-18 clang-17)
  find_program(MPT_MP4_RECIPE_CLANGXX NAMES clang++ clang++-18 clang++-17)
  set(MPT_MP4_RECIPE_CLANG_C "${MPT_MP4_RECIPE_CLANG}")
  set(MPT_MP4_RECIPE_CLANG_CXX "${MPT_MP4_RECIPE_CLANGXX}")
endif()
if(CMAKE_GENERATOR MATCHES "Ninja")
  set(MPT_MP4_RECIPE_NINJA "${CMAKE_MAKE_PROGRAM}")
else()
  set(MPT_MP4_RECIPE_NINJA "")
endif()
set(_args "-DMPT_GX_BACKEND=${MPT_GX_BACKEND}" "-DMPT_DAWN_PREFIX=${MPT_DAWN_PREFIX}"
          "-DCMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO=Release" "-DCMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL=Release")
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
  list(APPEND _args "-DCMAKE_PREFIX_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()
# Test only: build the module with the layout probe (mp4/src/layout_probe.c): OFF, ON or rodata (see
# MPT_MP4_LAYOUT_PROBE there). Its trail must equal a normal module's; it has its own build ID through the
# arguments' hash, so its replays and save states never match a normal module's. Only a test build
# (MPT_BUILD_TESTS) may set it, and never a CI preset (MPT_WARNINGS_AS_ERRORS).
set(MPT_MP4_RECIPE_LAYOUT_PROBE OFF CACHE STRING "Test only: OFF, ON or rodata; the recipe builds the MP4 module with its layout probe")
set_property(CACHE MPT_MP4_RECIPE_LAYOUT_PROBE PROPERTY STRINGS OFF ON rodata)
if(NOT MPT_MP4_RECIPE_LAYOUT_PROBE STREQUAL "OFF" AND (NOT MPT_BUILD_TESTS OR MPT_WARNINGS_AS_ERRORS))
  message(FATAL_ERROR "MPT_MP4_RECIPE_LAYOUT_PROBE is test only: it needs MPT_BUILD_TESTS=ON and no CI preset")
endif()
if(MPT_MP4_RECIPE_LAYOUT_PROBE STREQUAL "ON" OR MPT_MP4_RECIPE_LAYOUT_PROBE STREQUAL "rodata")
  list(APPEND _args "-DMPT_MP4_LAYOUT_PROBE=${MPT_MP4_RECIPE_LAYOUT_PROBE}")
elseif(NOT MPT_MP4_RECIPE_LAYOUT_PROBE STREQUAL "OFF")
  message(FATAL_ERROR "MPT_MP4_RECIPE_LAYOUT_PROBE must be OFF, ON or rodata, not '${MPT_MP4_RECIPE_LAYOUT_PROBE}'")
endif()
# The arguments select a different module from the same tree, so they go into the build ID, but by a
# path-independent identity: two developers building the same commit get the same id (#55 review).
#  - MPT_DAWN_PREFIX: whether one is given. Dawn's pin is gx/dawn.lock, in the id's tree group, and the
#    backend is a constant.
#  - CMAKE_PREFIX_PATH (vcpkg's installed tree): the triplet, vcpkg.json and vcpkg-configuration.json,
#    which hold the packages and the registry baseline that fix their versions.
#  - Every other argument as it is.
# The paths themselves go only into the host's manifest (mp4_recipe_build_id.txt). The module build
# gets the hash as one more argument.
set(_args_text "")
set(_args_paths "")
foreach(_a IN LISTS _args)
  string(REPLACE "\\" "/" _a "${_a}")
  if(_a MATCHES "^-DMPT_DAWN_PREFIX=(.*)$")
    if(CMAKE_MATCH_1)
      string(APPEND _args_text "dawn_prefix set\n")
    else()
      string(APPEND _args_text "dawn_prefix unset\n")
    endif()
    string(APPEND _args_paths "path MPT_DAWN_PREFIX ${CMAKE_MATCH_1}\n")
  elseif(_a MATCHES "^-DCMAKE_PREFIX_PATH=(.*)$")
    string(APPEND _args_paths "path CMAKE_PREFIX_PATH ${CMAKE_MATCH_1}\n")
    string(APPEND _args_text "vcpkg triplet ${VCPKG_TARGET_TRIPLET}\n")
    foreach(_vf vcpkg.json vcpkg-configuration.json)
      set(_vp "${CMAKE_SOURCE_DIR}/${_vf}")
      if(EXISTS "${_vp}")
        file(SHA256 "${_vp}" _vh)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_vp}")
      else()
        set(_vh none)
      endif()
      string(APPEND _args_text "vcpkg ${_vf} ${_vh}\n")
    endforeach()
  else()
    string(APPEND _args_text "${_a}\n")
  endif()
endforeach()
string(SHA256 _args_hash "${_args_text}")
list(APPEND _args "-DMPT_MP4_RECIPE_ARGS_HASH=${_args_hash}")
set(MPT_MP4_RECIPE_ARGS "")
foreach(_a IN LISTS _args)
  string(REPLACE "\\" "/" _a "${_a}")
  string(APPEND MPT_MP4_RECIPE_ARGS "    \"${_a}\",\n")
endforeach()
foreach(_v GC_WII_SOURCE_DIR CORE_INCLUDE_DIR CLANG_C CLANG_CXX NINJA)
  string(REPLACE "\\" "/" MPT_MP4_RECIPE_${_v} "${MPT_MP4_RECIPE_${_v}}")
endforeach()
# The recipe's runtime version is the module's build ID (build_id.cmake, ADR 0009 amendment after
# P1-16), computed here from the same inputs the recipe's module build uses: this tree, core/include,
# the Clang the recipe runs, its build type, the GX backend and the arguments above, with every pin
# taken as pinned. A pinned recipe build therefore gets this exact id, which its install step checks.
# Every input is a configure dependency, so a content change re-runs this, not only a new file.
include("${CMAKE_CURRENT_SOURCE_DIR}/../build_id.cmake")
# The FP flag set follows the recipe's compiler driver: clang-cl takes the MSVC-style spelling.
get_filename_component(_recipe_cc_name "${MPT_MP4_RECIPE_CLANG_C}" NAME_WE)
if(_recipe_cc_name MATCHES "^clang-cl")
  set(_recipe_msvc_frontend TRUE)
else()
  set(_recipe_msvc_frontend FALSE)
endif()
mpt_mp4_build_id(ID MPT_MP4_RECIPE_RUNTIME_VERSION MANIFEST _mp4_manifest INPUTS _mp4_id_inputs
  GC_WII_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CORE_INCLUDE_DIR "${MPT_MP4_RECIPE_CORE_INCLUDE_DIR}"
  COMPILER "${MPT_MP4_RECIPE_CLANG_C}" BUILD_TYPE "${MPT_MP4_RECIPE_BUILD_TYPE}" GX_BACKEND "${MPT_GX_BACKEND}"
  MSVC_FRONTEND ${_recipe_msvc_frontend} RECIPE_ARGS_HASH "${_args_hash}"
  SOURCE_STATES partyboard=pinned musyx=pinned aurora=pinned)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_mp4_id_inputs})
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mp4_recipe_build_id.txt"
     CONTENT "${_mp4_manifest}recipe_args ${_args_hash}\n${_args_text}${_args_paths}" @ONLY)
message(STATUS "runtimes/gc_wii/mp4/host: the recipe builds MP4 module ${MPT_MP4_RECIPE_RUNTIME_VERSION}")
configure_file(recipe_config.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/recipe_config.h" @ONLY)

add_library(mpt_mp4_register STATIC src/register.cpp)
target_include_directories(mpt_mp4_register PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(mpt_mp4_register PUBLIC mpt::mp4_runtime mpt::mp4_adapter mpt::mp4_recipe mpt::shell_core)
mpt_set_warnings(mpt_mp4_register)
mpt_register_title(TARGET mpt_mp4_register FUNCTION mpt_register_mp4)
