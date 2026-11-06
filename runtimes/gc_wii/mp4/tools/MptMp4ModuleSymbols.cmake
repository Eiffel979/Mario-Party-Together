# mpt_mp4_module_symbols: the game module's symbol-table reader (src/module_symbols.cpp), as a small
# Clang-built library so the module and a public-CI test share it (#55 review). Host-side code: no game
# section, no game headers. Hidden visibility, so nothing of it is exported from mp4_game.
#
#   include(MptMp4ModuleSymbols.cmake)   # defines the target once
#   target_link_libraries(<target> PRIVATE mpt_mp4_module_symbols)
#   mpt_mp4_link_symtab(<target>)          # keep a symbol table in <target>'s image for it to read
if(NOT TARGET mpt_mp4_module_symbols)
  add_library(mpt_mp4_module_symbols STATIC "${CMAKE_CURRENT_LIST_DIR}/../src/module_symbols.cpp"
                                            "${CMAKE_CURRENT_LIST_DIR}/../src/module_symbols.h")
  target_include_directories(mpt_mp4_module_symbols PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src")
  target_compile_features(mpt_mp4_module_symbols PUBLIC cxx_std_20)
  set_target_properties(mpt_mp4_module_symbols PROPERTIES CXX_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(mpt_mp4_module_symbols PRIVATE /W4 /permissive- /utf-8 /WX)
  else()
    target_compile_options(mpt_mp4_module_symbols PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Werror)
    target_link_libraries(mpt_mp4_module_symbols PUBLIC ${CMAKE_DL_LIBS})
  endif()
endif()

# The image keeps a symbol table the reader can find: lld-link writes a COFF one with /DEBUG:SYMTAB
# (outside every mapped section, so the layout does not change); ELF keeps .symtab unless stripped.
function(mpt_mp4_link_symtab target)
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_link_options(${target} PRIVATE /DEBUG:SYMTAB)
  endif()
endfunction()
