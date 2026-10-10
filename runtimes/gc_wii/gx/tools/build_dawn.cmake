# Developer recipe: fetch Dawn from dawn.lock and build it into an install prefix (ADR 0007 section 6,
# ADR 0009 amendment after the P1-14 gate). Dawn is a host dependency; it is never built inside our tree.
#
#   cmake -DMPT_FETCH_TOOL=<build>/runtimes/gc_wii/gx/tools/mpt_fetch_sources[.exe]
#         -DMPT_SOURCE_CACHE=<repo>/external/sources
#         -DMPT_DAWN_PREFIX=<repo>/external/dawn
#         [-DMPT_DAWN_HEADERS_ONLY=ON]          # only the generated WebGPU headers (null backend)
#         [-DMPT_DAWN_BUILD_DIR=<dir>]          # default: <prefix>-build
#         [-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl]   # default: clang-cl / clang
#         [-DMPT_DAWN_JOBS=<n>]
#         -P runtimes/gc_wii/gx/tools/build_dawn.cmake
#
# Windows: run it from a Visual Studio x64 developer prompt. Keep the paths short: Dawn's object paths
# are long, and MSVC tools used by some steps do not support paths over 260 characters.
#
# Writes <prefix>/mpt_dawn_stamp.cmake, which runtimes/gc_wii/gx checks: the prefix must have been made
# from the current dawn.lock, and MPT_GX_BACKEND=dawn needs a full build, not headers only.
cmake_minimum_required(VERSION 3.25)

get_filename_component(_gx_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_lock "${_gx_dir}/dawn.lock")
foreach(_v MPT_FETCH_TOOL MPT_SOURCE_CACHE MPT_DAWN_PREFIX)
  if(NOT ${_v})
    message(FATAL_ERROR "build_dawn.cmake: set -D${_v}=... (see the header of this file)")
  endif()
endforeach()
if(NOT MPT_DAWN_BUILD_DIR)
  set(MPT_DAWN_BUILD_DIR "${MPT_DAWN_PREFIX}-build")
endif()
if(NOT MPT_DAWN_JOBS)
  cmake_host_system_information(RESULT MPT_DAWN_JOBS QUERY NUMBER_OF_LOGICAL_CORES)
endif()
if(NOT CMAKE_C_COMPILER)
  if(WIN32)
    set(CMAKE_C_COMPILER clang-cl)
    set(CMAKE_CXX_COMPILER clang-cl)
  else()
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
  endif()
endif()

# ---- 1. Fetch and verify every lock entry (commit, tree, DEPS cross-check). ----
file(MAKE_DIRECTORY "${MPT_DAWN_BUILD_DIR}")
set(_sources "${MPT_DAWN_BUILD_DIR}/mpt_dawn_sources.cmake")
string(TIMESTAMP _t0 "%s")
execute_process(
  COMMAND "${MPT_FETCH_TOOL}" --lock "${_lock}" --cache "${MPT_SOURCE_CACHE}" --out "${_sources}"
  RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "build_dawn.cmake: fetching dawn.lock failed (${_rc})")
endif()
include("${_sources}")
string(TIMESTAMP _t1 "%s")
math(EXPR _fetch_s "${_t1} - ${_t0}")
message(STATUS "Dawn sources fetched and verified in ${_fetch_s} s")

# Lock entry -> Dawn's directory variable. Every pinned dependency must be used here.
set(_dir_vars
  dawn_abseil_cpp=DAWN_ABSEIL_DIR
  dawn_jinja2=DAWN_JINJA2_DIR
  dawn_markupsafe=DAWN_MARKUPSAFE_DIR
  dawn_spirv_headers=DAWN_SPIRV_HEADERS_DIR
  dawn_spirv_tools=DAWN_SPIRV_TOOLS_DIR
  dawn_vulkan_headers=DAWN_VULKAN_HEADERS_DIR
  dawn_vulkan_utility_libraries=DAWN_VULKAN_UTILITY_LIBRARIES_DIR
  dawn_webgpu_headers=DAWN_WEBGPU_HEADERS_DIR)
set(_dawn_dir_args)
foreach(_pair IN LISTS _dir_vars)
  string(REPLACE "=" ";" _pair "${_pair}")
  list(GET _pair 0 _name)
  list(GET _pair 1 _var)
  if(NOT DEFINED MPT_SOURCE_${_name}_DIR)
    message(FATAL_ERROR "build_dawn.cmake: dawn.lock has no entry ${_name}")
  endif()
  set(${_var} "${MPT_SOURCE_${_name}_DIR}")
  list(APPEND _dawn_dir_args "-D${_var}=${MPT_SOURCE_${_name}_DIR}")
endforeach()
set(_dawn_src "${MPT_SOURCE_dawn_DIR}")

# Dawn's generator imports jinja2 and markupsafe from the *parent* of the given paths, so the
# package folders must be named after the packages. The cache names them by commit; stage copies.
set(_pydeps "${MPT_DAWN_BUILD_DIR}/pydeps")
file(REMOVE_RECURSE "${_pydeps}")
foreach(_pkg jinja2 markupsafe)
  string(TOUPPER "${_pkg}" _up)
  file(COPY "${DAWN_${_up}_DIR}/" DESTINATION "${_pydeps}/${_pkg}" PATTERN ".git" EXCLUDE)
  set(DAWN_${_up}_DIR "${_pydeps}/${_pkg}")
  list(FILTER _dawn_dir_args EXCLUDE REGEX "^-DDAWN_${_up}_DIR=")
  list(APPEND _dawn_dir_args "-DDAWN_${_up}_DIR=${_pydeps}/${_pkg}")
