# MusyX, the MP4 sound library, built from the pinned upstream source (ADR 0007 §4,
# ADR 0009 and its amendment after P1-13b) and attached to the SDK as a pull-mode
# AudioEngine. Owner: runtime-gc-wii.
#
# The source is never committed. Fetch it with asset-import's verified fetcher, for
# example:
#   mpt_fetch_sources --lock runtimes/gc_wii/mp4/decomp.lock --cache <MPT_SOURCE_CACHE>
# which puts it at <MPT_SOURCE_CACHE>/musyx/<locked commit>, the default MPT_MUSYX_DIR
# (the same layout the GX layer uses for aurora). Without it this target is skipped,
# unless MPT_REQUIRE_MUSYX is ON.
#
# What is linked: the runtime sources upstream's own CMakeLists lists, with the null
# host backend (hw_pc_null.c) instead of the SDL one, and without the debugger. So no
# thread, mutex or condition variable is on any linked path; rendering happens only
# when the SDK's AI tick calls sndPCRender inside stepFrame.
set(MPT_MUSYX_DIR "" CACHE PATH "musyx checkout at the commit in runtimes/gc_wii/mp4/decomp.lock")
option(MPT_REQUIRE_MUSYX "Fail the configure if MPT_MUSYX_DIR is not a pinned musyx checkout" OFF)
option(MPT_MUSYX_ALLOW_UNPINNED "Build a musyx tree that is not at the locked commit (development only)" OFF)
# The MusyX version MP4 was built with selects struct layouts and behaviour. 2.0.0 is
# upstream's default; P1-15 confirms it against Party Board's msm build.
set(MPT_MUSYX_VERSION "2.0.0" CACHE STRING "MUSY_VERSION major.minor.patch")

set(_lock "${CMAKE_CURRENT_SOURCE_DIR}/../mp4/decomp.lock")
file(READ "${_lock}" _lock_json)
string(JSON _n LENGTH "${_lock_json}" sources)
math(EXPR _last "${_n} - 1")
set(MPT_MUSYX_LOCKED_COMMIT "")
foreach(_i RANGE ${_last})
  string(JSON _name GET "${_lock_json}" sources ${_i} name)
  if(_name STREQUAL "musyx")
    string(JSON MPT_MUSYX_LOCKED_COMMIT GET "${_lock_json}" sources ${_i} commit)
  endif()
endforeach()
if(NOT MPT_MUSYX_LOCKED_COMMIT)
  message(FATAL_ERROR "runtimes/gc_wii/musyx: no musyx entry in ${_lock}")
endif()

