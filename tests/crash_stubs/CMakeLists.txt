# The synthetic modules the dump lane's crash self-tests crash on purpose (tests/CMakeLists.txt registers
# the tests): mpt_overrun_stub (--crash-selftest, M4-04) and mpt_guard_stub (--guard-reload-selftest,
# M4-16). Their own directory, so that the Debug flags below change these two modules only.
#
# MSVC's Debug runtime checks (/RTC1) catch the stubs' deliberate stack overrun on return, before the
# /GS cookie check the tests are about, and stop with a breakpoint (0x80000003) instead of the /GS
# fail-fast (0xC0000409). So no runtime checks here, in any configuration: the stubs die as they do in a
# release build. (clang-cl accepts /RTC1 and ignores it; MSVC_RUNTIME_CHECKS is the CMake 3.31+ form,
# used when policy CMP0184 is NEW.)
foreach(_mpt_flags CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
  string(REGEX REPLACE "[/-]RTC[1csu]+" "" ${_mpt_flags} "${${_mpt_flags}}")
endforeach()

set(_mpt_stub_dir "${CMAKE_CURRENT_SOURCE_DIR}/../mp4_loader")

add_library(mpt_overrun_stub MODULE "${_mpt_stub_dir}/overrun_stub.cpp")
set_target_properties(mpt_overrun_stub PROPERTIES PREFIX "" MSVC_RUNTIME_CHECKS "")
mpt_set_warnings(mpt_overrun_stub)
if(WIN32)
  target_compile_options(mpt_overrun_stub PRIVATE /GS)
else()
  target_compile_options(mpt_overrun_stub PRIVATE -fstack-protector-all)
endif()

# Windows x64 only; linked at a fixed base like the game module, so a reload maps it back.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND COMMAND mpt_mp4_fixed_base_link)
  add_library(mpt_guard_stub MODULE "${_mpt_stub_dir}/guard_stub.cpp")
  set_target_properties(mpt_guard_stub PROPERTIES PREFIX "" MSVC_RUNTIME_CHECKS "")
  target_compile_options(mpt_guard_stub PRIVATE /GS)
  mpt_mp4_fixed_base_link(mpt_guard_stub BASE 0x5B9700000000)
  mpt_set_warnings(mpt_guard_stub)
endif()
