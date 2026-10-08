# ADR 0009: MP4 game-source dependency and local build model

**Status:** Accepted
Author: architect, 2026-10-08. Rewritten after P1-01 (`docs/titles/mp4.md`) on 2026-10-08. Approved by: architect, 2026-10-08.
(The file keeps its original name, `...-mp4-decomp-dependency-...`, so existing links stay valid.)

## Context
ADR 0001 and PLAN §2.1 say releases never contain Nintendo code binaries and that runtimes are built **locally from the user's dump**. `VerifiedDump::build_dir` already describes "a local cache with the built runtime".

A decomp port compiles the community's reverse-engineered C source of the game. Even though that source is not a copy of a dump, its compiled form is the game's logic. Several decomp-based PC ports (for example Ship of Harkinian) ship that compiled code and require the user's ROM only for assets. That model contradicts our ADR 0001 as written.

P1-01 (`docs/titles/mp4.md` §1–5, §13) established:
- **`mariopartyrd/marioparty4`** (the matching decomp) is complete and fully matching for `GMPE01_00` and `GMPE01_01`. `main` is at `147b165a83187ac9e6cfdc3bf52f2e73437b1ffd` and has **no license**. A CC0 1.0 license is proposed in open PR #663, not merged. Its matching build `#include`s about 20 data blobs that decomp-toolkit extracts from the user's `main.dol` and `bootDll.rel`, so it cannot compile without a dump.
- **`mariopartyrd/partyboard`** ("Party Board") is the same organisation's PC port. Its game sources are forked from the decomp and adapted for PC. It is licensed **CC0 1.0** today. Under its PC build it reads those blobs **at runtime** from the user's disc data through a per-revision address table, so the compiled game code contains no dump-derived bytes. It supports `GMPE01_00` and `GMPE01_01`. It has **no audio** (MusyX init is disabled), **no THP movies**, and memory-card saves that do not fit our host-owned model. Its OS time layer (aurora) reads the host clock.
- Party Board's submodules: `AxioDL/musyx` (CC0 1.0), `encounter/aurora` (MIT) and `higan-emu/libco` (ISC, to be confirmed if adopted).
- The developer's local copy of `marioparty4` is a zip with no `.git` and an empty `extern/musyx`. Sampled blobs match `main` at `147b165`, but a zip cannot prove a commit.
- The developer's dump is USA Rev 0 (`GMPE01`, version byte `0x00`), the primary matching target.

## Decision

### 0. Decisions this ADR records
- **D1 (user):** the architecture is **strict local build on the user's machine** (option A). Phase 1 delivers **developer-only builds** (option C). Prebuilt game code (option B) is not pursued. It would need an ADR superseding ADR 0001.
- **D2 (user, with the choice of source delegated to the coordinator and architect):** depend on community MP4 game source **as a pinned external, never vendored**. Following P1-01's option (a), the game source is **Party Board**. `marioparty4` is a **read-only reference only**: developers may read it, but nothing is fetched, compiled or patched from it.
- **D3 (user):** Phase 1 supports **`GMPE01_00` only**.

### 1. The dependency
`runtimes/gc_wii/mp4/decomp.lock` records every external we build from, and `docs/titles/mp4.md` §1 mirrors it:

