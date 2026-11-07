# The floating-point model of game-facing code (ADR 0008 section 4): strict IEEE, no contraction, no
# fast-math, an SSE2 baseline with no FMA3 assumed. sdk/CMakeLists.txt applies it to the SDK and
# everything linked with it; mp4/build_id.cmake records it in the MP4 build fingerprint.
#
# mpt_gc_fp_flags(<out_var> <msvc_frontend>): the flags for clang-cl (TRUE) or clang (FALSE).
function(mpt_gc_fp_flags out_var msvc_frontend)
  if(msvc_frontend)
    set(_flags /clang:-ffp-contract=off /clang:-fno-fast-math /clang:-fno-unsafe-math-optimizations)
  else()
    set(_flags -ffp-contract=off -fno-fast-math -fno-unsafe-math-optimizations -msse2 -mfpmath=sse
               -fcf-protection=none)
  endif()
  set(${out_var} ${_flags} PARENT_SCOPE)
endfunction()
