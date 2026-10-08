# ADR 0010: UI stack for the launcher and overlays

**Status:** Proposed
Author: frontend-ui, 2026-10-08 (Phase 1 plan P1-19). Awaiting architect approval. Does not change a core contract.

## Context
The user chose a **minimal launcher UI** for Phase 1 (plan decision D6). P1-23 builds exactly these screens:
- **Library:** "Add game dump", then identify, then build progress, with clear rejection messages (exit criterion E11).
- **Local-party seating:** input devices to ports 0–3, CPU fill, launch.
- **Pause overlay:** resume, quit.

It must be controller-navigable, show title names as text, and use no Nintendo art. Phase 2 then adds the lobby, the public room browser, chat, the ruleset editor and settings, so the stack chosen now is the one those screens are built on.

Constraints from the existing decisions:
- **The shell owns the window and presents frames** (Phase 1 plan §3, P1-06). Phase 1 presents `cpu_rgba8` frames by uploading them to an SDL texture, aspect-correct and letterboxed (ADR 0002 rule 8). GX renders offscreen and reads back to `cpu_rgba8` whichever backend spike S1 picks (ADR 0007 §4, §6). A GPU texture hand-off needs its own ADR, written only if E7 fails. Today the shell uses `SDL_CreateWindowAndRenderer` (`core/shell/app.cpp`).
- **`InputFrame` is the determinism boundary** (Phase 1 plan §3). Everything left of it, including the UI, is host-side and may use wall-clock time. Nothing on the UI side may reach game state except through the `InputFrame` sequence, the seed and the initial save media (ADR 0002 rule 2, as amended by ADR 0004 rule 3).
- **`Session` is pure** and is stepped by the shell (ADR 0005). The UI reads phase and standings only through `Session::director()`, never from a runtime (ADR 0005 Consequences).
- **Presentation options must not affect `stateHash()`** (ADR 0002 rule 2, exit criterion E6).
- **Build:** C++20, CMake + vcpkg at baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d`, SDL3 3.4.12 built with `default-features: false`. Root build files are architect-owned.
- **Branding:** no Nintendo logos, box art, fonts or characters in the launcher (PLAN §2.4). Every shipped asset must be original or under a permissive licence, and must pass the compliance scanner.

Port facts checked against the pinned vcpkg baseline:

| Port | Version | Licence | Notes |
|---|---|---|---|
| `rmlui` | 6.2 | MIT | Deps `itlib`, `robin-hood-hashing` (both MIT). Default feature `freetype` uses FreeType with its default features off. Optional `svg` (lunasvg), `lua`, `lottie`. **The port does not install RmlUi's `Backends/` sources**, so the render and system interfaces are ours to write. |
| `imgui` | 1.92.8 | MIT | Features include `sdl3-binding`, `sdl3-renderer-binding`, `sdlgpu3-binding`, `freetype`, `test-engine`. |
| `sdl3-ttf` | 3.2.2 | Zlib | FreeType (default features off). Optional HarfBuzz. |
| `freetype` | 2.14.3 | FTL OR GPL-2.0-or-later | We would use it under the FTL, which requires a credit in the docs. |
| `nuklear` | 4.13.3 | Unlicense OR MIT | |
| `lvgl` | — | — | Not in the baseline. |

Upstream RmlUi facts: spatial navigation (`nav-up`/`nav-down`/`nav-left`/`nav-right`, `:focus-visible`) since 6.0; SDL 3 backends since 6.1; an SDL_GPU backend since 6.2; IME in the SDL backends since 6.3 (not yet in our baseline). SDL 3.4 adds `SDL_CreateGPURenderer`, which runs the 2D `SDL_Renderer` API on an `SDL_GPUDevice`.

## Decision

### 1. Stack
- **Player-facing UI (every screen in `ui/`, now and in Phase 2): RmlUi**, from vcpkg (`rmlui` with its default `freetype` feature only, no Lua, SVG or Lottie in Phase 1).
- **Developer tools: Dear ImGui**, later and only when a tool needs it (for example a Director event log, a hash-trail viewer, frame-time graphs). It is never used for a player-facing screen, never enabled in a non-developer build, and it is not added to `vcpkg.json` until the first tool exists. Phase 1 needs none: `--stats` (P1-06) covers E7.
- No other UI toolkit is added. A new one needs an amendment to this ADR.

### 2. Rendering through the shell's presenter
1. **The shell owns the window, the renderer and present.** `ui/` never creates a window, a renderer or a GPU device, and never calls `SDL_RenderPresent`.
2. **One overlay pass per presented frame.** The presenter draws the game frame (letterboxed), then calls the UI's render step on the same render target, then presents. With no game running, the presenter draws only the UI. The seam is a small shell-side interface (not a core contract), roughly:
   ```cpp
   // core/shell: implemented by ui/, called by the presenter.
   class IOverlay {
    public:
     virtual ~IOverlay() = default;
     virtual bool handleEvent(const SDL_Event&) = 0;   // true = consumed by the UI
     virtual void update(std::uint64_t host_time_ns) = 0;
     virtual void render(SDL_Renderer*, int width_px, int height_px) = 0;
   };
   ```
   The exact shape is settled in P1-06/P1-23 with architect. `ui/` depends on SDL3 for this seam only; its screen logic does not (§4).
3. **Phase 1 backend: our own RmlUi render interface on `SDL_Renderer`.** Geometry goes through `SDL_RenderGeometryRaw`, clipping through `SDL_SetRenderClipRect`, textures through `SDL_CreateTexture`. RmlUi's own SDL backend (MIT) is the reference. Code adapted from it keeps its licence notice.
4. **Features limited to what the renderer can draw.** The interface implements geometry, textures and scissor clipping. It does not implement clip masks, layers, filters or shaders (all optional in RmlUi 6). So RCSS in `ui/` does not use `transform` on clipping containers, `filter`, `backdrop-filter`, `mask-image` or shader-based gradients. Gradients use vertex-colour decorators. P1-23 checks each RCSS feature it uses against this interface.
5. **Backend changes stay inside `ui/backend/`.** If the presenter moves to SDL_GPU (after S1 or a later GPU hand-off ADR), the first choice is `SDL_CreateGPURenderer`, which keeps this interface unchanged on the shared device. The second is a render interface on SDL_GPU, using RmlUi's 6.2 backend as the reference. Neither choice touches screens or view models.
6. **The UI never reads back or modifies `FrameOutput`.** It draws over the presented frame and nothing else. Phase 1 has no in-game HUD over a running game: the pause overlay is shown only while the session is paused.

### 3. Determinism and isolation
1. **No path into game state.** `ui/` does not include `runtimes/` or `adapters/` headers and does not call `ITitleRuntime` or `ITitleAdapter`. It uses only the public APIs of `core/` (`Session` start/stop requests through the shell, `Session::director()` read-only, `TitleId`, `Result`), `asset-import/` and `input/`. CMake enforces this: the `ui` target links only those public targets.
2. **The UI never writes an `InputFrame`.** While any UI screen has input focus, the shell does not step the `Session`, so no frame exists that UI input could change. While the game runs with no screen open, the UI listens for one action only: the **host pause action**. It is a host-reserved binding owned by `input/` (proposed defaults: Esc on keyboard, the Guide button on a gamepad, never a button a title uses). The shell consumes it before `input::Mapper` samples the frame, so it never appears in an `InputFrame`.
3. **Resume does not leak menu input.** Buttons held when the overlay closes (for example the "Resume" confirm press) are masked in the pads until released. This is host-side, left of the boundary, and whatever the pads report is what the replay records, so determinism is unaffected. `input/` implements the masking (request below).
4. **Host time and randomness are allowed in `ui/`, and they stay there.** Animations, transitions, key-repeat and progress spinners use `SDL_GetTicksNS`. UI state is never part of `SaveState`, a replay, `stateHash()` or `SessionConfig`. The UI does not choose the session seed: it asks the shell to start a session, and the shell sets and records the seed (ADR 0006).
5. **Headless runs have no UI.** `--headless`, `--replay` and `--verify-replay` never create the UI. A shell test checks that a replay's hash trail is identical with the UI enabled and disabled, including a pause and resume mid-run (the E6 principle applied to the UI).
6. **Presentation settings are local.** Any later settings screen that changes `internal_scale` or widescreen only changes values that ADR 0002 rule 2 already keeps out of `stateHash()`.

### 4. Structure of `ui/`
| Path | Contents | Depends on |
|---|---|---|
| `ui/model/` | View models and flow state machines: the import flow (idle, choosing, identifying, building with progress, done, rejected with reason), seating (claim a device, assign a port, CPU fill, launch readiness), pause (resume, quit), and navigation and back-stack. Plain C++20. | `core/`, `asset-import/`, `input/` public APIs. **No RmlUi, no SDL.** |
| `ui/screens/` | RML documents and RCSS stylesheets, plus the RmlUi data models bound to `ui/model/`. | RmlUi |
| `ui/backend/` | RmlUi render, system and file interfaces for the presenter, and the SDL event → RmlUi input translation. | RmlUi, SDL3 |
| `ui/assets/` | One open-licence font (§5) and theme tokens. | — |

- P1-23's "headless UI-logic tests where the stack allows" are doctest tests on `ui/model/`, which needs no renderer or font. A small number of document tests may load RML with a no-op render interface and the bundled font to check bindings.
- **Long-running work stays off the UI thread.** asset-import's identify and build steps run on a worker thread. Its progress callback (P1-10) posts to a queue that the model drains on the main thread.

### 5. Navigation, input and theme
- **Controller navigation:** RmlUi spatial navigation (`nav-*` properties, `:focus-visible`). `ui/backend/` turns gamepad D-pad and left-stick events (with a deadzone and host-timed repeat) into navigation keys. South confirms and East goes back, by **position**. Prompts show positional glyphs that we draw ourselves, never console-brand button art. Mouse and keyboard work everywhere too: every focusable element is also clickable, and Tab, the arrow keys, Enter and Esc navigate.
- **Every screen opens with a focused element**, so a controller user is never stuck. A screen with no target for the back action does nothing on back, rather than exiting the app.
- **Choosing a dump:** mouse users get `SDL_ShowOpenFileDialog` and drag-and-drop onto the window (`SDL_EVENT_DROP_FILE`). Controller users get a simple in-UI file list (drives, folders, `.iso` files), because native dialogs are not controller-navigable.
- **Seating** uses `input/`'s device list and hot-plug events: "press any button to join" claims a device and assigns the next free port.
- **Theme:** a console-style "10-foot" layout (large type, a strong focus ring, full-screen panels, transitions). It scales with the window through RmlUi's density-independent pixel ratio. Colours and sizes are RCSS tokens in one stylesheet. The visual identity is original: no Nintendo logos, box art, fonts or characters. Titles are shown as text from the catalog.
- **Font:** one font under the SIL Open Font License 1.1, with good Latin coverage and a look that is clearly not a Nintendo typeface. It is committed under `ui/assets/fonts/` with its `OFL.txt`, and qa-reviewer allowlists that exact file in the compliance scanner. The FreeType FTL credit goes in the licence notices. No image assets in Phase 1.
- **Text input** (room codes, chat, ruleset names) is Phase 2. It needs an on-screen keyboard for controllers and IME through the SDL backend (RmlUi 6.3, after a vcpkg baseline bump). This links to the `ibus` decision noted in `vcpkg.json`.

### 6. Requests to other owners
| Owner | Request | When |
|---|---|---|
| architect | Add `rmlui` (default features) to `vcpkg.json`, and add `add_subdirectory(ui)` to the root `CMakeLists.txt`. | Before P1-23 starts |
| architect (P1-06) | The presenter's overlay pass and the `IOverlay` seam (§2.2). Routing of SDL events to the UI while a screen has focus, and to `input/` otherwise. Not stepping the session while a screen has focus. The UI-on/UI-off replay test (§3.5). | P1-06 |
| input-mapping (P1-07) | A host-reserved pause action that is excluded from title profiles. Masking of buttons held at resume until they are released. A public device list with hot-plug events and stable device identities for seating. | P1-07 |
| asset-import (P1-10) | The progress callback may run on a worker thread. Identify and build reports carry a machine-readable reason (unknown, truncated, wrong title, known but unsupported revision or region, compressed container), so the UI can show E11's distinct messages. | P1-10, P1-11 |
| qa-reviewer | Allowlist the single OFL font file. Add an include-boundary check that `ui/` does not reference `runtimes/` or `adapters/`. | P1-23 |

## Alternatives
| Option | Licence / vcpkg | Gamepad navigation | Through the presenter | Console-like theming | Verdict |
|---|---|---|---|---|---|
| **RmlUi** (chosen) | MIT, in the baseline (6.2) | Spatial navigation built in (6.0+). We map the pad to navigation keys. | Our own render interface on `SDL_Renderer` (or SDL_GPU). It never owns the window. | Strong: RCSS, decorators, transitions, animations, data binding | **Chosen** for every player-facing screen |
| **Dear ImGui** for everything | MIT, in the baseline (1.92.8, with SDL3, SDL renderer and SDL_GPU bindings) | Built-in gamepad navigation, designed around tool windows | Official `SDL_Renderer` and SDL_GPU backends | Weak: a style struct plus hand-drawn widgets. Every console-like screen becomes custom draw code. | Rejected for player UI. Fastest for D6's three screens, but the Phase 2 lobby, browser and editor would have to be rewritten. **Kept for developer tools.** |
| **ImGui now, RmlUi in Phase 2** | as above | — | — | — | Rejected: P1-23 would be throwaway work, and the presenter seam would be built twice. |
| **Custom UI drawn with `SDL_Renderer` + SDL3_ttf** | Zlib, in the baseline | All ours | Native | Full control | Rejected: layout, scrolling, focus, text editing and data binding all become ours, and the Phase 2 ruleset editor alone has dozens of fields. |
| **Nuklear** | Unlicense/MIT, in the baseline | Weak, keyboard-oriented | Easy (vertex output) | Weak, immediate-mode skinning | Rejected: same problems as ImGui, with a smaller community. |
| **LVGL** | MIT, **not in the baseline** | Good (input-device groups) | Software buffers or draw units, aimed at embedded | Good | Rejected: not in vcpkg, and an embedded-first rendering model. |
| **Qt / QML** | LGPL-3.0 or commercial. In vcpkg, but very large. | Qt Gamepad is deprecated. We would need our own. | Owns its own windowing, event loop and scene graph. Composing it with the shell's presenter is awkward. | Strong | Rejected: build weight, LGPL obligations, and it takes over the main loop and rendering. |
| **Web views (CEF, Ultralight)** | CEF BSD but huge. Ultralight proprietary. | Our own | Off-screen rendering to a texture, at significant cost | Strong | Rejected: size, extra processes and threads, and licence (Ultralight). |
| **NoesisGUI** (XAML) | Proprietary | Good | Good | Strong | Rejected: closed-source licence. |
| **Slint** | GPL-3.0, royalty-free or commercial. Needs a Rust toolchain. | Partial | Possible | Good | Rejected: licence terms and the Rust toolchain. |

All options can be kept out of deterministic state equally well. §3 is architecture, not a property of the toolkit. Qt and web views make it harder because they bring their own threads and event loops.

## Consequences
- New dependencies: `rmlui` 6.2, `itlib`, `robin-hood-hashing` and FreeType (default features off). All are MIT or FTL. The FTL credit goes in the licence notices.
- `ui/` writes and maintains its own RmlUi render and system interfaces (a few hundred lines), because the vcpkg port does not install the upstream backends.
- RCSS in `ui/` is limited to features the `SDL_Renderer` interface supports (§2.4) until the presenter moves to the GPU.
- The pause overlay depends on P1-06 (overlay pass, event routing, not stepping while paused) and P1-07 (pause action, held-button masking).
- One binary font file enters the repo, allowlisted by exact path. It is our own choice under the OFL, not Nintendo material.
- Phase 2 screens (lobby, room browser, chat, ruleset editor, settings) are built on the same stack. Text input needs the RmlUi 6.3 IME work and a decision about Linux IME (`ibus`).
- Prior art: Zelda64Recomp, from the N64Recomp/RT64 ecosystem planned for Phase 3, builds its controller-navigable menus with RmlUi. Ship of Harkinian's libultraship uses Dear ImGui for its in-game menus, which is the tool-style look this ADR avoids for player screens.

**Not done:** no prototype was built for this ADR. P1-23 starts with a time-boxed bring-up (at most 2 days): the RmlUi render interface on the shell's `SDL_Renderer`, one RML screen with spatial navigation driven by a gamepad, and the UI-on/UI-off replay check against `FakeRuntime`. If that bring-up fails, frontend-ui amends this ADR before building the screens. The fallback is Dear ImGui for Phase 1 only, with RmlUi deferred to Phase 2.

## Review
_Pending architect review (P1-19 acceptance: "Accepted ADR")._
