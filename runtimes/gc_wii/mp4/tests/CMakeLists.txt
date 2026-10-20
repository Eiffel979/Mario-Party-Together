# REL isolation and the copy trap need no Party Board checkout (standalone/, also built without the
# module so public CI runs them).
add_subdirectory(standalone)

# The C ABI as a host sees it: load the module and call through mpt_mp4_get_api (no dump).
add_executable(mpt_mp4_abi_tests test_abi.c)
target_include_directories(mpt_mp4_abi_tests PRIVATE ../include)
include("${CMAKE_CURRENT_SOURCE_DIR}/../layout.cmake")
target_compile_definitions(mpt_mp4_abi_tests PRIVATE MPT_MP4_MODULE_BASE=${MPT_MP4_MODULE_BASE}ull)
if(NOT WIN32)
  target_link_libraries(mpt_mp4_abi_tests PRIVATE ${CMAKE_DL_LIBS})
endif()
add_dependencies(mpt_mp4_abi_tests mp4_game)
target_compile_options(mpt_mp4_abi_tests PRIVATE ${MPT_MP4_WARN_FLAGS})
add_test(NAME mp4_game_abi COMMAND mpt_mp4_abi_tests "$<TARGET_FILE:mp4_game>")

# msm's disc structures in host byte order, on synthetic big-endian records.
add_executable(mpt_mp4_msm_swap_tests test_msm_swap.c ../src/msm_swap.c)
target_link_libraries(mpt_mp4_msm_swap_tests PRIVATE mpt_mp4_game_flags)
target_compile_options(mpt_mp4_msm_swap_tests PRIVATE ${MPT_MP4_WARN_FLAGS})
add_test(NAME mp4_msm_swap COMMAND mpt_mp4_msm_swap_tests)
