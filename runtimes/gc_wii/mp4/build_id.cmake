# MP4's build fingerprint (ADR 0009, amendment after P1-16: what build_id covers).
#
# One id, computed by mpt_mp4_build_id and used everywhere: the module's MPT_MP4_BUILD_ID (the save-state
# header field and the replay and golden-trail identity) and the asset-import recipe's
# runtime_build_version (the cache key). The module build (mp4/CMakeLists.txt) and the host build
# (mp4/host/CMakeLists.txt) call the same function with the same inputs, so a pinned recipe build gets
# the same id on both sides; the recipe's install step checks that it did.
#
# The id is "mp4-" and the first 16 hex digits of the SHA-256 of a canonical text with one line per
# component, in groups:
#   tree       every file the module build reads from runtimes/gc_wii, by content (sdk, gx, musyx, mp4:
#              CMake files and scripts, locks, src, include, tools); tests, docs and patches excluded
#   core       core/include, by content
#   patches    the Party Board, aurora and musyx patch sets, each in apply order
#   sources    every lock entry (name, commit, tree) with its state: pinned, unpinned:<fingerprint>
#              (from asset-import's resolver), locked (not resolved by the recipe) or unverified (a
#              developer build outside the recipe)
#   toolchain  compiler ID, version and target, build type, CMake version, the ADR 0008 section 4 flags
#   constants  GX backend, the recipe's module-build arguments, main-RAM size, fixed bases and spacing,
#              the Phase 1 gameplay boot flags
#   layout     kStateFormat (src/abi.cpp) and the ABI version (game_abi.h)
#
# It is content-based, never the git commit, so uncommitted edits count and two developers building
# the same commit get the same id. The manifest written next to the module lists every component with
# its hash, plus the repository commit and whether the tree was dirty (those two are not in the id).
#
# Script mode works too (cmake -P): tests/build_id_test.cmake checks that each group changes the id.
# The recipe builds the module in this configuration (recipe.cpp, through recipe_config.h).
set(MPT_MP4_RECIPE_BUILD_TYPE "Release")

include("${CMAKE_CURRENT_LIST_DIR}/../sdk/fp_flags.cmake")

