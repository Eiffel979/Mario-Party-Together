# Fails if the pure mapping core (everything in input/ except the SDL backend)
# includes or calls SDL, or pulls in clocks, RNG or threads. Run as:
#   cmake -DINPUT_DIR=<input/> -P check_no_sdl.cmake
file(GLOB_RECURSE sources
  "${INPUT_DIR}/include/*.h"
  "${INPUT_DIR}/src/*.cpp"
)
set(forbidden
  "#include[ \t]*[<\"]SDL"
  "SDL_[A-Za-z0-9_]+[ \t]*\\("
  "#include[ \t]*<(chrono|ctime|random|thread|mutex|atomic|future)>"
  "std::(rand|srand|time|chrono|thread|random_device)"
  "[^a-z_]time[ \t]*\\("
)
set(problems "")
foreach(file IN LISTS sources)
  get_filename_component(name "${file}" NAME)
  if(name MATCHES "^sdl_input\\.")
    continue()
  endif()
  file(READ "${file}" text)
  foreach(pattern IN LISTS forbidden)
    string(REGEX MATCH "${pattern}" hit "${text}")
    if(hit)
      string(APPEND problems "  ${file}: '${hit}'\n")
    endif()
  endforeach()
endforeach()
if(problems)
  message(FATAL_ERROR "input/ mapping core must stay pure (no SDL, clock, RNG or threads):\n${problems}")
endif()
list(LENGTH sources count)
message(STATUS "input core purity: ${count} file(s) checked, OK")
