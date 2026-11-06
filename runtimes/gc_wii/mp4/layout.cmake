# Fixed addresses for fresh-process save states (ADR 0008 section 11), shared by the game module's link
# (mp4/CMakeLists.txt) and the host runtime (mp4/host), which checks them at boot. The same values on
# Windows and Linux, inside the x86-64 user address space and away from where either OS puts images,
# heaps and stacks by default.
set(MPT_MP4_MODULE_BASE "0x500000000000")  # mp4_game.dll / .so image base
set(MPT_MP4_ARENA_BASE "0x400000000000")   # the SDK arena (SdkConfig::arena_base)
# Emulated main RAM for OSAlloc's arena (ADR 0008 amendment, PR #44): a build constant inside the
# module, never a setting. Party Board's TARGET_PC game heaps are 4x the console's (78 MiB); see
# docs/titles/mp4.md section 19 for the sizing.
set(MPT_MP4_MAIN_RAM_BYTES "117440512")      # 112 MiB
