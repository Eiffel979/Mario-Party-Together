# Link-time check for ADR 0008 §5: no object linked into the game module may leave
# fma/fmaf (condition 1) or a transcendental libm function (sin, cos, tan, their
# inverses and hyperbolics, exp*, log*, pow, cbrt, hypot, erf, gamma, sincos)
# undefined, in its float, double or long double form, or as a glibc *_finite
# alias. Those would bind to the host CRT or glibc and differ between platforms.
# Exactly specified functions (sqrt, fmod, floor, ceil, trunc, fabs, round, lround,
# copysign, frexp, ldexp) are allowed. P1-15 runs the same script on the game
# module's objects.
#
#   cmake -DNM=<llvm-nm|nm> "-DFILES=a.lib;b.obj" -P check_no_host_math.cmake
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
    # Symbol is the last field; allow a leading underscore (Mach-O / 32-bit COFF style) and
    # the __imp_ prefix of a DLL import (C code built against the dynamic CRT on Windows).
    if(_line MATCHES "[ \t](__imp_)?_?_?(fma|sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|asinh|acosh|atanh|exp|exp2|exp10|expm1|log|log2|log10|log1p|logb|pow|cbrt|hypot|erf|erfc|tgamma|lgamma|sincos)(f|l)?(_finite)?$"
       OR _line MATCHES "[ \t](__imp_)?_?(__builtin_fmaf?|__sincosf?_stret)$")
      list(APPEND _bad "${_file}: ${_line}")
    endif()
  endforeach()
endforeach()

if(_bad)
  string(REPLACE ";" "\n  " _msg "${_bad}")
  message(FATAL_ERROR "undefined host math references (must resolve to mpt_*, ADR 0008 §5):\n  ${_msg}")
endif()
message(STATUS "no undefined host fma or transcendental references")
