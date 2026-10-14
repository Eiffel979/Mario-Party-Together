# Link-time check for the GX front end and MTX (game-module code, ADR 0008 section 5): no object may
# leave a host libm function undefined unless IEEE 754 specifies it exactly. Trig must resolve to the
# SDK's mpt_* functions through the forced math redirect, fma/fmaf to mpt_fmaf.
#
#   cmake -DNM=<llvm-nm|nm> "-DFILES=a.lib;b.obj" -P check_no_host_math.cmake
if(NOT NM OR NOT FILES)
  message(FATAL_ERROR "usage: -DNM=<nm> -DFILES=<objects or archives>")
endif()

# Every C99 <math.h> function that IEEE 754 does not specify exactly, in its double, float and long
# double forms. Exactly specified ones (sqrt, fabs, floor, ceil, trunc, round, rint, nearbyint, fmod,
# remainder, copysign, nextafter, ldexp, frexp, scalbn, modf, fmin, fmax, fdim) are allowed.
set(_names sin cos tan asin acos atan atan2 sinh cosh tanh asinh acosh atanh exp exp2 expm1 log log2
           log10 log1p logb pow cbrt hypot erf erfc tgamma lgamma fma)
set(_forbidden "")
foreach(_n IN LISTS _names)
  list(APPEND _forbidden "${_n}" "${_n}f" "${_n}l")
endforeach()
string(JOIN "|" _forbidden ${_forbidden})

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
    # The symbol is the last field. Allow a leading underscore (Mach-O, 32-bit COFF) and the __imp_
    # prefix of DLL imports (clang-cl with /MD references the UCRT as __imp_sinf, __imp_log2, ...).
    if(_line MATCHES "[ \t](__imp_)?_?(${_forbidden})$")
      list(APPEND _bad "${_file}: ${_line}")
    endif()
  endforeach()
endforeach()

if(_bad)
  string(REPLACE ";" "\n  " _msg "${_bad}")
  message(FATAL_ERROR "undefined host libm references in GX front-end code (ADR 0008 section 5):\n  ${_msg}")
endif()
message(STATUS "no undefined host libm references")
