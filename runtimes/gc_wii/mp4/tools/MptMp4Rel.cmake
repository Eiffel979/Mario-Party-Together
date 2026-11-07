# REL isolation for the game module (P1-15 required pieces 1 and 2), shared by the module and its
# unit test so that the test exercises exactly the module's mechanism.
#
#   mpt_mp4_rel_setup()
#   mpt_mp4_add_rel(<name> SET <set> DIR <dir> TARGET_PREFIX <prefix> SOURCES <src>... [LINK <lib>...]
#                   [OPTIONS <compile option>...])
#   mpt_mp4_write_rel_table(<set> <output .c>)
#
# Each REL compiles to an object library whose writable data goes to the REL's own section (a
# forced-include header; .bss stays in that section through -fno-zero-initialized-in-bss), then
# tools/isolate_rel.cmake renames every external symbol it defines to mpt_rel_<name>__<symbol> and
# writes its info record (entry point, data range). <set>_LIBS and <set>_INFOS collect the archives
# and info sources to link; mpt_mp4_write_rel_table writes the overlay table for mpt_mp4_rel.h.

set(_MPT_MP4_REL_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/isolate_rel.cmake")

macro(mpt_mp4_rel_setup)
  # The LLVM tools must match the compiler. Look next to the compiler as invoked and next to its
  # real path (Debian/Ubuntu: /usr/bin/clang -> /usr/lib/llvm-<major>/bin/clang), and accept the
  # versioned names those distributions install without an unversioned llvm package.
  get_filename_component(_mpt_mp4_cc_dir "${CMAKE_C_COMPILER}" DIRECTORY)
  get_filename_component(_mpt_mp4_cc_real "${CMAKE_C_COMPILER}" REALPATH)
  get_filename_component(_mpt_mp4_cc_real_dir "${_mpt_mp4_cc_real}" DIRECTORY)
  string(REGEX MATCH "^[0-9]+" _mpt_mp4_cc_major "${CMAKE_C_COMPILER_VERSION}")
  set(_mpt_mp4_tool_hints "${_mpt_mp4_cc_dir}" "${_mpt_mp4_cc_real_dir}")
  if(_mpt_mp4_cc_major)
    list(APPEND _mpt_mp4_tool_hints "/usr/lib/llvm-${_mpt_mp4_cc_major}/bin")
  endif()
  foreach(_mpt_mp4_tool NM:llvm-nm OBJCOPY:llvm-objcopy AR:llvm-ar)
    string(REPLACE ":" ";" _mpt_mp4_tool "${_mpt_mp4_tool}")
    list(GET _mpt_mp4_tool 0 _mpt_mp4_var)
    list(GET _mpt_mp4_tool 1 _mpt_mp4_name)
    set(_mpt_mp4_names "${_mpt_mp4_name}")
    if(_mpt_mp4_cc_major)
      list(APPEND _mpt_mp4_names "${_mpt_mp4_name}-${_mpt_mp4_cc_major}")
    endif()
    find_program(MPT_MP4_${_mpt_mp4_var} NAMES ${_mpt_mp4_names} HINTS ${_mpt_mp4_tool_hints} REQUIRED)
  endforeach()
  if(WIN32)
    set(MPT_MP4_REL_FORMAT coff)
    set(MPT_MP4_REL_LIB_SUFFIX .lib)
    set(MPT_MP4_NOBSS /clang:-fno-zero-initialized-in-bss)
  else()
    set(MPT_MP4_REL_FORMAT elf)
    set(MPT_MP4_REL_LIB_SUFFIX .a)
    set(MPT_MP4_NOBSS -fno-zero-initialized-in-bss)
  endif()
endmacro()

function(mpt_mp4_add_rel name)
  cmake_parse_arguments(PARSE_ARGV 1 A "" "SET;DIR;TARGET_PREFIX" "SOURCES;LINK;OPTIONS")
  set(_tgt "${A_TARGET_PREFIX}${name}")
  add_library(${_tgt} OBJECT ${A_SOURCES})
  if(A_LINK)
    target_link_libraries(${_tgt} PRIVATE ${A_LINK})
  endif()
  set(_sect_h "${A_DIR}/${name}_section.h")
  if(MPT_MP4_REL_FORMAT STREQUAL "coff")
    # '$' separators: the linker sorts .data$... by suffix, and no REL name may interleave another's.
    # const data goes to .rdata, and a written anchor keeps the section writable: see
    # src/game_section.h (an optimized-constant first global would otherwise make it read-only).
    file(CONFIGURE OUTPUT "${_sect_h}" CONTENT "#pragma data_seg(\".data$mptrel$${name}$b\")
#pragma bss_seg(\".data$mptrel$${name}$b\")
#pragma const_seg(\".rdata$mptrel\")
__attribute__((used)) static volatile unsigned char mpt_rw_anchor = 1;
")
    set(_fi "/FI${_sect_h}")
  else()
    file(CONFIGURE OUTPUT "${_sect_h}" CONTENT "#pragma clang section data=\"mptrel_${name}\"
__attribute__((used)) static volatile unsigned char mpt_rw_anchor = 1;
")
    set(_fi "SHELL:-include \"${_sect_h}\"")  # SHELL: so CMake keeps each -include
  endif()
  target_compile_options(${_tgt} PRIVATE ${MPT_MP4_NOBSS} ${_fi} ${A_OPTIONS})
  set(_lib "${A_DIR}/${name}${MPT_MP4_REL_LIB_SUFFIX}")
  set(_info "${A_DIR}/${name}_info.c")
  add_custom_command(OUTPUT "${_lib}" "${_info}"
    COMMAND "${CMAKE_COMMAND}" "-DREL=${name}" "-DFORMAT=${MPT_MP4_REL_FORMAT}" "-DNM=${MPT_MP4_NM}"
            "-DOBJCOPY=${MPT_MP4_OBJCOPY}" "-DAR=${MPT_MP4_AR}" "-DOBJECTS=$<JOIN:$<TARGET_OBJECTS:${_tgt}>,|>"
            "-DOUT_LIB=${_lib}" "-DOUT_INFO=${_info}" -P "${_MPT_MP4_REL_SCRIPT}"
    DEPENDS ${_tgt} $<TARGET_OBJECTS:${_tgt}> "${_MPT_MP4_REL_SCRIPT}"
    COMMENT "Isolating REL ${name}"
    VERBATIM) # quotes the "|"-joined object list for the shell
  set(${A_SET}_LIBS ${${A_SET}_LIBS} "${_lib}" PARENT_SCOPE)
  set(${A_SET}_INFOS ${${A_SET}_INFOS} "${_info}" PARENT_SCOPE)
  set(${A_SET}_NAMES ${${A_SET}_NAMES} "${name}" PARENT_SCOPE)
endfunction()

function(mpt_mp4_write_rel_table set output)
  set(_decls "")
  set(_entries "")
  foreach(_rel IN LISTS ${set}_NAMES)
    string(APPEND _decls "extern const MptMp4RelInfo mpt_rel_info_${_rel};\n")
    string(APPEND _entries "    &mpt_rel_info_${_rel},\n")
  endforeach()
  get_filename_component(_dir "${output}" DIRECTORY)
  file(MAKE_DIRECTORY "${_dir}")
  file(CONFIGURE OUTPUT "${output}" CONTENT
"/* Generated by runtimes/gc_wii/mp4/tools/MptMp4Rel.cmake. Do not edit. */
#include \"mpt_mp4_rel.h\"
${_decls}
const MptMp4RelInfo* const mpt_mp4_rel_table[] = {
${_entries}    0
};
const unsigned mpt_mp4_rel_count = sizeof(mpt_mp4_rel_table) / sizeof(mpt_mp4_rel_table[0]) - 1;
")
endfunction()
