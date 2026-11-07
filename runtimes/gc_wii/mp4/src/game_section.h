/* Forced into every game-core object (the DOL's sources, msm and our port files): their writable data
 * goes to one range, the game module's registered state region (ADR 0008 section 10). Zero-initialised
 * data is kept out of .bss by -fno-zero-initialized-in-bss. See game_data.c.
 *
 * The anchor: LLVM gives an object's section the attributes of the first global it emits there, and
 * that can be read-only: a static the optimizer made constant because nothing writes it (board/audio.c's
 * boardMusTbl), or a const global (data_seg takes those too unless const_seg is set). lld-link then
 * linked that object's section apart from the game range, read-only: its writable globals (boardSeq,
 * for one) were neither saved nor hashed, and the first write to one crashed (P1-17). A used, written
 * static emitted first keeps the section writable. It exists on every platform, so the state hash
 * sees the same symbols; its value never changes. const data goes to .rdata as usual. */
#if defined(_WIN32)
#pragma data_seg(".data$mptgame$b")
#pragma bss_seg(".data$mptgame$b")
#pragma const_seg(".rdata$mptgame")
#else
#pragma clang section data="mptgame"
#endif
__attribute__((used)) static volatile unsigned char mpt_rw_anchor = 1;
