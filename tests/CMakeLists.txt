find_package(doctest CONFIG REQUIRED)

add_executable(mpt_unit_tests
  unit/main.cpp
  unit/test_asset_import.cpp
  unit/test_build.cpp
  unit/test_contract_init.cpp
  unit/test_core.cpp
  unit/test_determinism.cpp
  unit/test_determinism_harness.cpp
  unit/test_event_routing.cpp
  unit/test_dump_lane.cpp
  unit/test_dump_lane_paths.cpp
  unit/test_dump_lane_png.cpp
  unit/test_gc_disc.cpp
  unit/test_fake_adapter.cpp
  unit/test_fake_runtime_v1.cpp
  unit/test_golden_trail.cpp
  unit/test_replay.cpp
  unit/test_runtime_status.cpp
  unit/test_session.cpp
  unit/test_shell.cpp
  unit/test_state_hasher.cpp
)
target_link_libraries(mpt_unit_tests PRIVATE mpt::core mpt::asset_import mpt::shell_core doctest::doctest)
target_compile_definitions(mpt_unit_tests PRIVATE
  MPT_KNOWN_DUMPS_JSON="${MPT_KNOWN_DUMPS_JSON}"
  MPT_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
mpt_set_warnings(mpt_unit_tests)

# MP4's host side (Mp4Runtime unbooted, Mp4Adapter, the ABI header) needs no game module or dump.
if(TARGET mpt::mp4_adapter)
  target_sources(mpt_unit_tests PRIVATE unit/test_mp4_host.cpp)
  target_link_libraries(mpt_unit_tests PRIVATE mpt::mp4_adapter)
endif()

add_test(NAME unit COMMAND mpt_unit_tests)

# Runtime conformance suite (P1-20c) against FakeRuntime and FakeAdapter, plus self-tests
# that every check catches the bug it is meant to catch. Mp4Runtime reuses the same
# checks on the dump lane (P1-16, P1-20d).
add_executable(mpt_conformance_tests
  conformance/main.cpp
  conformance/test_conformance_fake.cpp
  conformance/test_conformance_selftest.cpp
)
target_link_libraries(mpt_conformance_tests PRIVATE mpt::core doctest::doctest)
mpt_set_warnings(mpt_conformance_tests)
add_test(NAME conformance COMMAND mpt_conformance_tests)

# The MP4 host's module loader on stub modules (mp4_loader/): each stub is linked at the fixed image base
# exactly like the game module (mpt_mp4_fixed_base_link, runtimes/gc_wii/mp4/layout.cmake), so this
# checks the fixed base on Windows and Linux without game code or a dump. Its own executable, because
# the loader keeps one module per process.
if(TARGET mpt_mp4_runtime)
  include("${PROJECT_SOURCE_DIR}/runtimes/gc_wii/mp4/layout.cmake")
  # old_base: a v1.1 stub at the module base before ADR 0008's 2026-11-07 ruling. cross: a v1.1 stub
  # whose image crosses a 4 GiB boundary (linked 64 KiB below one, with a larger image).
  set(_mp4_stub_cross_base "0x5B97FFFF0000")
  foreach(_stub v11 v10 no_export old_base cross)
    add_library(mpt_mp4_stub_${_stub} MODULE mp4_loader/stub_module.cpp)
    target_include_directories(mpt_mp4_stub_${_stub} PRIVATE "${PROJECT_SOURCE_DIR}/runtimes/gc_wii/mp4/include")
    set_target_properties(mpt_mp4_stub_${_stub} PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
    if(_stub STREQUAL "old_base")
      mpt_mp4_fixed_base_link(mpt_mp4_stub_${_stub} BASE 0x500000000000)
    elseif(_stub STREQUAL "cross")
      mpt_mp4_fixed_base_link(mpt_mp4_stub_${_stub} BASE ${_mp4_stub_cross_base})
    else()
      mpt_mp4_fixed_base_link(mpt_mp4_stub_${_stub})
    endif()
    mpt_set_warnings(mpt_mp4_stub_${_stub})
  endforeach()
  target_compile_definitions(mpt_mp4_stub_v11 PRIVATE MPT_STUB_API_SIZE=MPT_MP4_API_SIZE_V1_1)
  target_compile_definitions(mpt_mp4_stub_v10 PRIVATE MPT_STUB_API_SIZE=MPT_MP4_API_SIZE_V1_0)
  target_compile_definitions(mpt_mp4_stub_no_export PRIVATE MPT_STUB_NO_EXPORT)
  target_compile_definitions(mpt_mp4_stub_old_base PRIVATE MPT_STUB_API_SIZE=MPT_MP4_API_SIZE_V1_1)
  target_compile_definitions(mpt_mp4_stub_cross PRIVATE MPT_STUB_API_SIZE=MPT_MP4_API_SIZE_V1_1 MPT_STUB_BIG_IMAGE)

  add_executable(mpt_mp4_loader_tests unit/main.cpp mp4_loader/test_module_loader.cpp)
  target_link_libraries(mpt_mp4_loader_tests PRIVATE mpt::mp4_runtime doctest::doctest ${CMAKE_DL_LIBS})
  target_compile_definitions(mpt_mp4_loader_tests PRIVATE
    MPT_MP4_MODULE_BASE=${MPT_MP4_MODULE_BASE}ull
    "MPT_STUB_V11=\"$<TARGET_FILE:mpt_mp4_stub_v11>\""
    "MPT_STUB_V10=\"$<TARGET_FILE:mpt_mp4_stub_v10>\""
    "MPT_STUB_NO_EXPORT=\"$<TARGET_FILE:mpt_mp4_stub_no_export>\""
    "MPT_STUB_OLD_BASE=\"$<TARGET_FILE:mpt_mp4_stub_old_base>\""
    "MPT_STUB_CROSS=\"$<TARGET_FILE:mpt_mp4_stub_cross>\""
    MPT_STUB_CROSS_BASE=${_mp4_stub_cross_base}ull)
  add_dependencies(mpt_mp4_loader_tests mpt_mp4_stub_v11 mpt_mp4_stub_v10 mpt_mp4_stub_no_export
                   mpt_mp4_stub_old_base mpt_mp4_stub_cross)
  mpt_set_warnings(mpt_mp4_loader_tests)
  add_test(NAME mp4_module_loader COMMAND mpt_mp4_loader_tests)
endif()

# The .mptr validator (ADR 0006) must reject a file that is not a replay.
add_test(NAME replay_check_rejects_non_replay
  COMMAND mpt_replay_check --fixture "${CMAKE_CURRENT_SOURCE_DIR}/golden/replay_v0_synthetic.mptr.hex")
set_tests_properties(replay_check_rejects_non_replay PROPERTIES PASS_REGULAR_EXPRESSION "not an \\.mptr file")

# The dump lane (P1-20d, ADR 0006): manual and local only. Skipped (exit 77) unless
# MPT_DUMP_DIR is set; see tests/dump_lane/README.md. Run it with: ctest -L dump
# It plays every real title the shell registers (P1-16), building each from the dump with asset-import.
add_executable(mpt_dump_lane dump_lane/main.cpp dump_lane/crash_report.h dump_lane/path_guard.h dump_lane/real_titles.h dump_lane/png.h dump_lane/watch.h)
target_link_libraries(mpt_dump_lane PRIVATE mpt::core mpt::asset_import mpt::shell_core)
target_compile_definitions(mpt_dump_lane PRIVATE
  MPT_REPLAYS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/replays"
  MPT_REPO_ROOT="${PROJECT_SOURCE_DIR}"
  MPT_KNOWN_DUMPS_JSON="${MPT_KNOWN_DUMPS_JSON}")
mpt_generate_title_registration(mpt_dump_lane)
if(TARGET mpt_mp4_runtime)
  target_link_libraries(mpt_dump_lane PRIVATE mpt::mp4_runtime)
  target_compile_definitions(mpt_dump_lane PRIVATE MPT_DUMP_LANE_MP4)
  # The MP4 module's layout probe mode, for --layout-probe (dump_lane/layout_perturbation.cmake).
  if(DEFINED MPT_MP4_RECIPE_LAYOUT_PROBE)
    target_compile_definitions(mpt_dump_lane PRIVATE "MPT_DUMP_LANE_LAYOUT_PROBE=\"${MPT_MP4_RECIPE_LAYOUT_PROBE}\"")
  endif()
  target_link_libraries(mpt_dump_lane PRIVATE mpt::mp4_adapter)
endif()
mpt_set_warnings(mpt_dump_lane)
add_test(NAME dump_lane_replays COMMAND mpt_dump_lane)
set_tests_properties(dump_lane_replays PROPERTIES LABELS dump SKIP_RETURN_CODE 77)
# MP4 layout perturbation (dump lane): only in a host build with MPT_MP4_RECIPE_LAYOUT_PROBE set, against a
# normal build's lane named by MPT_DUMP_LANE_LAYOUT_REFERENCE. See dump_lane/layout_perturbation.cmake.
if(TARGET mpt_mp4_runtime AND DEFINED MPT_MP4_RECIPE_LAYOUT_PROBE AND NOT MPT_MP4_RECIPE_LAYOUT_PROBE STREQUAL "OFF")
  set(MPT_DUMP_LANE_LAYOUT_REFERENCE "" CACHE FILEPATH "Test only: a normal (no layout probe) build's mpt_dump_lane")
  add_test(NAME dump_lane_layout_perturbation
    COMMAND ${CMAKE_COMMAND} "-DPROBE_LANE=$<TARGET_FILE:mpt_dump_lane>" "-DREFERENCE_LANE=${MPT_DUMP_LANE_LAYOUT_REFERENCE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/dump_lane/layout_perturbation.cmake")
  set_tests_properties(dump_lane_layout_perturbation PROPERTIES LABELS dump SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 7200)
  # The gate (ADR 0008, "Module words: a safe rule must not depend on layout", point 6): the whole party
  # to `finished` on both lanes, exit 0. Hours, not minutes: ctest -L dump -R layout_perturbation_full.
  add_test(NAME dump_lane_layout_perturbation_full
    COMMAND ${CMAKE_COMMAND} "-DPROBE_LANE=$<TARGET_FILE:mpt_dump_lane>" "-DREFERENCE_LANE=${MPT_DUMP_LANE_LAYOUT_REFERENCE}"
            -DFULL=ON -P "${CMAKE_CURRENT_SOURCE_DIR}/dump_lane/layout_perturbation.cmake")
  set_tests_properties(dump_lane_layout_perturbation_full PROPERTIES LABELS dump SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 21600)
endif()

# Cross-process determinism (ADR 0002 rule 10 follow-up, P1-20b1): the same FakeRuntime run
# in separate processes must give byte-identical hash trails.
add_executable(mpt_determinism_child support/determinism_child.cpp)
target_link_libraries(mpt_determinism_child PRIVATE mpt::core)
mpt_set_warnings(mpt_determinism_child)
add_test(NAME determinism_cross_process
  COMMAND ${CMAKE_COMMAND}
    "-DCHILD=$<TARGET_FILE:mpt_determinism_child>"
    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/determinism_cross_process"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/support/cross_process_determinism.cmake")

# Shell smoke tests with FakeRuntime registered as mp4 through a test-only registration
# (P1-06). All headless; --user-dir keeps saves out of the real user folder.
add_executable(mpt_shell_fake shell/fake_shell_main.cpp)
target_link_libraries(mpt_shell_fake PRIVATE mpt::shell_app)
mpt_set_warnings(mpt_shell_fake)
# The fake shell looks up input profiles next to its executable, as the real shell does
# (SdlInput::profile_search_dirs). It gets a synthetic mp4 profile, never the shipped one,
# so the profile test below does not depend on input/profiles/ content.
add_custom_command(TARGET mpt_shell_fake POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/shell/profiles"
          "$<TARGET_FILE_DIR:mpt_shell_fake>/profiles"
  VERBATIM)
add_executable(mpt_make_fake_replay shell/make_fake_replay.cpp)
target_link_libraries(mpt_make_fake_replay PRIVATE mpt::shell_core mpt::asset_import)
mpt_set_warnings(mpt_make_fake_replay)

set(MPT_SHELL_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/shell_tests")
set(MPT_SHELL_USER_DIR "${MPT_SHELL_TEST_DIR}/user")
add_test(NAME shell_fake_clean COMMAND ${CMAKE_COMMAND} -E remove_directory "${MPT_SHELL_TEST_DIR}")
set_tests_properties(shell_fake_clean PROPERTIES FIXTURES_SETUP shell_fake_dir)

# A live session records a replay.
add_test(NAME shell_fake_record
  COMMAND mpt_shell_fake --headless --title mp4 --seed 7 --frames 240 --stats
          --user-dir "${MPT_SHELL_USER_DIR}" --record "${MPT_SHELL_TEST_DIR}/live.mptr")
set_tests_properties(shell_fake_record PROPERTIES
  FIXTURES_REQUIRED shell_fake_dir FIXTURES_SETUP shell_fake_live
  PASS_REGULAR_EXPRESSION "recorded .*live\\.mptr")
# The recorded replay plays back in the window and verifies headless.
add_test(NAME shell_fake_replay
  COMMAND mpt_shell_fake --headless --replay "${MPT_SHELL_TEST_DIR}/live.mptr" --user-dir "${MPT_SHELL_USER_DIR}")
set_tests_properties(shell_fake_replay PROPERTIES FIXTURES_REQUIRED shell_fake_live PASS_REGULAR_EXPRESSION "replay OK")
add_test(NAME shell_fake_verify_live
  COMMAND mpt_shell_fake --verify-replay "${MPT_SHELL_TEST_DIR}/live.mptr" --user-dir "${MPT_SHELL_USER_DIR}")
set_tests_properties(shell_fake_verify_live PROPERTIES FIXTURES_REQUIRED shell_fake_live PASS_REGULAR_EXPRESSION "replay OK")

# --verify-replay on a synthetic whole-party replay prints the committed golden trail.
add_test(NAME shell_fake_make_party_replay
  COMMAND mpt_make_fake_replay "${MPT_SHELL_TEST_DIR}/party.mptr")
set_tests_properties(shell_fake_make_party_replay PROPERTIES
  FIXTURES_REQUIRED shell_fake_dir FIXTURES_SETUP shell_fake_party)
add_test(NAME shell_fake_verify_golden
  COMMAND ${CMAKE_COMMAND}
    "-DSHELL=$<TARGET_FILE:mpt_shell_fake>"
    "-DREPLAY=${MPT_SHELL_TEST_DIR}/party.mptr"
    "-DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/golden/fake_runtime_v1_party.trail.txt"
    "-DUSER_DIR=${MPT_SHELL_USER_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/shell/verify_trail.cmake")
set_tests_properties(shell_fake_verify_golden PROPERTIES FIXTURES_REQUIRED shell_fake_party)

# M4-05: a session loads the title's input profile, and a minigame change selects that
# profile's override. The whole-party replay enters minigames; the synthetic profile has
# an override for every fake minigame. (Headless there are no devices, so the mapped
# frames cannot show the override; input/ tests cover what an override does.)
add_test(NAME shell_fake_title_profile
  COMMAND mpt_shell_fake --headless --replay "${MPT_SHELL_TEST_DIR}/party.mptr" --user-dir "${MPT_SHELL_USER_DIR}")
set_tests_properties(shell_fake_title_profile PROPERTIES
  FIXTURES_REQUIRED shell_fake_party
  PASS_REGULAR_EXPRESSION "input: .*profiles.mp4.default\\.json profile.*input: mp4\\.minigame\\.fake_[a-z]+ override.*replay OK")

# M4-06 (ADR 0006 amendment for M4-06): live sessions auto-record a replay and a
# .summary.txt. mpt_shell_fake_party's title plays the whole-party script by itself, so a
# headless live session reaches `finished`. The script also checks that playback and a
# failed start write nothing, and that --record still overrides the path.
add_executable(mpt_shell_fake_party shell/fake_shell_main.cpp)
target_compile_definitions(mpt_shell_fake_party PRIVATE MPT_SHELL_FAKE_PARTY)
target_link_libraries(mpt_shell_fake_party PRIVATE mpt::shell_app)
mpt_set_warnings(mpt_shell_fake_party)
add_test(NAME shell_fake_auto_record
  COMMAND ${CMAKE_COMMAND}
    "-DSHELL=$<TARGET_FILE:mpt_shell_fake_party>"
    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/shell_auto_record"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/shell/auto_record.cmake")

# The shell must boot, run frames and exit cleanly without a display.
add_test(NAME shell_headless_smoke COMMAND mpt_shell --headless --frames 120)
set_tests_properties(shell_headless_smoke PROPERTIES PASS_REGULAR_EXPRESSION "exited after 120 frame")
add_test(NAME shell_version COMMAND mpt_shell --version)
set_tests_properties(shell_version PROPERTIES PASS_REGULAR_EXPRESSION "mario-party-together [0-9]+\\.[0-9]+\\.[0-9]+")

# Compliance scanner self-test (synthetic files only).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME compliance_selftest
    COMMAND Python3::Interpreter -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/compliance" -p "test_*.py")
  set_tests_properties(compliance_selftest PROPERTIES
    ENVIRONMENT "MPT_REPLAY_CHECK=$<TARGET_FILE:mpt_replay_check>")

  # The built .mptr validator accepts a valid fixture, and it and the scanner's Python port
  # (tests/compliance/mptr.py) agree on every generated case (P1-20e2). Fails, rather than
  # skips, if the tool is missing.
  add_test(NAME replay_check_agrees_with_scanner
    COMMAND Python3::Interpreter -m unittest test_mptr.ToolCrossCheckTest
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/compliance")
  set_tests_properties(replay_check_agrees_with_scanner PROPERTIES
    ENVIRONMENT "MPT_REPLAY_CHECK=$<TARGET_FILE:mpt_replay_check>;MPT_REQUIRE_REPLAY_CHECK=1")
endif()
