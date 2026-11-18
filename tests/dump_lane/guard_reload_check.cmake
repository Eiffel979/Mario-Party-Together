# The E5 module-reload check on a synthetic stub (M4-16, dump_lane/stack_guard.h): runs
#   mpt_dump_lane --supervise --guard-reload-selftest STUB reload
# and checks that the suspended guarded frame fails fast after the reload, under a new cookie. No dump
# and no game code. A script, not a plain test, because the lane exits with the child's 0xC0000409,
# which CTest counts as a crash whatever the output says.
#
#   cmake -DLANE=<mpt_dump_lane> -DSTUB=<guard stub module> -P guard_reload_check.cmake
foreach(_var LANE STUB)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "guard_reload_check: ${_var} is not set")
  endif()
endforeach()
execute_process(
  COMMAND "${LANE}" --supervise --guard-reload-selftest "${STUB}" reload
  RESULT_VARIABLE _code
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err)
message(STATUS "exit: ${_code}\n--- stdout\n${_out}--- stderr\n${_err}---")
set(_failures "")
foreach(_regex
    "guard selftest: suspended, [1-9][0-9]* guard word"
    "module unloaded and loaded again, at the same base yes, cookie [0-9a-f]+ \\(CHANGED\\)"
    "status 0xc0000409 STATUS_STACK_BUFFER_OVERRUN \\(fail-fast\\)[^\n]*stack cookie check failure"
    "culprit mpt_guard_stub\\.dll\\+0x[0-9a-f]+ [^\n]*mpt_guard_stub_suspend")
  if(NOT "${_out}" MATCHES "${_regex}")
    list(APPEND _failures "missing: ${_regex}")
  endif()
endforeach()
if("${_out}" MATCHES "the guarded function returned" OR "${_code}" STREQUAL "0")
  list(APPEND _failures "the guarded function returned after the reload")
endif()
if(_failures)
  list(JOIN _failures "\n  " _list)
  message(FATAL_ERROR "guard reload check failed:\n  ${_list}")
endif()
message(STATUS "guard reload check: pass")
