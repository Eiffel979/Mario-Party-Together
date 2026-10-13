# Tests for the GX layer (synthetic data only: our own display lists and textures, never game assets).
#
# Always (null renderer, no GPU):
#   gx_scenes_null              every scene runs through the front end and command processor
#   gx_scenes_null_repeat       a second process gives the same GX-side trail
# With MPT_GX_BACKEND=dawn (needs a GPU, lavapipe or Dawn's Null adapter):
#   gx_scenes_dawn, gx_scenes_dawn_again
#   gx_null_vs_dawn             G-c: identical __gx and FIFO-stream hashes, frame by frame
#   gx_dawn_repeatable          G-e: identical images from two processes (synchronous pipelines)
#   gx_dawn_probes              G-d: probe colours against golden_probes.txt with a tolerance
set(MPT_GX_TEST_API "automatic" CACHE STRING "GPU API for the dawn scene tests: automatic, d3d12, vulkan, d3d11, dawn_null")
option(MPT_GX_TEST_SOFTWARE_ADAPTER "Let the dawn scene tests use a software adapter (lavapipe on CI)" OFF)
set(_api_args --api ${MPT_GX_TEST_API})
if(MPT_GX_TEST_SOFTWARE_ADAPTER)
  list(APPEND _api_args --allow-software-adapter)
endif()
set(MPT_GX_TEST_GOLDEN "${CMAKE_CURRENT_SOURCE_DIR}/golden_probes.txt" CACHE FILEPATH
  "Golden probe colours for gx_dawn_probes (recorded on the reference machine, D3D12)")

find_package(xxHash CONFIG REQUIRED)
set(_out "${CMAKE_CURRENT_BINARY_DIR}/out")

function(_mpt_gx_scene_exe name renderer)
  add_executable(${name} scene_runner.cpp scenes.cpp)
  target_link_libraries(${name} PRIVATE ${renderer} mpt::gx_math_redirect xxHash::xxhash)
  target_include_directories(${name} PRIVATE "${MPT_GX_AURORA}/lib" "${MPT_GX_AURORA}/lib/dolphin/gx")
  target_compile_options(${name} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${MPT_GX_WARN_FLAGS}>")
endfunction()

_mpt_gx_scene_exe(mpt_gx_scenes_null mpt_gx_renderer_null)
add_test(NAME gx_scenes_null COMMAND mpt_gx_scenes_null --out "${_out}/null")
add_test(NAME gx_scenes_null_repeat COMMAND mpt_gx_scenes_null --out "${_out}/null_repeat")
add_test(NAME gx_null_repeatable
  COMMAND "${CMAKE_COMMAND}" "-DA=${_out}/null/results.txt" "-DB=${_out}/null_repeat/results.txt"
          "-DCOLUMNS=gx,stream,bytes" -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_results.cmake")
set_tests_properties(gx_scenes_null gx_scenes_null_repeat PROPERTIES FIXTURES_SETUP gx_null)
set_tests_properties(gx_null_repeatable PROPERTIES FIXTURES_REQUIRED gx_null)

if(MPT_GX_BACKEND STREQUAL "dawn")
  find_package(PNG REQUIRED)
  _mpt_gx_scene_exe(mpt_gx_scenes mpt_gx_renderer_dawn)
  target_link_libraries(mpt_gx_scenes PRIVATE PNG::PNG)
  target_compile_definitions(mpt_gx_scenes PRIVATE MPT_GX_TEST_PNG)
  mpt_gx_copy_runtime_dlls(mpt_gx_scenes)

  add_test(NAME gx_scenes_dawn COMMAND mpt_gx_scenes ${_api_args} --png --out "${_out}/dawn")
  add_test(NAME gx_scenes_dawn_again COMMAND mpt_gx_scenes ${_api_args} --out "${_out}/dawn_again")
  set_tests_properties(gx_scenes_dawn gx_scenes_dawn_again PROPERTIES FIXTURES_SETUP gx_dawn)
  add_test(NAME gx_null_vs_dawn
    COMMAND "${CMAKE_COMMAND}" "-DA=${_out}/null/results.txt" "-DB=${_out}/dawn/results.txt"
            "-DCOLUMNS=gx,stream,bytes" -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_results.cmake")
  set_tests_properties(gx_null_vs_dawn PROPERTIES FIXTURES_REQUIRED "gx_null;gx_dawn")
  add_test(NAME gx_dawn_repeatable
    COMMAND "${CMAKE_COMMAND}" "-DA=${_out}/dawn/results.txt" "-DB=${_out}/dawn_again/results.txt"
            "-DCOLUMNS=img" -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_results.cmake")
  set_tests_properties(gx_dawn_repeatable PROPERTIES FIXTURES_REQUIRED gx_dawn)
  if(NOT MPT_GX_TEST_API STREQUAL "dawn_null")
    # Dawn's Null adapter does not rasterize, so it has no colours to check.
    add_test(NAME gx_dawn_probes
      COMMAND mpt_gx_scenes ${_api_args} --out "${_out}/dawn_probes" --check-probes "${MPT_GX_TEST_GOLDEN}")
  endif()
endif()

# MTX golden values (ADR 0008 section 5 condition 2, amendment after P1-13 section 4).
find_package(doctest CONFIG REQUIRED)
add_executable(mpt_gx_mtx_tests test_mtx.cpp)
target_link_libraries(mpt_gx_mtx_tests PRIVATE mpt_gx_frontend mpt::gx_math_redirect doctest::doctest)
target_compile_options(mpt_gx_mtx_tests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${MPT_GX_WARN_FLAGS}>")
add_test(NAME gx_mtx_golden COMMAND mpt_gx_mtx_tests)

# The front end and MTX use no host libm transcendental or fma (ADR 0008 section 5).
get_filename_component(_mpt_gx_cc_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(MPT_GX_NM NAMES llvm-nm nm HINTS "${_mpt_gx_cc_dir}")
if(MPT_GX_NM)
  add_test(NAME gx_frontend_no_host_math
    COMMAND "${CMAKE_COMMAND}" "-DNM=${MPT_GX_NM}" "-DFILES=$<TARGET_FILE:mpt_gx_frontend>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_no_host_math.cmake")
elseif(MPT_REQUIRE_GX)
  message(FATAL_ERROR "runtimes/gc_wii/gx: llvm-nm/nm not found for the libm link check")
endif()
