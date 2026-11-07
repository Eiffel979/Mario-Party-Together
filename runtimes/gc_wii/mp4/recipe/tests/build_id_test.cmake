# The MP4 build ID changes with every group of its inputs (ADR 0009, amendment after P1-16, point 3).
#
#   cmake -DGC_WII_DIR=<runtimes/gc_wii> -DCORE_INCLUDE_DIR=<core/include> -DWORK=<scratch dir>
#         -P build_id_test.cmake
#
# Copies the input set that build_id.cmake reports into WORK, checks that the copy gives the same id
# as the tree (so the reported set is complete), then changes one input of each group and checks that
# the id changes each time. Needs no compiler: the toolchain is given as text.
cmake_minimum_required(VERSION 3.25)
foreach(_v GC_WII_DIR CORE_INCLUDE_DIR WORK)
  if(NOT ${_v})
    message(FATAL_ERROR "build_id_test: -D${_v}=... is required")
  endif()
endforeach()
include("${GC_WII_DIR}/mp4/build_id.cmake")

set(_toolchain "clang 22.1.3 x86_64-test")
set(_build_type Release)
set(_backend null)
set(_args_hash 0123)
set(_states partyboard=pinned musyx=pinned aurora=pinned)
function(id_of gc core out_var)
  mpt_mp4_build_id(ID _id GC_WII_DIR "${gc}" CORE_INCLUDE_DIR "${core}" TOOLCHAIN "${_toolchain}"
    BUILD_TYPE "${_build_type}" GX_BACKEND "${_backend}" MSVC_FRONTEND FALSE RECIPE_ARGS_HASH "${_args_hash}"
    SOURCE_STATES ${_states})
  set(${out_var} "${_id}" PARENT_SCOPE)
endfunction()

# Copy every reported input.
mpt_mp4_build_id(ID _tree_id INPUTS _inputs GC_WII_DIR "${GC_WII_DIR}" CORE_INCLUDE_DIR "${CORE_INCLUDE_DIR}"
  TOOLCHAIN "${_toolchain}" BUILD_TYPE "${_build_type}" GX_BACKEND "${_backend}" MSVC_FRONTEND FALSE
  RECIPE_ARGS_HASH "${_args_hash}" SOURCE_STATES ${_states})
file(REMOVE_RECURSE "${WORK}")
set(_gc "${WORK}/runtimes/gc_wii")
set(_core "${WORK}/core/include")
get_filename_component(_src_gc "${GC_WII_DIR}" ABSOLUTE)
get_filename_component(_src_core "${CORE_INCLUDE_DIR}" ABSOLUTE)
foreach(_f IN LISTS _inputs)
  string(FIND "${_f}" "${_src_core}/" _in_core)
  if(_in_core EQUAL 0)
    file(RELATIVE_PATH _rel "${_src_core}" "${_f}")
    set(_dest "${_core}/${_rel}")
  else()
    file(RELATIVE_PATH _rel "${_src_gc}" "${_f}")
    set(_dest "${_gc}/${_rel}")
  endif()
  get_filename_component(_dir "${_dest}" DIRECTORY)
  file(MAKE_DIRECTORY "${_dir}")
  file(COPY_FILE "${_f}" "${_dest}" ONLY_IF_DIFFERENT)
endforeach()

id_of("${_gc}" "${_core}" _base)
if(NOT _base STREQUAL _tree_id)
  message(FATAL_ERROR "build_id_test: the copied inputs give ${_base}, the tree ${_tree_id}: the input set is incomplete")
endif()
id_of("${_gc}" "${_core}" _again)
if(NOT _again STREQUAL _base)
  message(FATAL_ERROR "build_id_test: the id is not stable (${_base}, then ${_again})")
endif()

set(_failures 0)
set(_ref "${_base}")
macro(expect_change what)
  id_of("${_gc}" "${_core}" _changed)
  if(_changed STREQUAL _ref)
    message(SEND_ERROR "build_id_test: changing ${what} kept the id ${_ref}")
    math(EXPR _failures "${_failures} + 1")
  else()
    message(STATUS "build_id_test: ${what}: ${_ref} -> ${_changed}")
  endif()
endmacro()

