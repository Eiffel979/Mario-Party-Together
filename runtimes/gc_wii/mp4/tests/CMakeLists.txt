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
