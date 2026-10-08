# Mario Party Together — Project Plan

## 1. Vision

One PC application that plays every mainline Mario Party title (MP1–MP10, Super Mario Party, Mario Party Superstars) with:

- **Native online multiplayer.** The host's PC runs the game and owns the rules. Friends join through a lobby. Rooms can be listed publicly in a room browser so strangers can join.
- **Rulesets.** Built-in presets ("Mario Party 1 Only", "MP2 Only", …, "Greatest Hits") plus fully custom rulesets: minigame pool, boards, turn count, minigame time limits, star cost and count, starting coins, bonus stars, and so on.
- **One host "engine".** Each game keeps its original graphics. All of them run inside one shell that owns the window, audio, input, networking, rules and UI.
- **Unified controls.** Every title plays on keyboard/mouse or a standard controller. Titles built around motion or special hardware (MP8/MP9 Wii Remote, MP10 GamePad, Super Mario Party Joy-Con) get remapped control schemes.

## 2. Ground rules (non-negotiable)

1. **Bring your own game.** The repo and releases never contain Nintendo code binaries, ROMs, ISOs, textures, models, audio or text dumps. Users point the app at dumps of games they own. The app verifies them by hash and builds the native runtime and assets locally on first run. This is how Ship of Harkinian, Zelda64Recomp and the decomp-based PC ports operate.
2. **No piracy tooling.** No key extraction, no DRM circumvention code, no download links to games.
3. **Original code only in the repo.** Recompiler configs, symbol maps, patches and our own C/C++ are fine. Copied disassembly or dumped data are not.
4. **Branding.** "Mario Party" is a Nintendo trademark. Before any public release, pick a non-infringing project name and avoid Nintendo logos and art in the launcher.

## 3. How "no emulators" actually works

There are three technical strategies. Which one a title uses depends on how much reverse engineering already exists for it:

| Strategy | What it is | Feasible for |
|---|---|---|
| **Static recompilation** | Translate the original MIPS/PowerPC machine code into C ahead of time and link it against our host libraries (e.g. N64Recomp + RT64 renderer). | N64: MP1, MP2, MP3 |
| **Decompilation port** | Use the community decomp's matching C source. Replace the console SDK (GX graphics, audio, pad, DVD) with PC implementations. | MP4 (fully decompiled, May 2025). MP5–MP8 as their decomps mature. |
| **Embedded high-level emulation core** | Last resort: embed a CPU/GPU core as a library behind the same interface. The user never sees "an emulator", but it is one internally. | MP9 (Wii), MP10 (Wii U). Switch titles only if legally viable. |

Every title, whatever its strategy, sits behind the same two interfaces (§5). That is what makes the shell one engine.

### Title feasibility snapshot (verify at the start of each phase)

| Title | Platform | Strategy | Risk |
|---|---|---|---|
| MP1, MP2, MP3 | N64 | Recomp, with decomp symbols | Low–Med |
| MP4 | GameCube | Decomp port (100% decompiled) | **Low (first target)** |
| MP5, MP6, MP7 | GameCube | Decomp port (decomps in progress: mariopartyrd) | Med |
| MP8 | Wii | Decomp port + motion remap | Med–High |
| MP9 | Wii | Decomp if one exists, otherwise embedded core | High |
| MP10 | Wii U | Embedded core | High |
| Super Mario Party, Superstars | Switch | Research only | **Very high.** No decomps. Nintendo has taken legal action against Switch emulation (Yuzu, 2024). May be cut. |

## 4. Architecture

```
┌──────────────────────── Host Shell (C++20, SDL3) ────────────────────────┐
│  UI / Launcher  │  Lobby & Room Browser client  │  Ruleset editor          │
│─────────────────┴───────────────────────────────┴──────────────────────────│
│  Party Director  — canonical party state (players, coins, stars, turns)  │
│                   — drives the session according to the active Ruleset    │
│─────────────────────────────────────────────────────────────────────────────│
│  Rules Engine │ Netplay (lockstep, host-authoritative) │ Input Mapper      │
│─────────────────────────────────────────────────────────────────────────────│
│        ITitleAdapter  (boards, minigames, rule hooks, results)            │
│        ITitleRuntime  (boot, step frame, input in, A/V out, save state)   │
│─────────────────────────────────────────────────────────────────────────────│
│  runtimes/n64 (recomp+RT64) │ runtimes/gc_wii (decomp+GX layer) │ modern  │
└─────────────────────────────────────────────────────────────────────────────┘
        ▲ built locally from the user's own dumps by asset-import
```

### Key design decisions

- **The Party Director owns the truth.** Each title's own party logic runs as normal, but the adapter mirrors its state into the Director and applies ruleset overrides through hooks (starting coins, star cost, turn count, minigame pool, timers).
- **Cross-game play is a later phase.** A board from game X triggering a minigame from game Y works like this:
  1. The Director suspends runtime X with a save state.
  2. It boots minigame Y directly through adapter Y's `launchMinigame()`.
  3. It collects the results and maps them into canonical coins and rankings.
  4. It resumes X and writes the results back through adapter X.
  
  Single-title rulesets come first. They need nothing beyond hooks.
