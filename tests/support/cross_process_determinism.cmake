# Cross-process determinism check (P1-20b1, ADR 0002 rule 10 follow-up).
#
# Runs mpt_determinism_child in separate processes with the same seed and requires
# byte-identical hash trails. The second process also skews its heap and runs two
# instances one after another, so hashes that depend on addresses, or on state left
# behind by an earlier instance, show up as a mismatch. A run with another seed must
# differ, which shows the comparison is not vacuous. Each trail is written to WORK_DIR
# for diffing when the test fails.
#
# Usage: cmake -DCHILD=<path> -DWORK_DIR=<dir> -P cross_process_determinism.cmake

foreach(var CHILD WORK_DIR)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "${var} must be set")
  endif()
endforeach()
file(MAKE_DIRECTORY "${WORK_DIR}")

set(frames 600)
set(interval 60)
math(EXPR expected_checkpoints "${frames} / ${interval}")

function(run_child name)
  execute_process(
    COMMAND "${CHILD}" --frames ${frames} --interval ${interval} ${ARGN}
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${name}: child process failed (${rc}):\n${err}")
  endif()
  string(REPLACE "\r" "" out "${out}")
  file(WRITE "${WORK_DIR}/${name}.trail.txt" "${out}")
  set(${name} "${out}" PARENT_SCOPE)
endfunction()

run_child(process_a --seed 1234)
run_child(process_b --seed 1234 --heap-skew 1048583 --instances 2)
run_child(process_other_seed --seed 1235)

string(REGEX MATCHALL "[0-9]+ [0-9a-f]+\n" checkpoints "${process_a}")
list(LENGTH checkpoints count)
if(NOT count EQUAL expected_checkpoints)
  message(FATAL_ERROR "expected ${expected_checkpoints} checkpoints, got ${count}:\n${process_a}")
endif()

if(NOT process_a STREQUAL process_b)
  message(FATAL_ERROR
    "hash trails differ between processes. Compare ${WORK_DIR}/process_a.trail.txt "
    "with ${WORK_DIR}/process_b.trail.txt.")
endif()

# Compare checkpoint lines only: the header names the seed, so it always differs.
string(REGEX MATCHALL "[0-9]+ [0-9a-f]+\n" other_checkpoints "${process_other_seed}")
if(checkpoints STREQUAL other_checkpoints)
  message(FATAL_ERROR "a different seed gave the same trail, so the trail does not cover RNG state")
endif()

message(STATUS "cross-process determinism: ${count} checkpoints identical in two processes")