# Puts a copied input back from the tree (file(WRITE) would change its line endings on Windows).
macro(restore path)
  file(RELATIVE_PATH _r "${WORK}" "${path}")
  if(_r MATCHES "^core/include/(.*)$")
    file(COPY_FILE "${_src_core}/${CMAKE_MATCH_1}" "${path}")
  else()
    string(REGEX REPLACE "^runtimes/gc_wii/" "" _r "${_r}")
    file(COPY_FILE "${_src_gc}/${_r}" "${path}")
  endif()
endmacro()

# One byte more at the end of a file of a group (file(APPEND) adds exactly that byte), then the original.
macro(change_file group path)
  file(APPEND "${path}" " ")
  expect_change("${group} (${path})")
  restore("${path}")
endmacro()

# A targeted edit of a file: the reference is the same file rewritten unchanged, so only the edit counts.
macro(change_text what path from to)
  file(READ "${path}" _orig)
  string(REGEX REPLACE "${from}" "${to}" _edited "${_orig}")
  if(_edited STREQUAL _orig)
    message(FATAL_ERROR "build_id_test: '${from}' is not in ${path}")
  endif()
  file(WRITE "${path}" "${_orig}")
  id_of("${_gc}" "${_core}" _ref)
  file(WRITE "${path}" "${_edited}")
  expect_change("${what}")
  restore("${path}")
  set(_ref "${_base}")
endmacro()

# A file group's first file, by name.
macro(first_file out_var pattern)
  file(GLOB_RECURSE _matches LIST_DIRECTORIES false "${pattern}")
  list(SORT _matches)
  list(GET _matches 0 ${out_var})
endmacro()

change_file(tree "${_gc}/sdk/src/sdk.cpp")
change_file(tree "${_gc}/mp4/tools/isolate_rel.cmake")
first_file(_core_file "${_core}/*")
change_file(core "${_core_file}")
first_file(_patch "${_gc}/mp4/patches/partyboard/*.patch")
change_file(patches "${_patch}")
first_file(_patch "${_gc}/gx/patches/aurora/*.patch")
change_file(patches "${_patch}")

# sources: a pin's state (an unpinned override), and a lock entry's commit.
set(_states partyboard=pinned musyx=unpinned:tree:0000 aurora=pinned)
expect_change("sources (musyx unpinned)")
set(_states partyboard=pinned musyx=pinned aurora=pinned)
change_text("sources (partyboard commit)" "${_gc}/mp4/decomp.lock"
            "98ac07fd1e8f7ab938bbb6fe75789fad99419010" "98ac07fd1e8f7ab938bbb6fe75789fad99419011")

# toolchain
set(_toolchain "clang 22.1.4 x86_64-test")
expect_change("toolchain (compiler version)")
set(_toolchain "clang 22.1.3 x86_64-test")
set(_build_type RelWithDebInfo)
expect_change("toolchain (build type)")
set(_build_type Release)

# constants
set(_backend dawn)
expect_change("constants (GX backend)")
set(_backend null)
set(_args_hash 0124)
expect_change("constants (recipe arguments)")
set(_args_hash 0123)
change_text("constants (main RAM)" "${_gc}/mp4/layout.cmake" "MAIN_RAM_BYTES \"[0-9]+\"" "MAIN_RAM_BYTES \"125829120\"")
change_text("constants (module base)" "${_gc}/mp4/layout.cmake" "MODULE_BASE \"0x[0-9a-fA-F]+\"" "MODULE_BASE \"0x600000000000\"")

# layout versions
change_text("layout (kStateFormat)" "${_gc}/mp4/src/abi.cpp" "kStateFormat = ([0-9]+);" "kStateFormat = 99;")
change_text("layout (ABI version)" "${_gc}/mp4/include/mpt/mp4/game_abi.h" "MPT_MP4_ABI_VERSION ([0-9]+)u" "MPT_MP4_ABI_VERSION 99u")

id_of("${_gc}" "${_core}" _restored)
if(NOT _restored STREQUAL _base)
  message(FATAL_ERROR "build_id_test: restoring every input gave ${_restored}, not ${_base}")
endif()
if(_failures GREATER 0)
  message(FATAL_ERROR "build_id_test: ${_failures} change(s) kept the id")
endif()
message(STATUS "build_id_test: every group changes the id")
