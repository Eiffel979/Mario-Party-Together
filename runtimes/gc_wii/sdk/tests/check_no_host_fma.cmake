# Link-time check for ADR 0008 §5 condition 1: no object linked into the game module
# may leave fma/fmaf undefined (that would bind to the host CRT or libm). Every
# explicit fmaf must resolve to our mpt_fmaf. P1-15 runs the same script on the
# game module's objects.
#
#   cmake -DNM=<llvm-nm|nm> "-DFILES=a.lib;b.obj" -P check_no_host_fma.cmake
if(NOT NM OR NOT FILES)
  message(FATAL_ERROR "usage: -DNM=<nm> -DFILES=<objects or archives>")
endif()

set(_bad "")
foreach(_file IN LISTS FILES)
  execute_process(COMMAND "${NM}" --undefined-only "${_file}"
                  OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${NM} failed on ${_file}: ${_err}")
  endif()
  string(REPLACE "\r" "" _out "${_out}")
  string(REPLACE "\n" ";" _lines "${_out}")
  foreach(_line IN LISTS _lines)
    # Symbol is the last field; allow a leading underscore (Mach-O / 32-bit COFF style).
    if(_line MATCHES "[ \t]_?(fma|fmaf|fmal|__builtin_fmaf?)$")
      list(APPEND _bad "${_file}: ${_line}")
    endif()
  endforeach()
endforeach()

if(_bad)
  string(REPLACE ";" "\n  " _msg "${_bad}")
  message(FATAL_ERROR "undefined host fma references (must use mpt_fmaf, ADR 0008 §5):\n  ${_msg}")
endif()
message(STATUS "no undefined fma/fmaf references")
