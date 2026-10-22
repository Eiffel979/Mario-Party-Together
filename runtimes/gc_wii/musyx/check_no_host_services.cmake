# Link-time check for the musyx merge condition (b) of the ADR 0009 amendment after
# P1-13b, CLAUDE.md rule 4 and ADR 0008 §7: the linked musyx objects reference no
# host thread, mutex, condition variable, event, sleep, clock, RNG or heap.
# Rendering may only happen when the SDK's AI tick calls it, inside stepFrame, and
# every allocation goes through the game's SND_HOOKS into the arena.
#
#   cmake -DNM=<llvm-nm|nm> "-DFILES=a.lib;b.obj" -P check_no_host_services.cmake
# or, for more files than a command line holds, -DFILES_FROM=<file with one path per line>.
if(FILES_FROM)
  file(STRINGS "${FILES_FROM}" FILES)
endif()
if(NOT NM OR NOT FILES)
  message(FATAL_ERROR "usage: -DNM=<nm> -DFILES=<objects or archives> | -DFILES_FROM=<list file>")
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
    # The symbol is the last field, optionally with the __imp_ prefix of a DLL import.
    if(_line MATCHES "[ \t](__imp_)?_?(pthread_[a-z_]+|thrd_[a-z_]+|mtx_[a-z_]+|cnd_[a-z_]+|SDL_[A-Za-z0-9_]+)$"
       OR _line MATCHES "[ \t](__imp_)?(CreateMutex[AW]?|CreateMutexEx[AW]?|ReleaseMutex|CreateThread|_beginthread(ex)?|CreateEvent[AW]?|SetEvent|ResetEvent|WaitFor(Single|Multiple)Object(s)?(Ex)?|(Initialize|Enter|Leave|Delete|TryEnter)CriticalSection(Ex|AndSpinCount)?|InitializeSRWLock|AcquireSRWLock[A-Za-z]+|ReleaseSRWLock[A-Za-z]+|SleepConditionVariable[A-Z]+|WakeConditionVariable|WakeAllConditionVariable|Sleep(Ex)?)$"
       OR _line MATCHES "[ \t](__imp_)?_?(time|_time64|_time32|clock|clock_gettime|gettimeofday|timeGetTime|GetTickCount(64)?|QueryPerformanceCounter|GetSystemTime(AsFileTime)?|rand|srand|rand_s)$"
       OR _line MATCHES "[ \t](__imp_)?_?(malloc|calloc|realloc|free|_aligned_malloc|_aligned_free|_malloc_base|_free_base)$")
      list(APPEND _bad "${_file}: ${_line}")
    endif()
  endforeach()
endforeach()

if(_bad)
  string(REPLACE ";" "\n  " _msg "${_bad}")
  message(FATAL_ERROR "host thread, lock, clock, RNG or heap references in linked musyx code:\n  ${_msg}")
endif()
message(STATUS "no host thread, lock, clock, RNG or heap references")
