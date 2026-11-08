# MP4 layout perturbation (dump lane, manual and local only; docs/titles/mp4.md section 20).
# A module built with the layout probe (MPT_MP4_RECIPE_LAYOUT_PROBE=ON or rodata) must give the same
# state_hash trail as a normal module on the same seeds and inputs: the Windows stand-in for the DLL and
# the .so laying the module out differently (E3c).
#
#   cmake -DPROBE_LANE=<probe build's mpt_dump_lane> -DREFERENCE_LANE=<normal build's mpt_dump_lane>
#         [-DSEEDS=1;2] [-DFRAMES=6000] -P layout_perturbation.cmake
#
# Each lane runs `--boot mp4 --party --seed S --frames FRAMES --every 10` (a trail line every 1,000
# frames) in its own process. The trails and the exit codes must match. Skipped without MPT_DUMP_DIR.
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ENV{MPT_DUMP_DIR} OR "$ENV{MPT_DUMP_DIR}" STREQUAL "")
  message("SKIPPED: MPT_DUMP_DIR is not set")
  return()
endif()
foreach(_v PROBE_LANE REFERENCE_LANE)
  if(NOT DEFINED ${_v} OR NOT EXISTS "${${_v}}")
    message(FATAL_ERROR "layout perturbation: ${_v} is not an existing mpt_dump_lane")
  endif()
endforeach()
file(REAL_PATH "${PROBE_LANE}" _probe)
file(REAL_PATH "${REFERENCE_LANE}" _reference)
if(_probe STREQUAL _reference)
  message(FATAL_ERROR "layout perturbation: PROBE_LANE and REFERENCE_LANE are the same executable")
endif()
if(NOT DEFINED SEEDS)
  set(SEEDS 1)
endif()
if(NOT DEFINED FRAMES)
  set(FRAMES 6000)
endif()

# The trail of one run: its `frame N hash X` lines.
function(_trail lane seed out_trail out_code)
  execute_process(COMMAND "${lane}" --boot mp4 --party --seed ${seed} --frames ${FRAMES} --every 10
                  RESULT_VARIABLE _code OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
  string(REGEX MATCHALL "frame [0-9]+ hash [0-9a-f]+" _lines "${_out}")
  set(${out_trail} "${_lines}" PARENT_SCOPE)
  set(${out_code} "${_code}" PARENT_SCOPE)
endfunction()

set(_failed 0)
foreach(_seed IN LISTS SEEDS)
  _trail("${REFERENCE_LANE}" ${_seed} _ref _ref_code)
  _trail("${PROBE_LANE}" ${_seed} _prb _prb_code)
  list(LENGTH _ref _n)
  if(_n EQUAL 0)
    message("seed ${_seed}: FAIL, the reference lane printed no trail (exit ${_ref_code})")
    set(_failed 1)
  elseif(NOT _ref STREQUAL _prb OR NOT _ref_code STREQUAL _prb_code)
    set(_first "")
    list(LENGTH _prb _m)
    math(EXPR _last "${_n} - 1")
    foreach(_i RANGE ${_last})
      list(GET _ref ${_i} _a)
      set(_b "(none)")
      if(_i LESS _m)
        list(GET _prb ${_i} _b)
      endif()
      if(NOT _a STREQUAL _b)
        set(_first "reference '${_a}', probe '${_b}'")
        break()
      endif()
    endforeach()
    message("seed ${_seed}: FAIL, trails differ: ${_first} (exit ${_ref_code} vs ${_prb_code})")
    set(_failed 1)
  else()
    list(GET _ref -1 _end)
    message("seed ${_seed}: ok, ${_n} checkpoints identical, last '${_end}' (exit ${_ref_code})")
  endif()
endforeach()
if(_failed)
  message(FATAL_ERROR "layout perturbation: the state_hash trail depends on the module layout")
endif()
