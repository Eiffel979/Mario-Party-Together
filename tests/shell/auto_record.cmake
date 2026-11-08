# M4-06 (ADR 0006 amendment for M4-06): live sessions auto-record a replay and a summary.
#
#   cmake -DSHELL=<mpt_shell_fake_party> -DWORK_DIR=<empty scratch folder> -P auto_record.cmake
#
# SHELL registers a FakeRuntime "mp4" that plays the whole-party script by itself, so a
# headless live session reaches `finished`. Checks, in order:
#   1. a live party auto-records <user>/replays/mp4-11-1.mptr and its .summary.txt, which
#      says `finished` and holds no path outside the user dir;
#   2. that replay passes --verify-replay, which prints the final phase;
#   3. a second party with the same seed gets n = 2, starts from the card the first one
#      saved, keeps that image in the media store, and still verifies after the slot moved on;
#   4. a --replay session records nothing;
#   5. a session that fails to start (a save slot the runtime rejects) leaves no file.

foreach(var SHELL WORK_DIR)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "auto_record.cmake needs -D${var}=...")
  endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
set(user "${WORK_DIR}/user")
set(replays "${user}/replays")

function(run_shell out_var code_var)
  execute_process(COMMAND "${SHELL}" ${ARGN} OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE code)
  set(${out_var} "${out}\n${err}" PARENT_SCOPE)
  set(${code_var} "${code}" PARENT_SCOPE)
endfunction()

function(expect_ok label out code)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "${label}: exited with ${code}\n${out}")
  endif()
endfunction()

function(expect_match label out regex)
  if(NOT out MATCHES "${regex}")
    message(FATAL_ERROR "${label}: output does not match '${regex}'\n${out}")
  endif()
endfunction()

