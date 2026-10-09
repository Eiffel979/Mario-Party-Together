# The SDK replacement layer core (P1-13). Built only with Clang (see ../CMakeLists.txt).
add_library(mpt_gc_sdk STATIC
  src/ar.cpp
  src/card.cpp
  src/context.cpp
  src/dvd.cpp
  src/file_source.cpp
  src/gc_math.cpp
  src/heap.cpp
  src/mtx.cpp
  src/os.cpp
  src/sdk.cpp
  src/vi_pad.cpp
)
add_library(mpt::gc_sdk ALIAS mpt_gc_sdk)
target_include_directories(mpt_gc_sdk PUBLIC include "${MPT_CORE_INCLUDE_DIR}")
target_compile_features(mpt_gc_sdk PUBLIC cxx_std_20)

# Strict IEEE, no contraction, no fast-math, SSE2 baseline (no FMA3 assumed), ADR 0008 ??4.
# Applied PUBLIC so everything linked with the SDK (tests now, the game module later)
# uses the same floating-point model.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(MPT_GC_FP_FLAGS /clang:-ffp-contract=off /clang:-fno-fast-math /clang:-fno-unsafe-math-optimizations)
  set(MPT_GC_WARN_FLAGS /W4 /permissive- /utf-8 -Wno-gnu-zero-variadic-macro-arguments)
  if(MPT_WARNINGS_AS_ERRORS)
    list(APPEND MPT_GC_WARN_FLAGS /WX)
  endif()
else()
  set(MPT_GC_FP_FLAGS -ffp-contract=off -fno-fast-math -fno-unsafe-math-optimizations -msse2 -mfpmath=sse)
  set(MPT_GC_WARN_FLAGS -Wall -Wextra -Wpedantic -Wshadow)
  if(MPT_WARNINGS_AS_ERRORS)
    list(APPEND MPT_GC_WARN_FLAGS -Werror)
  endif()
endif()
target_compile_options(mpt_gc_sdk PUBLIC ${MPT_GC_FP_FLAGS} PRIVATE ${MPT_GC_WARN_FLAGS})

if(MPT_BUILD_TESTS)
  find_package(doctest CONFIG REQUIRED)
  # Synthetic data only: no game files, no disc images, no save images from a console.
  add_executable(mpt_gc_sdk_tests
    tests/main.cpp
    tests/harness.h
    tests/test_card.cpp
    tests/test_clock.cpp
    tests/test_dvd_ar.cpp
    tests/test_math.cpp
    tests/test_pad_vi.cpp
    tests/test_scheduler.cpp
    tests/test_state.cpp
  )
  target_link_libraries(mpt_gc_sdk_tests PRIVATE mpt::gc_sdk doctest::doctest)
  target_compile_options(mpt_gc_sdk_tests PRIVATE ${MPT_GC_WARN_FLAGS})
  add_test(NAME gc_sdk_unit COMMAND mpt_gc_sdk_tests)
endif()