| Name | Repository | Pin (full commit hash) | License (SPDX) | Role |
|---|---|---|---|---|
| `partyboard` | `https://github.com/mariopartyrd/partyboard` | `98ac07fd1e8f7ab938bbb6fe75789fad99419010` | `CC0-1.0` | MP4 game source: game core, RELs, `msm`, MSL math, THP, and selected port files (§2) |
| `musyx` | `https://github.com/AxioDL/musyx` | `2a0e01aeff0d06a9ee9e12966c435cb75144af9e` (the commit Party Board's pin records) | `CC0-1.0` | Sound library, run through its PC synth in pull mode (ADR 0007) |
| `aurora` | `https://github.com/encounter/aurora` | `514339438178ef2bed1b14e5149d90ece0c6e0cc` (Party Board's pin). **Only if spike S1 adopts it** (ADR 0007 §6). | `MIT` | GX, GD and MTX only |
| `libco` | `https://github.com/higan-emu/libco` | `e18e09d634d612a01781168ad4d76be10a7e3bad` (Party Board's pin). **Only if P1-13 adopts it** (ADR 0008 §1). | `ISC` (confirm when adopted) | Coroutines |

Rules:
- **Pins are full upstream commit hashes**, never tags, branches or archive checksums. A submodule is pinned to the commit the parent's pin records unless the lock says otherwise and gives a reason. Submodules that are not in the lock are not fetched.
- **Never vendored**, never committed as a submodule. The recipe fetches by hash (`git clone --filter=blob:none`, `git checkout <sha>`) into **`external/`** at the repository root for developer builds (gitignored and rejected by the compliance scanner, P1-20e1), or into asset-import's user cache for builds started from the launcher, and refuses to build if `git rev-parse HEAD` differs from the lock.
- **A user-supplied archive** (for example a GitHub zip) is accepted only if its computed git **tree hash** equals the tree of the locked commit, with submodule paths checked against the locked gitlinks and each submodule verified the same way. Otherwise the recipe asks for a git checkout. The developer's existing `marioparty4` zip is irrelevant to the build, since `marioparty4` is not a build input.
- **Local override (development only).** A developer may point the recipe at an existing checkout outside the repo, for example with `MPT_MP4_SOURCE_DIR`. If the tree hash does not match the lock, the manifest marks the build **`unpinned`**. Such a build never produces committed golden trails, exit evidence or known-good results, and never joins netplay.
- **Bumping a pin** is a PR that changes `decomp.lock` and `docs/titles/mp4.md` together, re-records the affected golden trails, and passes the manual dump lane (ADR 0006).
- **Our changes**, in order of preference:
  1. SDK-level replacement (ADR 0007).
  2. Our own target macro, compile-time shims and wrapper headers.
  3. Small patch files against the CC0 pins. Patches hold only our changes plus minimal context (PLAN §2.3), never data taken from a dump.

### 2. What we build from Party Board
- **We build:** its game sources (`src/game`, the RELs, `msm`, `libhu`, MSL math, THP), compiled with its PC macros where they help (`NON_MATCHING`, `BYTESWAPPING`) and our own target macro where its `TARGET_PC` code assumes its port layer. Two of its port files are reused, adapted to our layer, from the pin, rather than rewritten:
  - the runtime DOL/REL data reader (`dolassets`);
  - the 64-bit HSF model conversion (`byteswap`).
- **We do not use:** its main loop, frame limiter, UI, settings, achievements, per-REL shared-library loader, or aurora's OS/VI/PAD/DVD/CARD/SI parts. Our SDK layer (ADR 0007) and determinism model (ADR 0008) replace them.
- **New work, because Party Board lacks it:**
  - MusyX audio (ADR 0007);
  - THP movie decoding (ADR 0007);
  - host-owned memory-card saves (ADR 0004).

### 3. The build (option A as the architecture)
The game module is built **on the user's machine** by asset-import, through a `BuildRecipe` (P1-10) that runtime-gc-wii supplies:
1. Check that the dump identifies as `GMPE01_00` (disc ID `GMPE01`, version byte `0x00`, known hash). Any other MP4 revision or region is refused, with a message naming it, before any build work.
2. Extract the disc file system into the cache (P1-09). The extracted DOL, RELs and files stay in `build_dir`, on the user's machine only.
3. Fetch and verify the pins (§1).
4. Compile Party Board's game sources, MusyX and our shims with Clang (ADR 0008), with every REL statically linked into **one game module** and symbol-isolated (P1-15). Link it into `mp4_game.dll` / `.so` in `build_dir`.
5. Write a manifest: dump sha256, game version (`GMPE01_00`), every pin (or `unpinned`), recipe version, toolchain version.

**There is no data-extraction step.** Data blobs are read **at runtime** from the extracted DOL/RELs in `build_dir` through a per-revision address table. The table holds offsets and sizes only, never bytes. The game module therefore contains **no dump-derived bytes**.

`Mp4Runtime` (host side, in our binary) loads that module at `boot()` through a narrow C ABI. How it gets a fixed load base is ADR 0008 §11. Our binaries hold only our code. That keeps ADR 0001 intact.

### 4. Phase 1 delivery (option C)
Phase 1 builds are **developer-only**. The recipe assumes a Clang toolchain is installed. Bundling a toolchain, or another end-user path, is decided before the first release beyond developers. No release artifacts are produced in Phase 1, and no game module leaves a developer's machine.

### 5. Standing rules (replacing the interim rules I1–I4 of the earlier draft)
The earlier draft had interim rules I1–I4 because no licensed source existed. With CC0 pins they are reassessed:

| Rule | Was | Now |
|---|---|---|
| I1: nothing from upstream committed | license-driven | **Kept, for architectural reasons.** We still never vendor game source (ownership, repo size, takedown exposure, and the code is the game's logic). It is no longer a license constraint for the CC0/MIT pins. |
| I2: no patch files carrying upstream lines | license-driven | **Lifted for the pinned CC0/MIT/ISC sources.** Patch files against `partyboard`, `musyx`, `aurora` and `libco` are allowed under §1. **Kept for `marioparty4`**: no patch, excerpt or file from it is committed while it has no license. |
| I3: no quoting upstream source in docs | license-driven | **Relaxed.** Identifiers, file paths and short excerpts from the pinned CC0/MIT sources may appear in docs where a decision depends on them. For `marioparty4`, identifiers and paths only, no code excerpts. |
| I4: no distribution beyond developers | license-driven | **Kept as a Phase 1 scope rule (D1)**, no longer a license blocker for the pins. Lifting it needs (a) the end-user delivery decision (D1, toolchain) and (b) the user's confirmation that Party Board's CC0 grant meets their D2 condition, or that `marioparty4` PR #663 has merged (see the Review). |

## Fallback
**`marioparty4` pinned at the first commit after PR #663 (CC0) merges.** Switching to it is an amendment to this ADR. It means a new lock, re-applying our PC changes (the decomp has `NON_MATCHING` fixes but no PC paths), adding back a runtime data reader, and re-recording every golden trail. We switch only if Party Board stops being viable (a license change at a future commit, which does not affect our pinned commit, or the project being abandoned with blocking bugs). PowerPC static recompilation stays the last resort.

## Alternatives
- **Pin `marioparty4` now.** Rejected: no license on `main`, so building on it is not covered.
- **Wait for PR #663, then pin the decomp (P1-01 option b).** Rejected as the primary path. It blocks P1-15 on an external merge with no date, and it would re-do PC porting work that Party Board has already done under CC0. It is kept as the fallback.
- **Vendor the game source in our repo.** Rejected (§5, I1).
- **Use Party Board's per-REL shared libraries.** Rejected as the default: about 95 OS-loaded modules each get their own ASLR base, which breaks fresh-process save states (ADR 0008 §11). It is kept as a fallback if symbol isolation in one module proves fragile, at the cost of narrowing E5.
- **Adopt Party Board whole, including aurora's OS/DVD/CARD layers.** Rejected: aurora's OS time reads the host clock, and its DVD/CARD paths bypass host-owned save media (ADRs 0004, 0008).
- **Pin the developer's zip by checksum.** Rejected: it cannot be traced to a commit, it is not the chosen source, and it lacks submodules.

## Consequences
- The game module no longer needs a dump in order to **compile**. D1 still keeps the build on the user's machine, and Phase 1 adds no prebuilt path. Public CI **may** compile the game module from the pins as a **compile-only check**: no dump, no tests that run it, no artifacts kept or uploaded (P1-21, optional). Running the real game stays on the manual dump lane (ADR 0006).
- `VerifiedDump::build_dir` holds the extracted disc data and the game module. Netplay join checks (Phase 2) compare the dump hash **and** the build manifest (game version, all pins, recipe, toolchain). An `unpinned` build never joins.
- Audio, THP movies and save media are our work, budgeted in P1-13 (ADR 0007) and P1-16 (ADR 0004).
- Supporting `GMPE01_01` later costs a known-dumps entry, an address-table variant and its own replay fixtures. Party Board already supports it. PAL/JP wait for upstream.
- **Residual provenance risk:** Party Board's game sources descend from `marioparty4`, which has no license yet. Party Board's CC0 grant comes from the same organisation, and the decomp's CC0 PR signals the same intent, so the risk is low. It is tracked in the Phase 1 plan as R11, and it closes when PR #663 merges.

## Review (architect, 2026-10-08)
**Verdict: Accepted.** The earlier draft stayed Proposed for two reasons, and both are now resolved for the sources we actually build:
1. **Pins.** Every source is pinned by a full upstream commit hash from P1-01 (§1). aurora and libco are conditional on S1 and P1-13, and their pins are already known.
2. **License.** The pinned game source is CC0 1.0, musyx is CC0 1.0, aurora is MIT and libco is ISC (to be confirmed if adopted). These clearly permit fetching, patching and compiling locally. The unlicensed `marioparty4` is no longer a build input.

The build-success condition in the earlier draft ("a clean build from the pins") belongs to P1-15's acceptance test, not to this decision, and the fallback covers the case where it fails.

**Left for the user (does not block Phase 1):** the user's D2 answer said the license must be "confirmed with upstream before any distribution beyond developers". That was written about `marioparty4`. Before any distribution beyond developers, the user should say whether Party Board's CC0 grant satisfies it, or whether they still want PR #663 merged (or another explicit upstream confirmation). I4 keeps Phase 1 developer-only either way.