function(replay_files result)
  file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${replays}" "${replays}/*")
  list(SORT files)
  set(${result} "${files}" PARENT_SCOPE)
endfunction()

# 1. A live party. 600 frames: the script reaches `finished` at frame 410.
run_shell(out code --headless --title mp4 --seed 11 --frames 600 --user-dir "${user}")
expect_ok("live party" "${out}" "${code}")
expect_match("live party" "${out}" "party: phase finished, turn 3/3, [0-9]+ events")
expect_match("live party" "${out}" "recorded [^\n]*mp4-11-1\\.mptr")
foreach(f "${replays}/mp4-11-1.mptr" "${replays}/mp4-11-1.summary.txt")
  if(NOT EXISTS "${f}")
    message(FATAL_ERROR "live party: ${f} was not written\n${out}")
  endif()
endforeach()
file(READ "${replays}/mp4-11-1.summary.txt" summary)
foreach(line "phase finished" "turn 3/3" "title mp4" "seed 11" "replay replays/mp4-11-1.mptr" "frames 600"
             "end ok" "runtime_build none" "board mp4.board.fake_board")
  if(NOT summary MATCHES "(^|\n)${line}\n")
    message(FATAL_ERROR "summary lacks '${line}':\n${summary}")
  endif()
endforeach()
if(NOT summary MATCHES "(^|\n)events [1-9][0-9]*\n")
  message(FATAL_ERROR "summary has no event count:\n${summary}")
endif()
# No path but the user-dir-relative replay path: not the work folder, not a drive or root.
string(FIND "${summary}" "${WORK_DIR}" at)
if(NOT at EQUAL -1 OR summary MATCHES "[A-Za-z]:[/\\\\]" OR summary MATCHES "\n[a-z_0-9]+ /")
  message(FATAL_ERROR "summary holds a path outside the user dir:\n${summary}")
endif()
# The first party started from a blank card, so nothing went to the media store.
if(EXISTS "${replays}/media")
  message(FATAL_ERROR "a blank card was put in the media store")
endif()

# 2. The auto-recorded replay verifies and --verify-replay prints the final phase.
run_shell(out code --verify-replay "${replays}/mp4-11-1.mptr" --user-dir "${user}")
expect_ok("verify 1" "${out}" "${code}")
expect_match("verify 1" "${out}" "party: phase finished, turn 3/3")
expect_match("verify 1" "${out}" "replay OK")

# 3. The same seed again: n = 2. The first party saved (frame 300), so this one starts from
# a non-blank card, and its own save moves the slot on. The replay must still verify.
run_shell(out code --headless --title mp4 --seed 11 --frames 600 --user-dir "${user}")
expect_ok("second party" "${out}" "${code}")
if(NOT EXISTS "${replays}/mp4-11-2.mptr" OR NOT EXISTS "${replays}/mp4-11-2.summary.txt")
  message(FATAL_ERROR "second party: mp4-11-2 was not written\n${out}")
endif()
file(GLOB media "${replays}/media/*.mptsave")
list(LENGTH media media_count)
if(NOT media_count EQUAL 1)
  message(FATAL_ERROR "expected one image in the media store, found: ${media}")
endif()
run_shell(out code --verify-replay "${replays}/mp4-11-2.mptr" --user-dir "${user}")
expect_ok("verify 2" "${out}" "${code}")
expect_match("verify 2" "${out}" "party: phase finished")
expect_match("verify 2" "${out}" "replay OK")

# 4. Playback records nothing.
replay_files(before)
run_shell(out code --headless --replay "${replays}/mp4-11-2.mptr" --user-dir "${user}")
expect_ok("replay session" "${out}" "${code}")
expect_match("replay session" "${out}" "replay OK")
expect_match("replay session" "${out}" "party: phase finished")
if(out MATCHES "recording to|recorded ")
  message(FATAL_ERROR "replay session: a playback session recorded\n${out}")
endif()
replay_files(after)
if(NOT "${before}" STREQUAL "${after}")
  message(FATAL_ERROR "replay session changed the replays folder:\nbefore: ${before}\nafter: ${after}")
endif()

# 5. A start that fails leaves no file. The slot holds an image whose format version
# (0x01010101) FakeRuntime rejects at boot, so Session::start fails.
set(bad "${WORK_DIR}/bad_user")
string(ASCII 1 one)
file(MAKE_DIRECTORY "${bad}/saves/mp4/USA")
file(WRITE "${bad}/saves/mp4/USA/slot0.mptsave" "${one}${one}${one}${one}AAAAAAAA")
run_shell(out code --headless --title mp4 --seed 5 --frames 60 --user-dir "${bad}")
if(code EQUAL 0)
  message(FATAL_ERROR "failed start: the shell exited 0\n${out}")
endif()
file(GLOB_RECURSE left LIST_DIRECTORIES true RELATIVE "${bad}" "${bad}/*")
list(SORT left)
if(NOT "${left}" STREQUAL "saves;saves/mp4;saves/mp4/USA;saves/mp4/USA/slot0.mptsave")
  message(FATAL_ERROR "failed start left files behind: ${left}\n${out}")
endif()

# 6. --record (with --embed-save, so the media store is untouched) overrides the path: the
# summary goes next to that file and names it without
# any folder (it is outside the user dir), and nothing is auto-recorded.
replay_files(before)
run_shell(out code --headless --title mp4 --seed 12 --frames 600 --user-dir "${user}"
          --record "${WORK_DIR}/elsewhere/rec.mptr" --embed-save)
expect_ok("--record" "${out}" "${code}")
if(NOT EXISTS "${WORK_DIR}/elsewhere/rec.mptr" OR NOT EXISTS "${WORK_DIR}/elsewhere/rec.summary.txt")
  message(FATAL_ERROR "--record: rec.mptr or rec.summary.txt missing\n${out}")
endif()
file(READ "${WORK_DIR}/elsewhere/rec.summary.txt" summary)
if(NOT summary MATCHES "\nreplay rec\\.mptr\n" OR NOT summary MATCHES "\nphase finished\n")
  message(FATAL_ERROR "--record summary:\n${summary}")
endif()
replay_files(after)
if(NOT "${before}" STREQUAL "${after}")
  message(FATAL_ERROR "--record also auto-recorded:\nbefore: ${before}\nafter: ${after}")
endif()
run_shell(out code --verify-replay "${WORK_DIR}/elsewhere/rec.mptr" --user-dir "${user}")
expect_ok("verify --record" "${out}" "${code}")

message(STATUS "auto-record: party finished, replays verified, playback and failed start wrote nothing")
