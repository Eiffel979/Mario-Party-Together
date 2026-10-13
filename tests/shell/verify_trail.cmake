# Runs `SHELL --verify-replay REPLAY --user-dir USER_DIR` and checks that the hash trail
# it prints ("<frame> <16 hex>" lines) equals GOLDEN (same layout, '#' comments allowed).
# Used by the shell_fake_verify_golden test (P1-06).

foreach(var SHELL REPLAY GOLDEN USER_DIR)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "verify_trail.cmake needs -D${var}=...")
  endif()
endforeach()

execute_process(
  COMMAND "${SHELL}" --verify-replay "${REPLAY}" --user-dir "${USER_DIR}"
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE code)
if(NOT code EQUAL 0)
  message(FATAL_ERROR "--verify-replay exited with ${code}\nstdout:\n${out}\nstderr:\n${err}")
endif()

function(trail_lines text result)
  string(REPLACE "\r" "" text "${text}")
  string(REPLACE "\n" ";" lines "${text}")
  set(kept "")
  foreach(line IN LISTS lines)
    if(line MATCHES "^[0-9]+ [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]$")
      list(APPEND kept "${line}")
    endif()
  endforeach()
  set(${result} "${kept}" PARENT_SCOPE)
endfunction()

file(READ "${GOLDEN}" golden_text)
trail_lines("${golden_text}" golden)
trail_lines("${out}" actual)
list(LENGTH golden golden_count)
if(golden_count EQUAL 0)
  message(FATAL_ERROR "${GOLDEN} holds no trail lines")
endif()
if(NOT "${golden}" STREQUAL "${actual}")
  string(REPLACE ";" "\n" golden_show "${golden}")
  string(REPLACE ";" "\n" actual_show "${actual}")
  message(FATAL_ERROR "trail differs from ${GOLDEN}\ngolden:\n${golden_show}\nprinted:\n${actual_show}")
endif()
message(STATUS "trail matches ${GOLDEN} (${golden_count} checkpoints)")