# "clang <version> <target>" from `<compiler> --version`, as the recipe's check_toolchain reads it.
function(mpt_mp4_toolchain_of compiler out_var)
  if(NOT compiler OR NOT EXISTS "${compiler}")
    set(${out_var} "none" PARENT_SCOPE)
    return()
  endif()
  execute_process(COMMAND "${compiler}" --version OUTPUT_VARIABLE _out ERROR_QUIET RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0 OR NOT _out MATCHES "clang version ([0-9]+\\.[0-9]+\\.[0-9]+)")
    message(FATAL_ERROR "build_id: ${compiler} --version does not report a Clang version")
  endif()
  set(_version "${CMAKE_MATCH_1}")
  if(NOT _out MATCHES "Target: ([A-Za-z0-9_.-]+)")
    message(FATAL_ERROR "build_id: ${compiler} --version does not report a target")
  endif()
  set(${out_var} "clang ${_version} ${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

function(_mpt_mp4_build_id_group text_var group_var name)
  string(SHA256 _h "${${group_var}}")
  set(${text_var} "${${text_var}}group ${name} ${_h}\n${${group_var}}" PARENT_SCOPE)
endfunction()

# mpt_mp4_build_id(ID <var> [MANIFEST <var>] [INPUTS <var>]
#                  GC_WII_DIR <dir> CORE_INCLUDE_DIR <dir>
#                  COMPILER <path> | TOOLCHAIN "<clang version target>"
#                  BUILD_TYPE <type> GX_BACKEND <backend> MSVC_FRONTEND <bool>
#                  [RECIPE_ARGS_HASH <hash>] [SOURCE_STATES <name>=<state>...])
# INPUTS receives every file that went into the id, for CMAKE_CONFIGURE_DEPENDS.
function(mpt_mp4_build_id)
  cmake_parse_arguments(PARSE_ARGV 0 A ""
    "ID;MANIFEST;INPUTS;GC_WII_DIR;CORE_INCLUDE_DIR;COMPILER;TOOLCHAIN;BUILD_TYPE;GX_BACKEND;MSVC_FRONTEND;RECIPE_ARGS_HASH"
    "SOURCE_STATES")
  foreach(_req ID GC_WII_DIR CORE_INCLUDE_DIR GX_BACKEND)
    if(NOT A_${_req})
      message(FATAL_ERROR "mpt_mp4_build_id: ${_req} is required")
    endif()
  endforeach()
  get_filename_component(_gc "${A_GC_WII_DIR}" ABSOLUTE)
  get_filename_component(_core "${A_CORE_INCLUDE_DIR}" ABSOLUTE)
  set(_inputs "")

  # ---- tree ----
  file(GLOB_RECURSE _tree_files LIST_DIRECTORIES false
    "${_gc}/CMakeLists.txt"
    "${_gc}/sdk/CMakeLists.txt" "${_gc}/sdk/*.cmake" "${_gc}/sdk/src/*" "${_gc}/sdk/include/*"
    "${_gc}/gx/CMakeLists.txt" "${_gc}/gx/*.cmake" "${_gc}/gx/*.lock" "${_gc}/gx/src/*" "${_gc}/gx/include/*"
    "${_gc}/musyx/CMakeLists.txt" "${_gc}/musyx/*.cmake" "${_gc}/musyx/src/*" "${_gc}/musyx/include/*"
    "${_gc}/mp4/CMakeLists.txt" "${_gc}/mp4/*.cmake" "${_gc}/mp4/*.lock" "${_gc}/mp4/src/*" "${_gc}/mp4/include/*"
    "${_gc}/mp4/tools/*")
  list(SORT _tree_files)
  set(_tree "")
  foreach(_f IN LISTS _tree_files)
    file(SHA256 "${_f}" _h)
    file(RELATIVE_PATH _rel "${_gc}" "${_f}")
    string(APPEND _tree "tree ${_rel} ${_h}\n")
  endforeach()
  list(APPEND _inputs ${_tree_files})

  # ---- core ----
  file(GLOB_RECURSE _core_files LIST_DIRECTORIES false "${_core}/*")
  list(SORT _core_files)
  set(_core_text "")
  foreach(_f IN LISTS _core_files)
    file(SHA256 "${_f}" _h)
    file(RELATIVE_PATH _rel "${_core}" "${_f}")
    string(APPEND _core_text "core core/include/${_rel} ${_h}\n")
  endforeach()
  list(APPEND _inputs ${_core_files})

  # ---- patches, per set in apply order (each set is applied sorted by name) ----
  set(_patches "")
  foreach(_set "mp4/patches/partyboard" "gx/patches/aurora" "musyx/patches")
    file(GLOB _set_files LIST_DIRECTORIES false "${_gc}/${_set}/*.patch")
    list(SORT _set_files)
    foreach(_f IN LISTS _set_files)
      file(SHA256 "${_f}" _h)
      file(RELATIVE_PATH _rel "${_gc}" "${_f}")
      string(APPEND _patches "patch ${_rel} ${_h}\n")
    endforeach()
    list(APPEND _inputs ${_set_files})
  endforeach()

  # ---- sources ----
  set(_states "")
  foreach(_s IN LISTS A_SOURCE_STATES)
    if(_s MATCHES "^([^=]+)=(.+)$")
      set(_state_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    else()
      message(FATAL_ERROR "mpt_mp4_build_id: SOURCE_STATES entry '${_s}' is not name=state")
    endif()
  endforeach()
  set(_sources "")
  foreach(_lock "mp4/decomp.lock" "gx/aurora.lock" "gx/dawn.lock")
    if(NOT EXISTS "${_gc}/${_lock}")
      continue()
    endif()
    file(READ "${_gc}/${_lock}" _json)
    string(JSON _n LENGTH "${_json}" sources)
    if(_n EQUAL 0)
      continue()
    endif()
    math(EXPR _last "${_n} - 1")
    foreach(_i RANGE ${_last})
      string(JSON _name GET "${_json}" sources ${_i} name)
      string(JSON _commit GET "${_json}" sources ${_i} commit)
      string(JSON _treeid ERROR_VARIABLE _no_tree GET "${_json}" sources ${_i} tree)
      if(_no_tree)
        set(_treeid "-")
      endif()
      if(_lock STREQUAL "mp4/decomp.lock")
        if(DEFINED _state_${_name})
          set(_st "${_state_${_name}}")
        else()
          set(_st "unverified")
        endif()
      else()
        set(_st "locked")
      endif()
      string(APPEND _sources "source ${_lock} ${_name} ${_commit} ${_treeid} ${_st}\n")
    endforeach()
  endforeach()

  # ---- toolchain ----
  if(A_TOOLCHAIN)
    set(_tc "${A_TOOLCHAIN}")
  else()
    mpt_mp4_toolchain_of("${A_COMPILER}" _tc)
  endif()
  if(A_MSVC_FRONTEND)
    mpt_gc_fp_flags(_fp TRUE)
  else()
    mpt_gc_fp_flags(_fp FALSE)
  endif()
  string(JOIN " " _fp ${_fp})
  set(_toolchain "toolchain compiler ${_tc}\ntoolchain build_type ${A_BUILD_TYPE}\ntoolchain cmake ${CMAKE_VERSION}\n")
  string(APPEND _toolchain "toolchain fp_flags ${_fp}\n")

  # ---- constants ----
  include("${_gc}/mp4/layout.cmake")
  math(EXPR _spacing "${MPT_MP4_MODULE_BASE} - ${MPT_MP4_ARENA_BASE}" OUTPUT_FORMAT HEXADECIMAL)
  file(READ "${_gc}/mp4/include/mpt/mp4/game_abi.h" _abi_h)
  if(NOT _abi_h MATCHES "#define MPT_MP4_PHASE1_GAMEPLAY_FLAGS (0x[0-9A-Fa-f]+)u")
    message(FATAL_ERROR "build_id: no MPT_MP4_PHASE1_GAMEPLAY_FLAGS in game_abi.h")
  endif()
  set(_flags "${CMAKE_MATCH_1}")
  set(_constants "constant gx_backend ${A_GX_BACKEND}\nconstant recipe_args ${A_RECIPE_ARGS_HASH}\n")
  string(APPEND _constants "constant main_ram_bytes ${MPT_MP4_MAIN_RAM_BYTES}\n")
  string(APPEND _constants "constant module_base ${MPT_MP4_MODULE_BASE}\nconstant arena_base ${MPT_MP4_ARENA_BASE}\n")
  string(APPEND _constants "constant base_spacing ${_spacing}\nconstant phase1_gameplay_flags ${_flags}\n")

  # ---- layout versions ----
  if(NOT _abi_h MATCHES "#define MPT_MP4_ABI_VERSION ([0-9]+)u")
    message(FATAL_ERROR "build_id: no MPT_MP4_ABI_VERSION in game_abi.h")
  endif()
  set(_abi "${CMAKE_MATCH_1}")
  file(READ "${_gc}/mp4/src/abi.cpp" _abi_cpp)
  if(NOT _abi_cpp MATCHES "kStateFormat = ([0-9]+);")
    message(FATAL_ERROR "build_id: no kStateFormat in src/abi.cpp")
  endif()
  set(_layout "layout state_format ${CMAKE_MATCH_1}\nlayout abi_version ${_abi}\n")

  set(_text "mpt-mp4-build-id 1\n")
  _mpt_mp4_build_id_group(_text _tree tree)
  _mpt_mp4_build_id_group(_text _core_text core)
  _mpt_mp4_build_id_group(_text _patches patches)
  _mpt_mp4_build_id_group(_text _sources sources)
  _mpt_mp4_build_id_group(_text _toolchain toolchain)
  _mpt_mp4_build_id_group(_text _constants constants)
  _mpt_mp4_build_id_group(_text _layout layout)
  string(SHA256 _full "${_text}")
  string(SUBSTRING "${_full}" 0 16 _short)
  set(${A_ID} "mp4-${_short}" PARENT_SCOPE)
  if(A_MANIFEST)
    set(${A_MANIFEST} "build_id mp4-${_short}\nsha256 ${_full}\n${_text}" PARENT_SCOPE)
  endif()
  if(A_INPUTS)
    list(APPEND _inputs "${_gc}/mp4/layout.cmake" "${_gc}/mp4/build_id.cmake" "${_gc}/sdk/fp_flags.cmake")
    list(REMOVE_DUPLICATES _inputs)
    set(${A_INPUTS} "${_inputs}" PARENT_SCOPE)
  endif()
endfunction()

# The repository commit and whether the module's inputs differ from it: recorded in the manifest next
# to the id (ADR 0009 amendment point 5), never part of it. "none" outside a git checkout.
function(mpt_mp4_tree_state gc_wii_dir core_include_dir out_var)
  find_package(Git QUIET)
  set(_state "commit none\ndirty unknown\n")
  if(GIT_EXECUTABLE)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${gc_wii_dir}" rev-parse HEAD
                    OUTPUT_VARIABLE _head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _rc)
    if(_rc EQUAL 0)
      execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${gc_wii_dir}" status --porcelain -- . "${core_include_dir}"
                      OUTPUT_VARIABLE _dirty ERROR_QUIET RESULT_VARIABLE _rc2)
      if(_rc2 EQUAL 0 AND _dirty STREQUAL "")
        set(_state "commit ${_head}\ndirty no\n")
      elseif(_rc2 EQUAL 0)
        set(_state "commit ${_head}\ndirty yes\n")
      else()
        set(_state "commit ${_head}\ndirty unknown\n")
      endif()
    endif()
  endif()
  set(${out_var} "${_state}" PARENT_SCOPE)
endfunction()
