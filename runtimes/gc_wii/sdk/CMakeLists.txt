# The SDK replacement layer core (P1-13). Built only with Clang (see ../CMakeLists.txt).
add_library(mpt_gc_sdk STATIC
  src/ai.cpp
  src/ar.cpp
  src/card.cpp
  src/context.cpp
  src/dvd.cpp
  src/file_source.cpp
  src/gc_math.cpp
  src/heap.cpp
  src/layout.cpp
  src/os.cpp
  src/sdk.cpp
  src/thp.cpp
  src/vi_pad.cpp
)
add_library(mpt::gc_sdk ALIAS mpt_gc_sdk)
target_include_directories(mpt_gc_sdk PUBLIC include "${MPT_CORE_INCLUDE_DIR}")
target_compile_features(mpt_gc_sdk PUBLIC cxx_std_20)

# Strict IEEE, no contraction, no fast-math, SSE2 baseline (no FMA3 assumed), ADR 0008 §4.
# Applied PUBLIC so everything linked with the SDK (tests now, the game module later)
# uses the same floating-point model.
#
# Control-flow protection off: our context switch returns into other stacks, which a
# CET shadow stack rejects. Every image that hosts the SDK is linked /CETCOMPAT:NO on
# Windows (INTERFACE link option), and Sdk::create refuses to run if the process has
# user shadow stacks enabled anyway.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(MPT_GC_FP_FLAGS /clang:-ffp-contract=off /clang:-fno-fast-math /clang:-fno-unsafe-math-optimizations)
  set(MPT_GC_WARN_FLAGS /W4 /permissive- /utf-8 -Wno-gnu-zero-variadic-macro-arguments)
  if(MPT_WARNINGS_AS_ERRORS)
    list(APPEND MPT_GC_WARN_FLAGS /WX)
  endif()
  target_link_options(mpt_gc_sdk INTERFACE /CETCOMPAT:NO)
else()
  set(MPT_GC_FP_FLAGS -ffp-contract=off -fno-fast-math -fno-unsafe-math-optimizations -msse2 -mfpmath=sse
                      -fcf-protection=none)
  set(MPT_GC_WARN_FLAGS -Wall -Wextra -Wpedantic -Wshadow)
  if(MPT_WARNINGS_AS_ERRORS)
    list(APPEND MPT_GC_WARN_FLAGS -Werror)
  endif()
endif()
target_compile_options(mpt_gc_sdk PUBLIC ${MPT_GC_FP_FLAGS} PRIVATE ${MPT_GC_WARN_FLAGS})
set(MPT_GC_WARN_FLAGS "${MPT_GC_WARN_FLAGS}" PARENT_SCOPE)  # for ../musyx

# llvm-nm for the link-time fma check (ADR 0008 §5 condition 1).
get_filename_component(_mpt_gc_cc_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(MPT_GC_NM NAMES llvm-nm nm HINTS "${_mpt_gc_cc_dir}")

if(MPT_BUILD_TESTS)
  find_package(doctest CONFIG REQUIRED)

  # A translation unit that uses fmaf/std::fmaf through the redirect header, as game
  # code will. The nm check below proves it references mpt_fmaf, not the CRT's fmaf.
  add_library(mpt_gc_redirect_probe OBJECT tests/redirect_probe.cpp)
  target_link_libraries(mpt_gc_redirect_probe PRIVATE mpt::gc_sdk)
  # The same for the transcendental functions third-party C code calls (musyx).
  add_library(mpt_gc_libm_redirect_probe OBJECT tests/libm_redirect_probe.c)
  target_link_libraries(mpt_gc_libm_redirect_probe PRIVATE mpt::gc_sdk)

  # Synthetic data only: no game files, no disc images, no save images from a console.
  add_executable(mpt_gc_sdk_tests
    tests/main.cpp
    tests/harness.h
    tests/test_audio.cpp
    tests/test_card.cpp
    tests/test_clock.cpp
    tests/test_context.cpp
    tests/test_dvd_ar.cpp
    tests/test_fst.cpp
    tests/test_math.cpp
    tests/test_pad_vi.cpp
    tests/test_scheduler.cpp
    tests/test_state.cpp
    tests/test_thp.cpp
    tests/thp_encoder.h
    $<TARGET_OBJECTS:mpt_gc_redirect_probe>
    $<TARGET_OBJECTS:mpt_gc_libm_redirect_probe>
  )
  target_link_libraries(mpt_gc_sdk_tests PRIVATE mpt::gc_sdk doctest::doctest)
  target_compile_options(mpt_gc_sdk_tests PRIVATE ${MPT_GC_WARN_FLAGS})
  # Synthetic extracted discs are generated here at test time, never committed.
  target_compile_definitions(mpt_gc_sdk_tests PRIVATE MPT_GC_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}/test_tmp")
  add_test(NAME gc_sdk_unit COMMAND mpt_gc_sdk_tests)

  if(MPT_GC_NM)
    set(MPT_GC_NO_HOST_MATH_FILES "$<TARGET_FILE:mpt_gc_sdk>;$<TARGET_OBJECTS:mpt_gc_redirect_probe>;$<TARGET_OBJECTS:mpt_gc_libm_redirect_probe>")
    add_test(NAME gc_sdk_no_host_math
      COMMAND "${CMAKE_COMMAND}" "-DNM=${MPT_GC_NM}" "-DFILES=${MPT_GC_NO_HOST_MATH_FILES}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_no_host_math.cmake")
    set_tests_properties(gc_sdk_no_host_math PROPERTIES COMMAND_EXPAND_LISTS ON)
    # The check must catch plain host calls (no redirect): these are expected to fail.
    add_custom_target(mpt_gc_probes ALL)
    foreach(_probe fma powf log2)
      if(_probe STREQUAL "fma")
        set(_src tests/fma_negative_probe.cpp)
      else()
        set(_src tests/${_probe}_negative_probe.c)
      endif()
      add_library(mpt_gc_${_probe}_negative_probe OBJECT ${_src})
      target_link_libraries(mpt_gc_${_probe}_negative_probe PRIVATE mpt::gc_sdk)
      add_test(NAME gc_sdk_no_host_math_detects_${_probe}
        COMMAND "${CMAKE_COMMAND}" "-DNM=${MPT_GC_NM}" "-DFILES=$<TARGET_OBJECTS:mpt_gc_${_probe}_negative_probe>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_no_host_math.cmake")
      set_tests_properties(gc_sdk_no_host_math_detects_${_probe} PROPERTIES WILL_FAIL ON)
      add_dependencies(mpt_gc_probes mpt_gc_${_probe}_negative_probe)
    endforeach()
  elseif(MPT_REQUIRE_GC_WII)
    message(FATAL_ERROR "runtimes/gc_wii: llvm-nm/nm not found for the host-math link check")
  endif()
endif()
