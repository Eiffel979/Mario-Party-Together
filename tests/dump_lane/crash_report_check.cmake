# The dump lane's crash report on a synthetic stub module (M4-04): runs
#   mpt_dump_lane --supervise --crash-report FILE --crash-selftest STUB KIND
# and checks the report on stdout and in FILE. No dump and no game code: the stub
# (tests/mp4_loader/overrun_stub.cpp) crashes on purpose, and the breadcrumbs are made up.
#
#   cmake -DLANE=<mpt_dump_lane> -DSTUB=<overrun stub module> -DKIND=gs|av|abort -P crash_report_check.cmake
#
# FILE goes to the system temp folder: the lane refuses report paths inside the repository, and CI's
# build folder is inside it.
foreach(_var LANE STUB KIND)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "crash_report_check: ${_var} is not set")
  endif()
endforeach()

if(DEFINED ENV{RUNNER_TEMP})
  set(_tmp "$ENV{RUNNER_TEMP}")
elseif(DEFINED ENV{TEMP})
  set(_tmp "$ENV{TEMP}")
else()
  set(_tmp "/tmp")
endif()
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef _tag)
set(_report "${_tmp}/mpt_crash_report_${KIND}_${_tag}.txt")
file(REMOVE "${_report}")

execute_process(
  COMMAND "${LANE}" --supervise --crash-report "${_report}" --crash-selftest "${STUB}" "${KIND}"
  RESULT_VARIABLE _code
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err)
message(STATUS "exit: ${_code}\n--- stdout\n${_out}--- stderr\n${_err}---")

set(_failures "")
# A function, not a macro: a macro would expand the escapes in the regex a second time.
function(expect _text _regex)
  if(NOT "${_text}" MATCHES "${_regex}")
    list(APPEND _failures "missing: ${_regex}")
    set(_failures "${_failures}" PARENT_SCOPE)
  endif()
endfunction()

if("${_code}" STREQUAL "0")
  list(APPEND _failures "the lane exited 0 although the stub crashed")
endif()
if(NOT EXISTS "${_report}")
  list(APPEND _failures "--crash-report wrote no file")
  set(_file "")
else()
  file(READ "${_report}" _file)
  file(REMOVE "${_report}")
endif()

# Every report: the header, the made-up breadcrumbs and the footer, in the file too.
foreach(_text IN ITEMS "${_out}" "${_file}")
  expect("${_text}" "dump lane crash report v1\n")
  expect("${_text}" "\nrun mode selftest seed 7 board 3\n")
  expect("${_text}" "\nframe 1234 phase minigame turn 4/20 overlay selftestDll\n")
  expect("${_text}" "\nlast checkpoint frame 1200 hash 0123456789abcdef\n")
  expect("${_text}" "\nend of crash report\n")
endforeach()

# No paths in the report: module file names only, never their folders. So no backslash, no drive and
# no "folder/name" anywhere ("turn 4/20" is the only slash a report has).
foreach(_path_regex "\\\\" "[A-Za-z]:[/\\\\]" "[A-Za-z_.-][A-Za-z0-9_.-]*/[A-Za-z_.]")
  if("${_file}" MATCHES "${_path_regex}")
    string(REGEX MATCH "[^\n]*${_path_regex}[^\n]*" _line "${_file}")
    list(APPEND _failures "the report holds a path: ${_line}")
  endif()
endforeach()

if(KIND STREQUAL "gs")
  # The acceptance case: a /GS fail-fast (Windows) is reported, and the culprit is the stub's function.
  if(WIN32)
    expect("${_file}" "\nstatus 0xc0000409 STATUS_STACK_BUFFER_OVERRUN \\(fail-fast\\)[^\n]*, fail-fast code 2 stack cookie check failure")
    expect("${_file}" "\nculprit mpt_overrun_stub\\.dll\\+0x[0-9a-f]+ [^\n]*overrun_stub_smash")
    expect("${_file}" "\nstack\n  #0 ")
  else()
    expect("${_file}" "\nstatus __stack_chk_fail")
    expect("${_file}" "\nculprit mpt_overrun_stub\\.so\\+0x[0-9a-f]+")
  endif()
elseif(KIND STREQUAL "av")
  expect("${_file}" "\nstatus 0xc0000005 EXCEPTION_ACCESS_VIOLATION, not handled, write 0x0000000000000000\n")
  expect("${_file}" "\nculprit mpt_overrun_stub\\.dll\\+0x[0-9a-f]+ mpt_overrun_stub_crash")
  # The in-process hook still reports first (crash_report.h), with the same breadcrumbs.
  expect("${_err}" "dump lane: crash, exception 0xc0000005")
  expect("${_err}" "run mode selftest seed 7 board 3")
elseif(KIND STREQUAL "abort")
  expect("${_file}" "\nstatus exit code 3 ")
  expect("${_err}" "dump lane: crash, abort\\(\\) \\(SIGABRT\\)")
else()
  list(APPEND _failures "unknown KIND ${KIND}")
endif()

if(_failures)
  list(JOIN _failures "\n  " _list)
  message(FATAL_ERROR "crash report check (${KIND}) failed:\n  ${_list}")
endif()
message(STATUS "crash report check (${KIND}): pass")
