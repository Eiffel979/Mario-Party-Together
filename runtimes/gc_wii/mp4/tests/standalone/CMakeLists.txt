# Unit checks of the game module's own machinery that need no Party Board checkout, so public CI runs
# them: REL isolation and the pristine-data restore, and the copy-destination trap. Added by
# mp4/tests (module builds) or directly by runtimes/gc_wii (every Clang build with tests).
set(_mp4 "${CMAKE_CURRENT_LIST_DIR}/../..")
include("${_mp4}/tools/MptMp4Rel.cmake")
mpt_mp4_rel_setup()
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(_warn /W4 /permissive- /utf-8 /WX)
else()
  set(_warn -Wall -Wextra -Wpedantic -Wshadow -Werror)
endif()

mpt_mp4_add_rel(rel_a SET MPT_MP4_TEST_REL DIR "${CMAKE_CURRENT_BINARY_DIR}/rels" TARGET_PREFIX mpt_mp4_test_
                SOURCES "${_mp4}/tests/rel_a/rel.c" OPTIONS ${_warn})
mpt_mp4_add_rel(rel_b SET MPT_MP4_TEST_REL DIR "${CMAKE_CURRENT_BINARY_DIR}/rels" TARGET_PREFIX mpt_mp4_test_
                SOURCES "${_mp4}/tests/rel_b/rel.c" "${_mp4}/tests/rel_b/helper.c" OPTIONS ${_warn})
mpt_mp4_write_rel_table(MPT_MP4_TEST_REL "${CMAKE_CURRENT_BINARY_DIR}/generated/rel_table.c")
add_executable(mpt_mp4_rel_tests "${_mp4}/tests/test_rel_isolation.c" "${_mp4}/src/rel_loader.c"
               "${_mp4}/src/game_data.c" ${MPT_MP4_TEST_REL_INFOS} "${CMAKE_CURRENT_BINARY_DIR}/generated/rel_table.c")
target_include_directories(mpt_mp4_rel_tests PRIVATE "${_mp4}/include")
target_link_libraries(mpt_mp4_rel_tests PRIVATE ${MPT_MP4_TEST_REL_LIBS})
target_compile_options(mpt_mp4_rel_tests PRIVATE ${_warn})
add_test(NAME mp4_rel_isolation COMMAND mpt_mp4_rel_tests)

# The copy-destination debug trap on the real GXCopyTex (null renderer), when the GX layer is built.
if(TARGET mpt::gx)
  add_executable(mpt_mp4_copy_trap_tests "${_mp4}/tests/test_copy_trap.cpp" "${_mp4}/src/copy_trap.cpp")
  target_link_libraries(mpt_mp4_copy_trap_tests PRIVATE mpt::gx)
  target_compile_options(mpt_mp4_copy_trap_tests PRIVATE ${_warn})
  add_test(NAME mp4_copy_trap COMMAND mpt_mp4_copy_trap_tests)
endif()
