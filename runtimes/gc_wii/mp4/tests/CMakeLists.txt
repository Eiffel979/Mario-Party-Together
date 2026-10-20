# P1-15 unit check for REL isolation and the pristine-data restore, on synthetic RELs only.
mpt_mp4_add_rel(rel_a SET MPT_MP4_TEST_REL DIR "${CMAKE_CURRENT_BINARY_DIR}/rels" TARGET_PREFIX mpt_mp4_test_
                SOURCES rel_a/rel.c)
mpt_mp4_add_rel(rel_b SET MPT_MP4_TEST_REL DIR "${CMAKE_CURRENT_BINARY_DIR}/rels" TARGET_PREFIX mpt_mp4_test_
                SOURCES rel_b/rel.c rel_b/helper.c)
mpt_mp4_write_rel_table(MPT_MP4_TEST_REL "${CMAKE_CURRENT_BINARY_DIR}/generated/rel_table.c")
add_executable(mpt_mp4_rel_tests test_rel_isolation.c ../src/rel_loader.c ${MPT_MP4_TEST_REL_INFOS}
               "${CMAKE_CURRENT_BINARY_DIR}/generated/rel_table.c")
target_include_directories(mpt_mp4_rel_tests PRIVATE ../include)
target_link_libraries(mpt_mp4_rel_tests PRIVATE ${MPT_MP4_TEST_REL_LIBS})
add_test(NAME mp4_rel_isolation COMMAND mpt_mp4_rel_tests)

# The C ABI as a host sees it: load the module and call through mpt_mp4_get_api (no dump).
add_executable(mpt_mp4_abi_tests test_abi.c)
target_include_directories(mpt_mp4_abi_tests PRIVATE ../include)
if(NOT WIN32)
  target_link_libraries(mpt_mp4_abi_tests PRIVATE ${CMAKE_DL_LIBS})
endif()
add_dependencies(mpt_mp4_abi_tests mp4_game)
add_test(NAME mp4_game_abi COMMAND mpt_mp4_abi_tests "$<TARGET_FILE:mp4_game>")

# The copy-destination debug trap on the real GXCopyTex (null renderer).
add_executable(mpt_mp4_copy_trap_tests test_copy_trap.cpp ../src/copy_trap.cpp)
target_link_libraries(mpt_mp4_copy_trap_tests PRIVATE mpt::gx)
add_test(NAME mp4_copy_trap COMMAND mpt_mp4_copy_trap_tests)

# msm's disc structures in host byte order, on synthetic big-endian records.
add_executable(mpt_mp4_msm_swap_tests test_msm_swap.c ../src/msm_swap.c)
target_link_libraries(mpt_mp4_msm_swap_tests PRIVATE mpt_mp4_game_flags)
add_test(NAME mp4_msm_swap COMMAND mpt_mp4_msm_swap_tests)