- **Netplay model: deterministic lockstep, host-authoritative.** Each title runs on every peer. Peers exchange only inputs, frame-locked with input delay (as in Dolphin netplay). The host owns the ruleset, the RNG seed and the "truth" for desync recovery. When a desync is detected, the host's save state is resent. Rollback can come later if latency is a problem. *Alternative considered:* stream video from the host. That is simpler to make correct but costs bandwidth and latency. Keep it as a fallback mode.
- **Room browser.** A tiny public listing service (`/server`) keeps room metadata and helps with NAT traversal (STUN, plus a TURN relay as fallback). Games always run on the host's PC, never on our server.
- **Determinism is a feature requirement.** Every runtime must produce identical frames from identical inputs plus seed. CI checks this.

## 5. Core contracts (owned by the architect; changed only via ADR)

```cpp
struct ITitleRuntime {
  virtual TitleId id() const = 0;
  virtual Result boot(const VerifiedDump&, const RuntimeConfig&) = 0;
  virtual void   stepFrame(const InputFrame& inputs) = 0;   // deterministic
  virtual FrameOutput video() = 0;  virtual AudioChunk audio() = 0;
  virtual SaveState saveState() = 0; virtual void loadState(const SaveState&) = 0;
  virtual uint64_t stateHash() = 0;                         // desync detection
};

struct ITitleAdapter {
  virtual Catalog catalog() = 0;                 // boards, minigames (stable IDs), characters
  virtual void applyRuleset(const ResolvedRuleset&) = 0;    // hooks/patches
  virtual PartySnapshot readParty() = 0;          // → Party Director
  virtual void writeParty(const PartySnapshot&) = 0;
  virtual void launchMinigame(MinigameId, const MinigameSetup&) = 0; // phase 5
  virtual std::optional<MinigameResult> pollMinigameResult() = 0;
};
```

Stable content IDs: `mp4.board.toads_midway_madness` and `mp2.minigame.bumper_balls`.

The sketch above is simplified. The real headers are in `core/include/mpt/core/`; ADR 0002 records the v0 contract and how it differs from this sketch.

## 6. Repository layout and ownership

| Path | Contents | Owner agent |
|---|---|---|
| `core/` | Shell, app lifecycle, Party Director, interfaces | `architect` |
| `rules/` | Ruleset schema, presets, validator | `rules-engine` |
| `net/`, `server/` | Netplay client, lobby, room-listing service | `netplay` |
| `input/` | Input abstraction, per-title mapping profiles | `input-mapping` |
| `ui/` | Launcher, lobby, room browser, ruleset editor | `frontend-ui` |
| `asset-import/` | Dump hashing/verification, local build and extraction | `asset-import` |
| `runtimes/n64/`, `adapters/mp1..mp3/` | N64 titles | `runtime-n64` |
| `runtimes/gc_wii/`, `adapters/mp4..mp8/` | GameCube/Wii titles | `runtime-gc-wii` |
| `runtimes/modern/`, `adapters/mp9, mp10, smp, mpss/` | Feasibility research and runtimes | `runtime-modern` |
| `tests/`, `.github/` | Test harnesses, CI, determinism checks | `qa-reviewer` |
| `docs/adr/`, `docs/titles/` | Decisions, per-title notes | everyone; ADRs approved by `architect` |

## 7. Roadmap

| Phase | Goal | Exit criteria |
|---|---|---|
| **0. Foundations** | CMake/vcpkg, SDL3 shell, CI, interfaces, asset-import skeleton, compliance checks | Empty shell boots on Windows/Linux. CI blocks commits containing binaries or dumps. |
| **1. Vertical slice: MP4** | MP4 decomp running behind `ITitleRuntime` with a PC GX layer | Full local 4-player party on keyboard/controller. Deterministic replay test passes. |
| **2. Online + rulesets (single-title)** | Lobby, lockstep netplay, room browser, ruleset schema + MP4 presets and custom options | 4 players on separate PCs finish a 20-turn MP4 party with a custom ruleset and no desync. |
| **3. N64 trilogy** | MP1–3 via recomp. Adapters and presets. Fix MP1's analog-stick-spin minigames for keyboard. | MP1/2/3 Only presets playable online. |
| **4. GameCube/Wii rest** | MP5, 6, 7 (decomp-gated), MP8 with motion remap | Each title playable as its own preset. |
| **5. Cross-game parties** | Party Director suspend/launch/resume; mixed minigame pools and "Greatest Hits" presets | Mixed-pool ruleset across ≥3 titles playable online. |
| **6. Wii/Wii U** | MP9 and MP10 feasibility study, then integration | Go/no-go ADR, then playable if go. |
| **7. Switch titles** | Legal and technical research only | Go/no-go ADR. Default assumption: out of scope. |

## 8. Working agreement for agents

- Every change stays inside its owner's paths. Cross-boundary changes go through the owner, or through `architect` for interfaces.
- Interface changes require an ADR in `docs/adr/NNNN-title.md`.
- Each title keeps a living `docs/titles/<id>.md` (strategy, decomp status, hooks found, known issues).
- `qa-reviewer` reviews before merge. Determinism and compliance checks must pass.
