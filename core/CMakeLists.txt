# Core contracts and shared types. No SDL dependency so runtimes, adapters and
# tests can link it without pulling in the windowing layer.
add_library(mpt_core
  src/content_id.cpp
  src/party_director.cpp
  src/replay.cpp
  src/session.cpp
  src/sha256.cpp
  src/title_id.cpp
  src/title_registry.cpp
)
add_library(mpt::core ALIAS mpt_core)
target_include_directories(mpt_core PUBLIC include)
target_compile_features(mpt_core PUBLIC cxx_std_20)
mpt_set_warnings(mpt_core)

# .mptr validator for the compliance scanner and the dump lane (ADR 0006).
add_executable(mpt_replay_check tools/replay_check.cpp)
target_link_libraries(mpt_replay_check PRIVATE mpt::core)
mpt_set_warnings(mpt_replay_check)

# The host shell executable.
add_executable(mpt_shell
  shell/main.cpp
  shell/app.cpp
)
target_link_libraries(mpt_shell PRIVATE mpt::core mpt::asset_import SDL3::SDL3)
target_compile_definitions(mpt_shell PRIVATE MPT_VERSION="${PROJECT_VERSION}")
set_target_properties(mpt_shell PROPERTIES OUTPUT_NAME "mario-party-together")
if(WIN32)
  set_target_properties(mpt_shell PROPERTIES WIN32_EXECUTABLE OFF)
endif()
mpt_set_warnings(mpt_shell)
add_custom_command(TARGET mpt_shell POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MPT_KNOWN_DUMPS_JSON}" "$<TARGET_FILE_DIR:mpt_shell>/data/known_dumps.json")
