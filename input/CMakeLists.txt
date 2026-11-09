# input/: unified input (owner: input-mapping).
#
# mpt_input      pure mapping core: profiles, seating, devices -> InputFrame.
#                Does not link SDL3; input_core_purity checks it does not include it.
# mpt_input_sdl  SDL3 device backend (keyboard + gamepad hot-plug) feeding the core.
add_library(mpt_input
  src/held_mask.cpp
  src/mapper.cpp
  src/names.cpp
  src/ports.cpp
  src/profile.cpp
  src/reserved.cpp
  src/title_profile.cpp
)
add_library(mpt::input ALIAS mpt_input)
target_include_directories(mpt_input PUBLIC include)
target_link_libraries(mpt_input
  PUBLIC mpt::core
  PRIVATE nlohmann_json::nlohmann_json)
mpt_set_warnings(mpt_input)

add_library(mpt_input_sdl
  src/sdl_input.cpp
)
add_library(mpt::input_sdl ALIAS mpt_input_sdl)
target_link_libraries(mpt_input_sdl PUBLIC mpt::input SDL3::SDL3)
mpt_set_warnings(mpt_input_sdl)

# Installed title profiles: input/profiles/ is copied to <bin>/profiles/ next to
# the shell executable, where SdlInput::profile_search_dirs() looks. core/ is
# added before input/, so the shell target exists here; add_dependencies works
# across directories, so core/CMakeLists.txt needs no change.
if(TARGET mpt_shell)
  add_custom_target(mpt_input_profiles
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/profiles"
            "$<TARGET_FILE_DIR:mpt_shell>/profiles"
    COMMENT "Copying input profiles next to the shell"
    VERBATIM)
  add_dependencies(mpt_shell mpt_input_profiles)
endif()

if(MPT_BUILD_TESTS)
  find_package(doctest CONFIG REQUIRED)
  enable_testing()

  # Synthetic device state only; links the pure core, never SDL.
  add_executable(mpt_input_tests
    tests/main.cpp
    tests/test_held_mask.cpp
    tests/test_mapper.cpp
    tests/test_mp4_e2_device.cpp
    tests/test_mp4_profile.cpp
    tests/test_names.cpp
    tests/test_ports.cpp
    tests/test_profile.cpp
    tests/test_reserved.cpp
    tests/test_title_profile.cpp
  )
  target_link_libraries(mpt_input_tests PRIVATE mpt::input doctest::doctest)
  # The shipped profiles and the coverage table they are checked against (read-only).
  target_compile_definitions(mpt_input_tests PRIVATE
    "MPT_INPUT_PROFILES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/profiles\""
    "MPT_DOCS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../docs\""
    # Synthetic profile directories the loader tests create and delete.
    "MPT_INPUT_TEST_SCRATCH_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/title_profile_scratch\"")
  mpt_set_warnings(mpt_input_tests)
  add_test(NAME input_unit COMMAND mpt_input_tests)
  add_test(NAME input_core_purity
    COMMAND "${CMAKE_COMMAND}" "-DINPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_no_sdl.cmake")

  # The build put the shipped profiles where the shell looks for them.
  if(TARGET mpt_input_profiles)
    add_test(NAME input_profiles_installed
      COMMAND "${CMAKE_COMMAND}" -E compare_files "${CMAKE_CURRENT_SOURCE_DIR}/profiles/mp4/default.json"
              "$<TARGET_FILE_DIR:mpt_shell>/profiles/mp4/default.json")
  endif()

  # The SDL backend boots headless and yields frames (no input injected).
  add_executable(mpt_input_sdl_smoke tests/sdl_smoke.cpp)
  target_link_libraries(mpt_input_sdl_smoke PRIVATE mpt::input_sdl)
  mpt_set_warnings(mpt_input_sdl_smoke)
  add_test(NAME input_sdl_smoke COMMAND mpt_input_sdl_smoke)
  set_tests_properties(input_sdl_smoke PROPERTIES PASS_REGULAR_EXPRESSION "input sdl smoke: OK")
endif()
