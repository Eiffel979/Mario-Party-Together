# ui/: the player-facing launcher (owner: frontend-ui, ADR 0010).
#
# mpt_ui_model  view models and flow state machines. Plain C++20: no RmlUi, no SDL
#               (ui_model_purity checks it). Links only core/, asset-import/ and the
#               pure input/ core.
# mpt_ui        RmlUi backend (render, system and file interfaces on the shell's
#               SDL_Renderer) and the screens. Never links a runtime or adapter target
#               (ui_include_boundary checks the sources).

find_package(Threads REQUIRED)
find_package(RmlUi CONFIG REQUIRED)

add_library(mpt_ui_model
  model/src/file_browser.cpp
  model/src/import_flow.cpp
  model/src/launcher_flow.cpp
  model/src/library.cpp
  model/src/nav_repeat.cpp
  model/src/seating.cpp
)
add_library(mpt::ui_model ALIAS mpt_ui_model)
target_include_directories(mpt_ui_model PUBLIC model/include)
target_link_libraries(mpt_ui_model
  PUBLIC mpt::core mpt::asset_import mpt::input Threads::Threads
  PRIVATE nlohmann_json::nlohmann_json)
mpt_set_warnings(mpt_ui_model)

add_library(mpt_ui
  backend/input_sdl.cpp
  backend/render_sdl.cpp
  backend/system_sdl.cpp
  screens/launcher.cpp
)
add_library(mpt::ui ALIAS mpt_ui)
target_include_directories(mpt_ui
  PUBLIC include
  PRIVATE backend)
# mpt::shell_core provides only the shell-side seams here (overlay.h, ui_host.h).
target_link_libraries(mpt_ui
  PUBLIC mpt::ui_model mpt::shell_core mpt::input SDL3::SDL3
  PRIVATE RmlUi::RmlUi)
target_compile_definitions(mpt_ui PRIVATE MPT_UI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
mpt_set_warnings(mpt_ui)

# Screens, theme and font next to the shell executable (ui/ in its folder). Without
# them the launcher falls back to this source folder (developer builds only, D1).
add_custom_target(mpt_ui_files
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/screens" "$<TARGET_FILE_DIR:mpt_shell>/ui/screens"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:mpt_shell>/ui/assets"
  COMMENT "Copying launcher screens and assets")
add_dependencies(mpt_ui mpt_ui_files)

if(MPT_BUILD_TESTS)
  find_package(doctest CONFIG REQUIRED)
  enable_testing()

  # Headless view-model tests (P1-23): synthetic data only, no renderer, no font.
  add_executable(mpt_ui_model_tests
    tests/main.cpp
    tests/test_file_browser.cpp
    tests/test_import_flow.cpp
    tests/test_launcher_flow.cpp
    tests/test_library.cpp
    tests/test_nav_repeat.cpp
    tests/test_seating.cpp
  )
  target_link_libraries(mpt_ui_model_tests PRIVATE mpt::ui_model doctest::doctest)
  mpt_set_warnings(mpt_ui_model_tests)
  add_test(NAME ui_model COMMAND mpt_ui_model_tests)

  # The ADR 0010 bring-up gate: the real launcher, drawn through the RmlUi render
  # interface on an SDL software renderer (dummy video driver), navigated with
  # synthetic keyboard and gamepad events, over a FakeRuntime replay. The hash trail
  # must equal the trail without a UI, including a pause and resume mid-run.
  add_executable(mpt_ui_overlay_tests
    tests/main.cpp
    tests/test_overlay_replay.cpp
  )
  target_link_libraries(mpt_ui_overlay_tests PRIVATE mpt::ui mpt::shell_core doctest::doctest)
  target_include_directories(mpt_ui_overlay_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
  target_compile_definitions(mpt_ui_overlay_tests PRIVATE MPT_UI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  mpt_set_warnings(mpt_ui_overlay_tests)
  add_test(NAME ui_overlay_replay COMMAND mpt_ui_overlay_tests)

  # ui/ never includes runtimes/ or adapters/ headers, and ui/model/ includes neither
  # RmlUi nor SDL (ADR 0010 §3.1, §4).
  add_test(NAME ui_include_boundary
    COMMAND "${CMAKE_COMMAND}" "-DUI_DIR=${CMAKE_CURRENT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boundaries.cmake")
endif()
