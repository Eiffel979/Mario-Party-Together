# ui/

Owner: `frontend-ui`. The player-facing launcher, built on RmlUi and drawn through the
shell's presenter (ADR 0010). Phase 1 (P1-23, D6) has exactly these screens:

| Screen | What it does |
|---|---|
| Library | Titles the user has added, shown as text. "Add game dump". Exit. |
| Add game dump | The in-UI file list (controllers) or the native file dialog (mouse). Drag-and-drop onto the window also works. |
| File list | Drives, folders and dump files (`.iso`, `.gcm`, N64 images, and the compressed containers so their specific message is shown). |
| Import | Identify, then build progress, then "ready" or a specific rejection (E11). Cancel stops the build. |
| Players | Devices to Player 1-4 with hot-plug. Select a device to change its seat; a new controller joins with any button. Empty seats are computer players, chosen in the game's own setup. |
| Pause | Opened by the host pause action (Esc, or the gamepad Guide button). Resume, Quit to library, Exit. |

## Layout

| Path | Contents | Depends on |
|---|---|---|
| `model/` | View models and flow state machines: `ImportFlow`, `Library`, `FileBrowser`, `Seating`, `LauncherFlow`, `NavRepeat`. | `core/`, `asset-import/`, `input/` (pure core). **No RmlUi, no SDL.** |
| `backend/` | RmlUi render interface on `SDL_Renderer`, host-time system interface, UTF-8 file interface, SDL key mapping. | RmlUi, SDL3 |
| `screens/` | `launcher.cpp` (the `IOverlay`) and one RML document per screen. | RmlUi, SDL3 |
| `assets/` | `theme.rcss` (every colour and size) and the one font, `fonts/LatoLatin-Regular.ttf`, with `fonts/OFL.txt`. | - |
| `include/mpt/ui/launcher.h` | `make_launcher(UiHost)`, which `core/shell/main.cpp` hands to `run()`. | |

Screens and assets are copied next to the executable (`<exe dir>/ui/`). Without them the
launcher falls back to this source folder (developer builds).

## Determinism and isolation (ADR 0010 §3)

- `ui/` never includes `runtimes/` or `adapters/` headers (`ui_include_boundary` test).
  It reaches the session only through `shell::IShellCommands` and `shell::SessionStatus`
  (`core/shell/ui_host.h`), and builds dumps only through asset-import. Every
  `VerifiedDump` comes from `asset_import::make_verified`.
- While any screen is up, `hasFocus()` is true and the shell does not step the session.
  While the game runs, the UI is offered only the host pause action.
- Host time is used for animation, key repeat and the resume hold cap. UI state is never
  part of `SaveState`, a replay, `stateHash()` or the seed; the shell picks the seed.
- `--headless`, `--replay` and `--verify-replay` never create the UI.
- **Resume masking.** "Resume" asks the shell to resume at once. The shell's
  `GameInputGate` (`core/shell/event_routing.h`) masks whatever is held when game input
  goes live again (ADR 0010 §3.3), so the confirm press never reaches the game.
- The pause action is input/'s reserved binding (`SdlInput::host_action`: Esc, Guide).
- `ui_overlay_replay` (the ADR 0010 bring-up gate) plays a FakeRuntime replay with the real
  launcher, routed with the shell's own `route_event` and `GameInputGate`: paused with Esc
  or Guide, navigated with synthetic D-pad, stick and buttons, resumed with south or east.
  The trail equals the trail with no UI.

## RCSS rules

The render interface implements geometry, generated textures and scissor clipping only.
Do not use `transform`, `filter`, `backdrop-filter`, `mask-image`, `box-shadow` or shader
gradients (`linear-gradient` and friends). `decorator: horizontal-gradient(...)` and
`vertical-gradient(...)` are vertex-coloured and fine. No image files in Phase 1.

## Library list

`<user data>/library.json` holds, per added title, the title ID, region, revision, the
dump's SHA-256 and the path to the user's file. It is the user's own local data: paths
and hashes, never game bytes. After a restart, "Play" re-checks the file through
asset-import (identify, then a build-cache hit) to get a fresh `VerifiedDump`.

## Not in Phase 1

Lobby, room browser, chat, ruleset editor, settings and rebinding, text input, replay
export. When replay export arrives it must warn before exporting a replay that embeds
save media (ADR 0006).

## Licence notices

- **RmlUi** 6.2, MIT License. `backend/render_sdl.*` is adapted from RmlUi's
  `Backends/RmlUi_Renderer_SDL.cpp`:

  > Copyright (c) 2008-2014 CodePoint Ltd, Shift Technology Ltd, and contributors
  > Copyright (c) 2019-2026 The RmlUi Team, and contributors
  >
  > Permission is hereby granted, free of charge, to any person obtaining a copy of this
  > software and associated documentation files (the "Software"), to deal in the Software
  > without restriction, including without limitation the rights to use, copy, modify,
  > merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
  > permit persons to whom the Software is furnished to do so, subject to the following
  > conditions:
  >
  > The above copyright notice and this permission notice shall be included in all copies
  > or substantial portions of the Software.
  >
  > THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
  > INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
  > PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  > HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
  > CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
  > THE USE OR OTHER DEALINGS IN THE SOFTWARE.

- **FreeType** (used by RmlUi), under the FreeType License (FTL): portions of this
  software are copyright © The FreeType Project (www.freetype.org). All rights reserved.
- **Lato** font (`assets/fonts/LatoLatin-Regular.ttf`), SIL Open Font License 1.1, see
  `assets/fonts/OFL.txt`.
- RmlUi's dependencies `itlib` and `robin-hood-hashing` are MIT licensed.
