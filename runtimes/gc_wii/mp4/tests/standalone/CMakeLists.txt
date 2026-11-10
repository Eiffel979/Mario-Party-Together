# Unit checks of the game module's own machinery that need no Party Board checkout, so public CI runs
# them: REL isolation and the pristine-data restore, the module's symbol-table reader, the ARAM
# checks and the copy-destination trap. Added by
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

# The module's symbol-table reader on this test's own image (PE/COFF on Windows, ELF elsewhere).
include("${_mp4}/tools/MptMp4ModuleSymbols.cmake")
add_executable(mpt_mp4_module_symbols_tests "${_mp4}/tests/test_module_symbols.cpp")
target_link_libraries(mpt_mp4_module_symbols_tests PRIVATE mpt_mp4_module_symbols)
target_compile_options(mpt_mp4_module_symbols_tests PRIVATE ${_warn})
mpt_mp4_link_symtab(mpt_mp4_module_symbols_tests)
add_test(NAME mp4_module_symbols COMMAND mpt_mp4_module_symbols_tests)

# Real linkers keep the function type (ADR 0008, "Module words: a safe rule must not depend on layout",
# point 6): a stub module (tests/mp4_loader/function_starts_*), linked exactly like the game module by
# this Clang build's lld-link or ld.lld, reports a static C, an extern C and an assembly function as
# function starts.
set(_mp4_loader "${_mp4}/../../../tests/mp4_loader")
include("${_mp4}/layout.cmake")
add_library(mpt_mp4_function_starts_stub MODULE "${_mp4_loader}/function_starts_module.cpp"
                                                "${_mp4_loader}/function_starts_stub.c")
target_include_directories(mpt_mp4_function_starts_stub PRIVATE "${_mp4}/include")
target_link_libraries(mpt_mp4_function_starts_stub PRIVATE mpt_mp4_module_symbols)
set_target_properties(mpt_mp4_function_starts_stub PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden
                      CXX_VISIBILITY_PRESET hidden)
target_compile_options(mpt_mp4_function_starts_stub PRIVATE ${_warn})
mpt_mp4_fixed_base_link(mpt_mp4_function_starts_stub)
mpt_mp4_link_symtab(mpt_mp4_function_starts_stub)
add_executable(mpt_mp4_function_starts_tests "${_mp4_loader}/test_function_starts.cpp")
target_compile_options(mpt_mp4_function_starts_tests PRIVATE ${_warn})
target_link_libraries(mpt_mp4_function_starts_tests PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(mpt_mp4_function_starts_tests mpt_mp4_function_starts_stub)
add_test(NAME mp4_function_starts
         COMMAND mpt_mp4_function_starts_tests "$<TARGET_FILE:mpt_mp4_function_starts_stub>")

# The per-step ARAM alias check (finding 2) and msm's guarded region (ADR 0008, "Gate status after the
# writer fixes", point 3), on synthetic block lists; the SDK's own tests cover the DMA guard itself.
add_executable(mpt_mp4_aram_checks_tests "${_mp4}/tests/test_aram_checks.cpp" "${_mp4}/src/aram_checks.cpp")
target_include_directories(mpt_mp4_aram_checks_tests PRIVATE "${_mp4}/src")
target_compile_options(mpt_mp4_aram_checks_tests PRIVATE ${_warn})
add_test(NAME mp4_aram_checks COMMAND mpt_mp4_aram_checks_tests)

# The copy-destination debug trap on the real GXCopyTex (null renderer), when the GX layer is built.
if(TARGET mpt::gx)
  add_executable(mpt_mp4_copy_trap_tests "${_mp4}/tests/test_copy_trap.cpp" "${_mp4}/src/copy_trap.cpp")
  target_link_libraries(mpt_mp4_copy_trap_tests PRIVATE mpt::gx)
  target_compile_options(mpt_mp4_copy_trap_tests PRIVATE ${_warn})
  add_test(NAME mp4_copy_trap COMMAND mpt_mp4_copy_trap_tests)
endif()
