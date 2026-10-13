find_package(doctest CONFIG REQUIRED)

add_executable(mpt_unit_tests
  unit/main.cpp
  unit/test_asset_import.cpp
  unit/test_build.cpp
  unit/test_contract_init.cpp
  unit/test_core.cpp
  unit/test_determinism.cpp
  unit/test_determinism_harness.cpp
  unit/test_gc_disc.cpp
  unit/test_fake_adapter.cpp
  unit/test_fake_runtime_v1.cpp
  unit/test_golden_trail.cpp
  unit/test_replay.cpp
  unit/test_session.cpp
  unit/test_shell.cpp
)
target_link_libraries(mpt_unit_tests PRIVATE mpt::core mpt::asset_import mpt::shell_core doctest::doctest)
target_compile_definitions(mpt_unit_tests PRIVATE
  MPT_KNOWN_DUMPS_JSON="${MPT_KNOWN_DUMPS_JSON}"
  MPT_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
mpt_set_warnings(mpt_unit_tests)

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

# The .mptr validator (ADR 0006) must reject a file that is not a replay.
add_test(NAME replay_check_rejects_non_replay
  COMMAND mpt_replay_check --fixture "${CMAKE_CURRENT_SOURCE_DIR}/golden/replay_v0_synthetic.mptr.hex")
set_tests_properties(replay_check_rejects_non_replay PROPERTIES PASS_REGULAR_EXPRESSION "not an \\.mptr file")

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
endif()