endforeach()
file(SHA256 "${_lock}" _lock_sha)

file(REMOVE_RECURSE "${MPT_DAWN_PREFIX}")
file(MAKE_DIRECTORY "${MPT_DAWN_PREFIX}/include")

if(MPT_DAWN_HEADERS_ONLY)
  # ---- 2a. Headers only: run Dawn's generator for the C and C++ WebGPU headers. ----
  find_program(_python NAMES python3 python REQUIRED)
  set(_gen "${MPT_DAWN_BUILD_DIR}/gen")
  file(REMOVE_RECURSE "${_gen}")
  execute_process(
    COMMAND "${_python}" -E -s "${_dawn_src}/generator/dawn_json_generator.py"
      --template-dir "${_dawn_src}/generator/templates"
      --root-dir "${_dawn_src}"
      --output-dir "${_gen}"
      --dawn-json "${_dawn_src}/src/dawn/dawn.json"
      --wire-json "${_dawn_src}/src/dawn/dawn_wire.json"
      --kotlin-json "${_dawn_src}/src/dawn/dawn_kotlin.json"
      --webgpu-kt-docs "${_dawn_src}/src/dawn/webgpu_kt_docs.json"
      --targets headers,cpp_headers
      --jinja2-path "${DAWN_JINJA2_DIR}"
      --markupsafe-path "${DAWN_MARKUPSAFE_DIR}"
    RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "build_dawn.cmake: Dawn's header generator failed (${_rc})")
  endif()
  file(COPY "${_gen}/include/" DESTINATION "${MPT_DAWN_PREFIX}/include")
  file(COPY "${_dawn_src}/include/webgpu/" DESTINATION "${MPT_DAWN_PREFIX}/include/webgpu"
       FILES_MATCHING PATTERN "*.h" PATTERN "webgpu_glfw.h" EXCLUDE)
  set(_mode headers)
else()
  # ---- 2b. Full build: shared monolithic webgpu_dawn, installed into the prefix. ----
  set(_build_type Release)
  string(TIMESTAMP _t2 "%s")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_dawn_src}" -B "${MPT_DAWN_BUILD_DIR}/dawn" -G Ninja
      -DCMAKE_BUILD_TYPE=${_build_type}
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}" "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DCMAKE_INSTALL_PREFIX=${MPT_DAWN_PREFIX}"
      -DDAWN_BUILD_MONOLITHIC_LIBRARY=SHARED
      -DDAWN_ENABLE_INSTALL=ON
      -DDAWN_FETCH_DEPENDENCIES=OFF
      -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF -DDAWN_BUILD_BENCHMARKS=OFF
      -DDAWN_BUILD_PROTOBUF=OFF
      -DDAWN_USE_GLFW=OFF -DDAWN_USE_X11=OFF -DDAWN_USE_WAYLAND=OFF
      -DDAWN_ENABLE_DESKTOP_GL=OFF -DDAWN_ENABLE_OPENGLES=OFF
      -DDAWN_ENABLE_VULKAN=ON -DDAWN_ENABLE_NULL=ON
      -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_CMD_TOOLS=OFF
      -DABSL_MSVC_STATIC_RUNTIME=OFF
      # vulkan-1.dll and d3dcompiler_47.dll load from System32; we redistribute neither.
      -DDAWN_FORCE_SYSTEM_COMPONENT_LOAD=ON
      ${_dawn_dir_args}
    RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "build_dawn.cmake: configuring Dawn failed (${_rc})")
  endif()
  string(TIMESTAMP _t3 "%s")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${MPT_DAWN_BUILD_DIR}/dawn" --target webgpu_dawn --parallel ${MPT_DAWN_JOBS}
    RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "build_dawn.cmake: building Dawn failed (${_rc})")
  endif()
  string(TIMESTAMP _t4 "%s")
  execute_process(COMMAND "${CMAKE_COMMAND}" --install "${MPT_DAWN_BUILD_DIR}/dawn" RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "build_dawn.cmake: installing Dawn failed (${_rc})")
  endif()
  math(EXPR _cfg_s "${_t3} - ${_t2}")
  math(EXPR _bld_s "${_t4} - ${_t3}")
  message(STATUS "Dawn configured in ${_cfg_s} s and built in ${_bld_s} s")
  set(_mode full)
endif()

set(_pinned TRUE)
foreach(_v IN LISTS _dir_vars ITEMS dawn=x)
  string(REGEX REPLACE "=.*" "" _name "${_v}")
  if(NOT MPT_SOURCE_${_name}_PINNED)
    set(_pinned FALSE)
  endif()
endforeach()
file(WRITE "${MPT_DAWN_PREFIX}/mpt_dawn_stamp.cmake"
  "# Written by runtimes/gc_wii/gx/tools/build_dawn.cmake. Do not edit.\n"
  "set(MPT_DAWN_STAMP_LOCK_SHA256 \"${_lock_sha}\")\n"
  "set(MPT_DAWN_STAMP_MODE \"${_mode}\")\n"
  "set(MPT_DAWN_STAMP_COMMIT \"${MPT_SOURCE_dawn_COMMIT}\")\n"
  "set(MPT_DAWN_STAMP_PINNED ${_pinned})\n"
  "set(MPT_DAWN_STAMP_COMPILER \"${CMAKE_CXX_COMPILER}\")\n")
message(STATUS "Dawn (${_mode}) installed in ${MPT_DAWN_PREFIX}")
