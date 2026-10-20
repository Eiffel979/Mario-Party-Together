# Self-test for ui/cmake/check_link_boundary.cmake: a stand-in mpt_ui that links (through
# one intermediate target) a target defined under runtimes/ when VIOLATE is ON.
cmake_minimum_required(VERSION 3.25)
project(mpt_ui_link_selftest LANGUAGES NONE)

include("${MPT_UI_LINK_CHECK}")

add_subdirectory(runtimes/fake)
add_library(middle INTERFACE)
add_library(mpt_ui INTERFACE)
target_link_libraries(mpt_ui INTERFACE middle)
if(VIOLATE)
  target_link_libraries(middle INTERFACE "$<LINK_ONLY:fake_runtime>")
endif()

mpt_ui_check_link_boundary()
