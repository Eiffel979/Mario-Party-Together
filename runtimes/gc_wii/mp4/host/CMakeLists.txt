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
set(MPT_MP4_RECIPE_ARGS "")
foreach(_a IN LISTS _args)
  string(REPLACE "\\" "/" _a "${_a}")
  string(APPEND MPT_MP4_RECIPE_ARGS "    \"${_a}\",\n")
endforeach()
foreach(_v GC_WII_SOURCE_DIR CORE_INCLUDE_DIR CLANG_C CLANG_CXX NINJA)
  string(REPLACE "\\" "/" MPT_MP4_RECIPE_${_v} "${MPT_MP4_RECIPE_${_v}}")
endforeach()
# The cached module must be rebuilt whenever the code it is built from changes, so the recipe's runtime
# version is a hash of every file the module build reads from this tree.
set(_gc "${CMAKE_CURRENT_SOURCE_DIR}/../..")
file(GLOB_RECURSE _module_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false
  "${_gc}/CMakeLists.txt" "${_gc}/sdk/CMakeLists.txt" "${_gc}/sdk/src/*" "${_gc}/sdk/include/*"
  "${_gc}/gx/CMakeLists.txt" "${_gc}/gx/*.lock" "${_gc}/gx/src/*" "${_gc}/gx/include/*" "${_gc}/gx/patches/*"
  "${_gc}/musyx/CMakeLists.txt" "${_gc}/musyx/src/*" "${_gc}/musyx/include/*"
  "${_gc}/mp4/CMakeLists.txt" "${_gc}/mp4/*.cmake" "${_gc}/mp4/decomp.lock" "${_gc}/mp4/src/*" "${_gc}/mp4/include/*"
  "${_gc}/mp4/patches/*" "${_gc}/mp4/tools/*")
list(SORT _module_inputs)
set(_tree "")
foreach(_f IN LISTS _module_inputs)
  file(SHA256 "${_f}" _h)
  file(RELATIVE_PATH _rel "${_gc}" "${_f}")
  string(APPEND _tree "${_rel}=${_h};")
endforeach()
string(SHA256 _tree_hash "${_tree}")
string(SUBSTRING "${_tree_hash}" 0 16 _tree_hash)
set(MPT_MP4_RECIPE_RUNTIME_VERSION "mp4-module-${_tree_hash}")
configure_file(recipe_config.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/recipe_config.h" @ONLY)

add_library(mpt_mp4_register STATIC src/register.cpp)
target_include_directories(mpt_mp4_register PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(mpt_mp4_register PUBLIC mpt::mp4_runtime mpt::mp4_adapter mpt::mp4_recipe mpt::shell_core)
mpt_set_warnings(mpt_mp4_register)
mpt_register_title(TARGET mpt_mp4_register FUNCTION mpt_register_mp4)
