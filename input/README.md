# input/

Owner: `input-mapping`. This directory turns keyboards and SDL3 gamepads into the
`InputFrame` that runtimes, replays and netplay consume (ADR 0002 `input.h`).

```
SDL events ─► SdlInput (sdl_input.h)        host side, nondeterministic
                 │ DeviceSnapshot per device (plain data)
                 ▼
              PortAssigner (ports.h)         which device drives which port
              Mapper / map_frame (mapper.h)  pure: no SDL, clock, RNG or threads
                 │
                 ▼
              InputFrame                     the only output
```

| Target | Contents | Links SDL |
|---|---|---|
| `mpt::input` | names, profiles, seating, mapping | no |
| `mpt::input_sdl` | `SdlInput`: keyboard + gamepad hot-plug, per-frame sampling | yes |

The shell calls `SdlInput::handle_event` for every polled event and
`SdlInput::sample(frame)` exactly once per emulated frame.

## Conventions
- **Sticks:** `PadState` stick values are -32767..32767 with **+X right, +Y up**
  (N64/GameCube convention). SDL reports +Y down; profiles bind `axis:left_y-` to
  `stick_up` to flip it.
- **Triggers:** 0..255. A key or button bound to `trigger_l`/`trigger_r` gives 255.
- **Buttons:** an analog source bound to a button control presses it past half travel.
- **Seating:** a seated port reports `connected = 1` even if its controller is
  unplugged (the player idles rather than vanishing). Unseated ports are all zeros.
- **Several devices on one port** merge: buttons OR, and per stick axis the larger
  magnitude wins. Devices only share a port when the UI asks for it
  (`PortAssigner::assign`); hot-plug never merges.

## Seating and hot-plug
- **Lobby (auto-assign on, the default):** each new device, keyboard or gamepad,
  takes the lowest port with no device, so the keyboard and each controller get
  separate seats. A device that was unplugged returns to its last port if that
  port is still free. Once all 4 ports have a device, more devices stay unassigned
  until the UI seats them.
- **In a session (auto-assign off):** the shell calls
  `ports().set_auto_assign(false)` when a `Session` starts and turns it back on
  when the session ends. While it is off, hot-plug never seats a new port, so a pad
  plugged in mid-party cannot add a player. The only automatic step left is that a
  party member's replugged controller returns to its own seat, if that seat is
  still seated and has no device. Anything else goes through the UI.

## Profile format (`mpt.input_profile`, version 1)
Profiles are JSON. Per-title profiles live in `input/profiles/<title>/`. Without
one, the built-in GameCube-style default (`builtin_default_profile()`) is used.

```json
{
  "format": "mpt.input_profile",
  "version": 1,
  "name": "Mario Party 4 (default)",
  "title": "mp4",
  "options": {
    "stick_deadzone_percent": 10,
    "trigger_deadzone_percent": 5,
    "stick_half_percent": 50,
    "trigger_button_threshold": 230,
    "normalize_digital_diagonals": true
  },
  "bindings": {
    "a": ["key:k", "button:south"],
    "stick_up": ["key:w", "axis:left_y-"],
    "trigger_l": ["key:q", "axis:left_trigger"],
    "stick_half": ["key:lshift"]
  },
  "minigames": {
    "mp4.minigame.example_name": {
      "note": "why this minigame needs its own mapping",
      "options": { "stick_half_percent": 35 },
      "bindings": { "a": ["key:k", "key:space", "button:south", "button:east"] }
    }
  }
}
```

- `format`, `version`, `name` and `bindings` are required; `title`, `options` and
  `minigames` are optional. Unknown keys, names or out-of-range values are errors.
- **Base bindings:** a control that is not listed is unbound.
- **Minigame overrides:** keys are content IDs of kind `minigame` for the profile's
  title. A listed control replaces the base list (`[]` unbinds it); unlisted controls
  and options are inherited. The shell selects the override from the Director's
  current minigame (`Mapper::set_minigame`).

### Controls
`a b x y l r z zl start select home dpad_up dpad_down dpad_left dpad_right
c_up c_down c_left c_right` (one per canonical `mpt::Button`; `c_*` are the N64
C buttons), `stick_up stick_down stick_left stick_right`, `c_stick_up c_stick_down
c_stick_left c_stick_right` (GameCube C-stick / right stick), `trigger_l trigger_r`
(analog) and `stick_half` (modifier: while held, digital stick sources give
`stick_half_percent` deflection).

### Sources
- `key:<name>`: a key by position (USB HID usage). Names: `a`–`z`, `0`–`9`, `f1`–`f12`,
  `space enter escape backspace tab minus equals left_bracket right_bracket backslash
  semicolon apostrophe grave comma period slash caps_lock print_screen scroll_lock
  pause insert home page_up delete end page_down up down left right num_lock`,
  `kp_0`–`kp_9 kp_divide kp_multiply kp_minus kp_plus kp_enter kp_period`,
  `lctrl lshift lalt lgui rctrl rshift ralt rgui`. A key without a name is written
  `key:#<code>` with its decimal HID usage (0–511), for example `key:#100`.
- `button:<name>`: a gamepad button by position: `south east west north back guide
  start left_stick right_stick left_shoulder right_shoulder dpad_up dpad_down
  dpad_left dpad_right misc1 left_paddle1 right_paddle1 left_paddle2 right_paddle2
  touchpad`.
- `axis:<name>+` / `axis:<name>-`: one half of a stick axis (`left_x left_y right_x
  right_y`, SDL orientation, +Y down). `axis:left_trigger` / `axis:right_trigger`.

### Options
| Option | Range | Default | Meaning |
|---|---|---|---|
| `stick_deadzone_percent` | 0–90 | 10 | Radial deadzone on gamepad sticks, rescaled so full tilt stays full |
| `trigger_deadzone_percent` | 0–90 | 5 | Deadzone on gamepad triggers |
| `stick_half_percent` | 1–100 | 50 | Deflection while `stick_half` is held |
| `trigger_button_threshold` | 0–255 | 0 | Analog trigger at or above this also presses L/R (GameCube click). 0 = off |
| `normalize_digital_diagonals` | bool | true | Two direction keys give a 45° vector of radius 1, not a square corner |

All option values are integers, and the mapping core uses integer math only, so a
profile maps identically on every platform.
