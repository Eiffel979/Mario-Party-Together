# Mario Party Together

A single PC host application that plays every mainline Mario Party title natively, with online lobbies, a public room browser and custom rulesets. Read `docs/PLAN.md` before any non-trivial work.

## Hard rules

1. **Never commit Nintendo material.** That means no ROMs, ISOs, executables, extracted assets, textures, audio, text dumps or raw disassembly. Users supply their own dumps, and `asset-import` builds everything locally. This applies to tests and fixtures too: use synthetic data.
2. **No DRM circumvention or key extraction code**, and no links to game downloads.
3. **Stay in your lane.** Each path has an owner agent (see the table in `docs/PLAN.md` §6). Changes to `core/` interfaces (`ITitleRuntime`, `ITitleAdapter`, Party Director types) need an ADR in `docs/adr/` approved by `architect`.
4. **Determinism.** Runtimes must give identical output from identical inputs and seed. Never read wall-clock time, unseeded RNG or thread timing inside game logic.
5. **Host is authoritative.** The host's ruleset, RNG seed and state hash are the truth in online play.

## Stack

- C++20, CMake + vcpkg, SDL3 (window/input/audio)
- Rendering per runtime: RT64 for N64 and a GX→modern-API layer for GameCube/Wii, both behind the shell's presenter
- Ruleset files: JSON with a JSON Schema in `rules/schema/`
- Room-listing server: small standalone service in `server/`

## Conventions

- Content IDs: `<title>.<kind>.<snake_name>`, e.g. `mp3.minigame.hand_line_and_sinker`.
- Title IDs: `mp1`–`mp10`, `smp` (Super Mario Party), `mpss` (Superstars).
- One `docs/titles/<id>.md` per title, kept current by its runtime owner.
- Commits: small and scoped to one owner area, in imperative mood.

## Agent roster (`.claude/agents/`)

| Agent | Use it for |
|---|---|
| `architect` | Interfaces, ADRs, Party Director, cross-area design, resolving conflicts between agents |
| `runtime-n64` | MP1–3: recompilation, RT64 integration, N64 adapters |
| `runtime-gc-wii` | MP4–8: decomp ports, GX/SDK replacement layer, adapters |
| `runtime-modern` | MP9, MP10, SMP, MPSS: feasibility research, go/no-go ADRs, later integration |
| `rules-engine` | Ruleset schema, presets, validation, mapping rules onto adapter hooks |
| `netplay` | Lobby, lockstep sync, desync recovery, NAT traversal, room-listing server |
| `input-mapping` | Unified input, rebinding, per-title profiles, motion and GamePad remaps |
| `frontend-ui` | Launcher, game library, lobby/room browser screens, ruleset editor |
| `asset-import` | Dump selection, hash verification, local build/extraction pipeline |
| `qa-reviewer` | Tests, CI, determinism and compliance checks, pre-merge review |
