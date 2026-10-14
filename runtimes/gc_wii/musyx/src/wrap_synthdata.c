/* Compiles upstream synthdata.c with sndConvert32BitSDIRTo64BitSDIR allocating
 * through salMalloc/salFree (the game's SND_HOOKS, so the arena) instead of the
 * host heap (ADR 0008 §7). */
#include <stdlib.h>

#include "musyx/sal.h"

#define malloc(n) salMalloc(n)
#define free(p) salFree(p)
#include "synthdata.c"
