# ui/ include boundaries (ADR 0010 §3.1 and §4). Run with -DUI_DIR=<ui source dir>.
#  1. Nothing in ui/ includes a runtimes/ or adapters/ header.
#  2. ui/model/ includes neither RmlUi nor SDL, so it stays testable headless.

if(NOT UI_DIR)
  message(FATAL_ERROR "UI_DIR is not set")
endif()

file(GLOB_RECURSE _all "${UI_DIR}/*.h" "${UI_DIR}/*.hpp" "${UI_DIR}/*.cpp" "${UI_DIR}/*.cc")
set(_failures "")
foreach(_file IN LISTS _all)
  file(STRINGS "${_file}" _includes REGEX "^[ \t]*#[ \t]*include")
  file(RELATIVE_PATH _rel "${UI_DIR}" "${_file}")
  foreach(_line IN LISTS _includes)
    # Runtime and adapter headers: the directories, and the runtimes' public include
    # prefixes (mpt/gc/, mpt_gc/, mpt/gx/, mpt_gx/, mpt/musyx). The core contracts under
    # mpt/core/ (title_runtime.h, title_adapter.h) are core's public API and allowed.
    if(_line MATCHES "runtimes/|adapters/|mpt/runtime|mpt/adapter|mpt/gc/|mpt_gc/|mpt/gx/|mpt_gx/|mpt/musyx|gc_wii|n64|_runtime\\.h|_adapter\\.h"
       AND NOT _line MATCHES "[<\"]mpt/core/")
      # The bring-up test uses the qa-owned FakeRuntime from tests/support/ only.
      if(NOT (_rel MATCHES "^tests/" AND _line MATCHES "support/fake_"))
        list(APPEND _failures "${_rel}: ${_line}")
      endif()
    endif()
    if(_rel MATCHES "^model/" AND _line MATCHES "RmlUi|SDL")
      list(APPEND _failures "${_rel}: ${_line} (ui/model/ must not use RmlUi or SDL)")
    endif()
  endforeach()
endforeach()

if(_failures)
  list(JOIN _failures "\n  " _text)
  message(FATAL_ERROR "ui/ include boundary violated:\n  ${_text}")
endif()
list(LENGTH _all _count)
message(STATUS "ui include boundary: OK (${_count} files)")
