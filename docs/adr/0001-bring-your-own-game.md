# ADR 0001: Bring-your-own-game, no Nintendo material in the repo

**Status:** Accepted

## Context
The project aims to play every mainline Mario Party title in one app without separate emulators. The games' code, art and audio are Nintendo's copyrighted material. Projects that ship such content get taken down. Projects that make users supply their own dumps (Ship of Harkinian, Zelda64Recomp, decomp-based ports) have been able to operate.

## Decision
The repo and releases contain only original code, recompiler configs, symbol maps and patches. Users provide dumps of games they own. `asset-import` verifies them by hash and builds native runtimes and assets locally. No DRM circumvention or key extraction is ever included.

## Alternatives
- Bundle games or assets: rejected (copyright infringement, certain takedown).
- Download assets from a server: rejected (same problem, plus hosting liability).

## Consequences
- Players need their own copy of each game they want to play. Netplay checks dump hashes so all peers run identical builds.
- Titles that can't be supported without keys or firmware (likely the Switch titles) may be cut. See ADR on each title's go/no-go.
