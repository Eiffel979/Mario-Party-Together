# P1-14 opening gate (G-a to G-e)

**Owner:** runtime-gc-wii
**Status:** in progress (Windows checks running). This file is committed as checks complete.
**Inputs:** ADR 0007 §6, ADR 0008, ADR 0009, `docs/spikes/s1-gx-backend.md` §8, Phase 1 plan (P1-14, R17).
**Pins:** aurora `514339438178ef2bed1b14e5149d90ece0c6e0cc`, Dawn `13abc3bc8ea2d3c2050f9e77a12d012108ceee24`.

## Interim results

| Check | Windows | Linux |
|---|---|---|
| G-a | Dawn fetched by commit, all 19 `DEPS` entries pinned by full SHA and verified after fetch. Dawn built from source with clang-cl in 13 min 4 s (1105 steps). aurora GX front end, renderer and MTX built and linked against it without `aurora_core`. | Not run (no Linux machine). |
| G-b to G-e | Running. | Not run. |

Nothing third-party is committed. All checkouts and build trees live outside the repo.
