# Link-time check for the GX front end and MTX (game-module code, ADR 0008 section 5): no object may
# leave a host libm transcendental or fma undefined. Trig must resolve to the SDK's mpt_* functions
# through the forced math redirect, fma/fmaf to mpt_fmaf. sqrt and ldexp are exact in IEEE 754 and
# allowed from the host.
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
    if(_line MATCHES "[ \t]_?(fmal?|fmaf|sinf?|sinl|cosf?|cosl|tanf?|tanl|asinf?|acosf?|atanf?|atan2f?|sinhf?|coshf?|tanhf?|expf?|exp2f?|expm1f?|logf?|log2f?|log10f?|log1pf?|powf?|cbrtf?|hypotf?)$")
      list(APPEND _bad "${_file}: ${_line}")
    endif()
  endforeach()
endforeach()

if(_bad)
  string(REPLACE ";" "\n  " _msg "${_bad}")
  message(FATAL_ERROR "undefined host libm references in GX front-end code (ADR 0008 section 5):\n  ${_msg}")
endif()
message(STATUS "no undefined host libm references")
