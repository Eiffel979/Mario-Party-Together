# input/: unified input (owner: input-mapping).
#
# mpt_input      pure mapping core: profiles, seating, devices -> InputFrame.
#                Does not link SDL3; input_core_purity checks it does not include it.
add_library(mpt_input
  src/mapper.cpp
  src/names.cpp
  src/ports.cpp
  src/profile.cpp
)
add_library(mpt::input ALIAS mpt_input)
target_include_directories(mpt_input PUBLIC include)
target_link_libraries(mpt_input
  PUBLIC mpt::core
  PRIVATE nlohmann_json::nlohmann_json)
mpt_set_warnings(mpt_input)

if(MPT_BUILD_TESTS)
  find_package(doctest CONFIG REQUIRED)
  enable_testing()

  # Synthetic device state only; links the pure core, never SDL.
  add_executable(mpt_input_tests
    tests/main.cpp
    tests/test_mapper.cpp
    tests/test_names.cpp
    tests/test_ports.cpp
    tests/test_profile.cpp
  )
  target_link_libraries(mpt_input_tests PRIVATE mpt::input doctest::doctest)
  mpt_set_warnings(mpt_input_tests)
  add_test(NAME input_unit COMMAND mpt_input_tests)
  add_test(NAME input_core_purity
    COMMAND "${CMAKE_COMMAND}" "-DINPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_no_sdl.cmake")
endif()