if(NOT MPT_MUSYX_DIR)
  if(NOT MPT_SOURCE_CACHE)
    get_filename_component(_repo "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
    set(MPT_SOURCE_CACHE "${_repo}/external/sources")
  endif()
  set(MPT_MUSYX_DIR "${MPT_SOURCE_CACHE}/musyx/${MPT_MUSYX_LOCKED_COMMIT}")
endif()
if(NOT MPT_MUSYX_DIR OR NOT EXISTS "${MPT_MUSYX_DIR}/include/musyx/musyx.h")
  if(MPT_REQUIRE_MUSYX)
    message(FATAL_ERROR "runtimes/gc_wii/musyx: MPT_MUSYX_DIR is not a musyx checkout (MPT_REQUIRE_MUSYX is ON). "
                        "Fetch runtimes/gc_wii/mp4/decomp.lock with mpt_fetch_sources and set MPT_MUSYX_DIR.")
  endif()
  message(STATUS "runtimes/gc_wii/musyx: MPT_MUSYX_DIR not set; MusyX is not built")
  return()
endif()

# The checkout must be at the locked commit (the fetcher also checked its tree).
find_package(Git QUIET)
set(_head "")
if(GIT_FOUND AND EXISTS "${MPT_MUSYX_DIR}/.git")
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${MPT_MUSYX_DIR}" rev-parse HEAD
                  OUTPUT_VARIABLE _head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${MPT_MUSYX_DIR}" status --porcelain --untracked-files=no
                  OUTPUT_VARIABLE _dirty OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(_dirty)
    set(_head "${_head}-dirty")
  endif()
endif()
if(NOT _head STREQUAL MPT_MUSYX_LOCKED_COMMIT)
  if(NOT MPT_MUSYX_ALLOW_UNPINNED)
    message(FATAL_ERROR "runtimes/gc_wii/musyx: ${MPT_MUSYX_DIR} is at '${_head}', the lock says "
                        "${MPT_MUSYX_LOCKED_COMMIT}. Refetch, or set MPT_MUSYX_ALLOW_UNPINNED for development.")
  endif()
  message(WARNING "runtimes/gc_wii/musyx: building an UNPINNED musyx tree ('${_head}')")
endif()
message(STATUS "runtimes/gc_wii/musyx: building MusyX ${MPT_MUSYX_VERSION} from ${MPT_MUSYX_DIR} (${_head})")

set(_rt "${MPT_MUSYX_DIR}/src/musyx/runtime")
# Upstream's runtime list at the pin, minus hw_pc_sdl.c (threaded SDL device, never
# used) and minus the files compiled through our wrappers below.
set(_musyx_sources
  seq.c synth.c seq_api.c snd_synthapi.c synthmacros.c synthvoice.c
  synth_ac.c synth_adsr.c synth_vsamples.c synth_dbtab.c s_data.c hw_dspctrl.c hw_volconv.c
  snd3d.c snd_init.c snd_math.c snd_service.c hardware.c hw_aramdma.c hw_dolphin.c
  hw_pc_output.c hw_pc_rate.c hw_pc_vsample.c hw_pc_song.c hw_pc_samples.c hw_pc_assets.c
  hw_pc_decode.c hw_pc.c hw_pc_null.c hw_lib_dolphin.c
  CheapReverb/creverb_fx.c CheapReverb/creverb.c StdReverb/reverb_fx.c StdReverb/reverb.c
  Delay/delay_fx.c Chorus/chorus_fx.c)
# Compiled through wrappers that #include them unchanged: snd_midictrl.c and stream.c,
# so the engine hash can read their file-scope state by value; synthdata.c, so
# sndConvert32BitSDIRTo64BitSDIR allocates through the game's hooks (ADR 0008 §7).
set(_wrapped snd_midictrl.c stream.c synthdata.c)
# hw_memory.c is replaced by src/musyx_memory.c (no host-heap fallback).
set(_abs "")
foreach(_s IN LISTS _musyx_sources _wrapped)
  if(NOT EXISTS "${_rt}/${_s}")
    message(FATAL_ERROR "runtimes/gc_wii/musyx: ${_rt}/${_s} is missing at this commit; update the source list")
  endif()
endforeach()
foreach(_s IN LISTS _musyx_sources)
  list(APPEND _abs "${_rt}/${_s}")
endforeach()

string(REPLACE "." ";" _ver "${MPT_MUSYX_VERSION}")
list(GET _ver 0 _vmaj)
list(GET _ver 1 _vmin)
list(GET _ver 2 _vpat)

# mpt_musyx_core: musyx itself plus its C glue (all C, all game-module code).
add_library(mpt_musyx_core STATIC
  ${_abs}
  src/wrap_snd_midictrl.c
  src/wrap_stream.c
  src/musyx_memory.c
  src/wrap_synthdata.c
  src/musyx_glue.c
  src/musyx_state_markers.c
  src/musyx_debugger_stubs.c
  src/musyx_glue.h
  src/musyx_state_section.h)
# musyx's headers are third-party code: SYSTEM, so their K&R-style "()" declarations
# (-Wstrict-prototypes under Linux Clang 18) and anything else in them never trip our
# warnings. Our own glue, adapter and tests keep the full warning set and -Werror.
# SYSTEM PUBLIC propagates as a system include to everything that links the core.
target_include_directories(mpt_musyx_core SYSTEM PUBLIC "${MPT_MUSYX_DIR}/include" PRIVATE "${_rt}")
target_include_directories(mpt_musyx_core PRIVATE src)
target_link_libraries(mpt_musyx_core PUBLIC mpt::gc_sdk)
set_target_properties(mpt_musyx_core PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_definitions(mpt_musyx_core PUBLIC
  MUSY_TARGET=0  # MUSY_TARGET_PC
  MUSY_VERSION_MAJOR=${_vmaj} MUSY_VERSION_MINOR=${_vmin} MUSY_VERSION_PATCH=${_vpat}
  PRIVATE NDEBUG)
# musyx uses M_PI (hw_pc.c resampler table, hardware.c), which is not ISO C: the
# UCRT defines it only with _USE_MATH_DEFINES and glibc only in its _DEFAULT_SOURCE/
# _GNU_SOURCE/_XOPEN_SOURCE modes, which our strict C11 build does not enable.
# We define it ourselves, on every OS, with the exact token sequence both C
# libraries use. So the value (and the resampler table and hash it feeds) is
# identical everywhere, a later identical #define in <math.h> is a benign
# redefinition, and no other declaration or feature macro changes: unlike
# _DEFAULT_SOURCE, which would expose BSD/SVID/POSIX declarations to every musyx
# unit on Linux only, or _USE_MATH_DEFINES, which works only on Windows.
target_compile_definitions(mpt_musyx_core PRIVATE "M_PI=3.14159265358979323846")

# mpt_musyx: the C++ AudioEngine adapter (host-side setup: section bounds, reset).
add_library(mpt_musyx STATIC src/musyx_engine.cpp include/mpt/gc/musyx_engine.h)
add_library(mpt::musyx ALIAS mpt_musyx)
target_include_directories(mpt_musyx PUBLIC include PRIVATE src)
target_link_libraries(mpt_musyx PUBLIC mpt_musyx_core)
target_compile_options(mpt_musyx PRIVATE ${MPT_GC_WARN_FLAGS})

# Every musyx translation unit gets the libm redirect (ADR 0008 §5) and the state
# section pragmas (save states capture its statics). The marker and C++ glue files
# are compiled without the section header.
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(_force /FImusyx_state_section.h)
  set(_quiet /w)
else()
  set(_force -include musyx_state_section.h)
  set(_quiet -w)
endif()
set(_musyx_tus ${_abs} src/wrap_snd_midictrl.c src/wrap_stream.c src/wrap_synthdata.c src/musyx_memory.c)
set_source_files_properties(${_musyx_tus} PROPERTIES COMPILE_OPTIONS "${_force};${_quiet}")
# The glue reads musyx headers without being upstream code: our warnings apply.
set_source_files_properties(src/musyx_glue.c src/musyx_state_markers.c src/musyx_debugger_stubs.c
  PROPERTIES COMPILE_OPTIONS "${MPT_GC_WARN_FLAGS}")

if(MPT_BUILD_TESTS)
  find_package(doctest CONFIG REQUIRED)
  add_executable(mpt_musyx_tests tests/main.cpp tests/test_musyx.cpp tests/synthetic_sound.h)
  target_link_libraries(mpt_musyx_tests PRIVATE mpt::musyx doctest::doctest)
  target_include_directories(mpt_musyx_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/tests")
  target_compile_options(mpt_musyx_tests PRIVATE ${MPT_GC_WARN_FLAGS})
  add_test(NAME gc_musyx_unit COMMAND mpt_musyx_tests)
  if(MPT_GC_NM)
    # ADR 0008 §5 and merge condition (b): over every musyx object we link.
    add_test(NAME gc_musyx_no_host_math
      COMMAND "${CMAKE_COMMAND}" "-DNM=${MPT_GC_NM}" "-DFILES=$<TARGET_FILE:mpt_musyx_core>"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/tests/check_no_host_math.cmake")
    add_test(NAME gc_musyx_no_host_services
      COMMAND "${CMAKE_COMMAND}" "-DNM=${MPT_GC_NM}" "-DFILES=$<TARGET_FILE:mpt_musyx_core>"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/check_no_host_services.cmake")
  endif()
endif()
