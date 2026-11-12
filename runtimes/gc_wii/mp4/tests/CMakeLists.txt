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

# Game heaps return zeroed blocks (ADR 0008, "Stale heap bytes", patch 0026): Party Board's HuMem
# allocator and msm's heap from the patched pin, on the test's own buffers (no dump).
add_executable(mpt_mp4_heap_zero_tests test_heap_zero.c "${MPT_MP4_PB}/src/game/memory.c" "${MPT_MP4_PB}/src/msm/msmmem.c")
target_link_libraries(mpt_mp4_heap_zero_tests PRIVATE mpt_mp4_game_flags)
set_source_files_properties("${MPT_MP4_PB}/src/game/memory.c" "${MPT_MP4_PB}/src/msm/msmmem.c"
                            TARGET_DIRECTORY mpt_mp4_heap_zero_tests PROPERTIES COMPILE_OPTIONS "${MPT_MP4_NOWARN}")
target_compile_options(mpt_mp4_heap_zero_tests PRIVATE ${MPT_MP4_WARN_FLAGS})
add_test(NAME mp4_heap_zero COMMAND mpt_mp4_heap_zero_tests)

# HuARMalloc (patches 0028 and 0030): zeroed blocks, the console's ARAM base above msm's region, and the
# in-use list the state hash walks. armem.c from the patched pin; its SDK calls are stubbed over the
# test's own ARAM buffer (no dump).
add_executable(mpt_mp4_armem_tests test_armem.c "${MPT_MP4_PB}/src/game/armem.c")
target_link_libraries(mpt_mp4_armem_tests PRIVATE mpt_mp4_game_flags)
set_source_files_properties("${MPT_MP4_PB}/src/game/armem.c"
                            TARGET_DIRECTORY mpt_mp4_armem_tests PROPERTIES COMPILE_OPTIONS "${MPT_MP4_NOWARN}")
target_compile_options(mpt_mp4_armem_tests PRIVATE ${MPT_MP4_WARN_FLAGS})
add_test(NAME mp4_armem COMMAND mpt_mp4_armem_tests)

# SetRevMtx reads no uninitialised stack (patch 0029, the layout gate's writer): EnvelopeExec.c from the
# patched pin on a hand-made two-object model, on the GX layer's MTX (no dump).
add_executable(mpt_mp4_envelope_tests test_envelope.c "${MPT_MP4_PB}/src/game/EnvelopeExec.c")
target_link_libraries(mpt_mp4_envelope_tests PRIVATE mpt_mp4_game_flags)
set_source_files_properties("${MPT_MP4_PB}/src/game/EnvelopeExec.c"
                            TARGET_DIRECTORY mpt_mp4_envelope_tests PROPERTIES COMPILE_OPTIONS "${MPT_MP4_NOWARN}")
target_compile_options(mpt_mp4_envelope_tests PRIVATE ${MPT_MP4_WARN_FLAGS})
add_test(NAME mp4_envelope COMMAND mpt_mp4_envelope_tests)

# A second HuSprAnimRead of one anim file shares the first conversion (patch 0032, the M4-03 seed 2
# crash in byteswap_animbankdata): sprman.c and byteswap.cpp from the patched pin on a hand-made
# big-endian anim file (no dump). No renderer (mpt::gx) on the link line, like the tests above: the
# test stubs the two GX calls sprman.c makes, and the rest is the frontend's MTX. Listing the renderer
# archive ahead of the frontend's also fails under GNU ld, which reads each archive once.
add_executable(mpt_mp4_sprman_anim_tests test_sprman_anim.c "${MPT_MP4_PB}/src/game/sprman.c"
                                         $<TARGET_OBJECTS:mpt_mp4_byteswap>)
target_link_libraries(mpt_mp4_sprman_anim_tests PRIVATE mpt_mp4_game_flags)
set_source_files_properties("${MPT_MP4_PB}/src/game/sprman.c"
                            TARGET_DIRECTORY mpt_mp4_sprman_anim_tests PROPERTIES COMPILE_OPTIONS "${MPT_MP4_NOWARN}")
target_compile_options(mpt_mp4_sprman_anim_tests PRIVATE ${MPT_MP4_WARN_FLAGS})
add_test(NAME mp4_sprman_anim COMMAND mpt_mp4_sprman_anim_tests)
