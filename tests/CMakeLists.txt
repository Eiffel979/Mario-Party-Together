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
  unit/test_session.cpp
)
target_link_libraries(mpt_unit_tests PRIVATE mpt::core mpt::asset_import doctest::doctest)
target_compile_definitions(mpt_unit_tests PRIVATE
  MPT_KNOWN_DUMPS_JSON="${MPT_KNOWN_DUMPS_JSON}"
  MPT_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
mpt_set_warnings(mpt_unit_tests)

add_test(NAME unit COMMAND mpt_unit_tests)

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
