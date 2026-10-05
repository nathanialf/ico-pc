# Input (Phase 4C)

The game reads one PS2 pad. The host builds that pad from whatever is
plugged into the PC, in layers (`port/input/`):

| Layer | Where | Does |
|---|---|---|
| device | `input_sdl.c` (window build only) | SDL3 events: keyboard, mouse, any gamepad SDL recognises (hotplug); one raw snapshot per vsync |
| bindings | `bindings.c`, `input_config.c` | raw snapshot -> virtual pad, from the binding tables in `config.toml` |
| virtual pad | `vpad.c`, `pad_host.c` | sixteen buttons + two sticks as floats; merge, dead zone, stick fix, mirror, quantise |
| libpad | `pad_host.c` | the `scePad*` subset: a connected analog DualShock 2 and its 32-byte read buffer |
| game key config | `fumi/ios/pad.c` (`PadConf`, the key config screen) | untouched, runs on top of the buffer |

`ico_window_pump` (`port/platform/window_host.c`) hands SDL events to the
device layer and calls `ico_input_sdl_update` once per vsync; the game's pad
read (once per Main tick) takes whatever the virtual pad holds then.

## Sources and priority

1. The **pad script** (`pad_script.c`, `--pad-script` / `pad_script=`), when
   one is loaded: it is the only source. Headless runs and the trace
   replays stay deterministic and byte-exact; no stick fix or mirror is
   applied to it, and nothing rumbles.
2. Otherwise the **live** sources, merged: **gamepad** (all connected pads
   merged, max) and **keyboard + mouse**. Buttons are ORed; for each stick
   the vector with the larger magnitude wins. A keyboard works with a pad
   plugged in.
3. With neither (headless, no script) no controller is plugged in, as before
   (`scePadGetState` 0, the game runs as on a console with no pad).

The window build always has the keyboard, so the pad is always connected
there. The headless build links no SDL; `ico_input_set_live(1)` is what the
window build calls (from `ico_input_sdl_init`).

## Defaults

Gamepad (SDL's standard layout by position, so an Xbox, PlayStation or Switch
pad maps alike):

| PS2 | Gamepad | Keyboard | Mouse |
|---|---|---|---|
| Cross | south (A / Cross) | Space | left button |
| Circle | east | E | right button |
| Square | west | Q | |
| Triangle | north | R | |
| L1 | left shoulder | Tab, Backquote (`) | |
| R1 | right shoulder | F | middle button |
| L2 | left trigger (>= 50 %) | Z | |
| R2 | right trigger (>= 50 %) | X | |
| L3 / R3 | stick clicks | V / B | |
| Start | start | Enter | |
| Select | back | Backspace | |
| D-pad | d-pad | arrow keys | |
| Left stick | left stick | W A S D at full deflection; hold Left Shift to walk (0.5) | |
| Right stick | right stick | I J K L at full deflection | movement (see below) |

Fixed and not bindable: **Escape** closes the window (until Phase 6),
**Alt+Enter** toggles fullscreen (desktop resolution, the 4:3 picture
letterboxed by the presenter). The arrow keys are the D-pad because the menus
move on it; the right stick is IJKL.

Keyboard sticks: opposite keys cancel; two keys on different axes give a unit
vector (not the square's corner). The walk key scales the keyboard's left
stick only.

**Mouse.** While the window has focus and the game is in a stage (the boy
exists, not paused, not loading: `boyGObj`, `game_pause`, `data_loading`) the
mouse is captured (SDL relative mode, cursor hidden). Its motion pushes the
right stick, the game's limited camera pan: each vsync
`stick = stick * mouse_decay + motion_counts * 0.015 * mouse_sensitivity`,
clamped to the unit circle, so the stick returns to centre `mouse_decay`
per vsync after the mouse stops (default 0.80: under 1 % after 20 vsyncs).
Uncaptured (menus, focus lost) the motion is ignored and the stick decays.
The buttons work whether captured or not.

**Dead zone.** A radial dead zone with rescaling on gamepad sticks (default
0.12): inside it the stick is exactly centred, outside it runs 0 to 1 over
the rest of the range, so an idle stick is byte 128 on the wire, like a
DualShock. The game's own 48-unit dead zone (`iosPadNormalizeStick`) is
untouched.

**Quantising.** `byte = floor((v + 1) * 127.5 + 0.5)` clamped to 0..255:
0 -> 128, -1 -> 0, +1 -> 255, y positive downwards (0 is up on the PS2).

## config.toml

`<prefpath>/config.toml`; the preference folder is the executable's folder
for now (`ico_host_pref_dir`, `host_config.c`), where `ico-pc.ini` lives.
A small TOML subset (`ico_toml_*` in `host_config.c`): `[sections]`, `key =
value`, strings in `"..."` or `'...'`, `true`/`false`, numbers, one-line
arrays, `#` comments. `[input.kb]` with `cross = ..` is the same as `[input]`
with `kb.cross = ..` (a key's path is section + key). A missing file means
all defaults; a bad name is logged and skipped, the rest still applies.

```toml
[input]
keyboard = true          # use the keyboard / mouse / gamepad at all
mouse = true
gamepad = true
deadzone = 0.12          # gamepad radial dead zone, 0 to 0.9
walk_scale = 0.5         # left stick magnitude while a walk key is down
mouse_sensitivity = 1.0  # multiplier on 0.015 stick units per count
mouse_decay = 0.80       # 0 to 0.99, per vsync
mouse_invert_y = false
rumble = true

[input.kb]               # keyboard: target = "Key" or ["Key", "Key", ...]
walk = "LeftShift"
cross = "Space"
l1 = ["Tab", "Backquote"]
# lstick_up lstick_down lstick_left lstick_right, rstick_*, up down left right,
# cross circle square triangle l1 l2 l3 r1 r2 r3 start select

[input.mouse]            # mouse buttons: left right middle x1 x2
cross = "left"

[input.pad]              # gamepad sources, by position
cross = "south"
l2 = "lefttrigger"
# south east west north back start leftstick rightstick leftshoulder
# rightshoulder dpup dpdown dpleft dpright lefttrigger righttrigger
# leftx- leftx+ lefty- lefty+ rightx- rightx+ righty- righty+
# (a stick direction can be a button, and the d-pad can drive a stick)

[gameplay]
stick_fix = false
```

A target that appears in the file **replaces** that device's defaults for it
(up to four sources per target); `""` or `"none"` clears it. Key names are
physical positions (scancodes), case-insensitive, blanks and `_` ignored:
`A`-`Z`, `0`-`9`, `Space`, `Enter`, `Tab`, `Backspace`, `Backquote`, `Minus`,
`Equals`, `LeftBracket`, `RightBracket`, `Backslash`, `Semicolon`, `Quote`,
`Comma`, `Period`, `Slash`, `LeftShift`, `RightShift`, `LeftCtrl`,
`RightCtrl`, `LeftAlt`, `RightAlt`, `Up`, `Down`, `Left`, `Right`, `Insert`,
`Delete`, `Home`, `End`, `PageUp`, `PageDown`, `Keypad0`-`Keypad9`,
`KeypadEnter`, `KeypadPlus`, `KeypadMinus`, `F1`-`F4` (the list is
`port/input/keys.def`; short forms `LShift`, `Return`, `Grave` ... are
accepted). The default configuration is also available as text from
`ico_bindings_default_text()`.

## Stick fix

`iosPadNormalizeStick` (`fumi/ios/pad.c:415`) divides the stick's length by
`1 + 0.2 * d / 45`, d the angle in whole degrees to the nearest axis (0 to
45), so a diagonal push reads up to 17 % weaker than the same push along an
axis. **Off by default**: the original maths are the default.
`[gameplay] stick_fix = true` pre-scales each stick vector by the same
factor, with d continuous, before quantising, so the divisor cancels: the
length the game works with is the pad's own, within 0.5 %, and never smaller.
The result is capped so no component leaves the byte square (the direction is
kept), and the scale never shrinks a vector.

| d (degrees off an axis) | 0 | 10 | 22.5 | 30 | 40 | 45 |
|---|---|---|---|---|---|---|
| scale | 1.000 | 1.044 | 1.100 | 1.133 | 1.178 | 1.200 |

Applied to the live sources only (a script is exact bytes). The angle uses a
small arctangent without libm, accurate to 0.25 degrees (a scale error under
0.001).

## Mirror mode hook

`ico_input_set_mirror(int)` negates stick X on both sticks after the fix,
before quantising (centre stays 128). Phase 6's option calls it; there is
no config key yet. Live sources only.

## libpad

`pad_host.c` is the whole libpad subset the game links (`port/compat/libpad.h`
lists it). One DualShock 2 in port 0 slot 0, port 1 empty. It powers up
digital, and `pad.c`'s state machine walks it to analog exactly as the scripted
pad always did (the header of `pad_host.c` has the tick table), so the headless
traces are unchanged.

`scePadRead` fills 32 bytes: `[0]` 0 ok; `[1]` id 0x41 digital / 0x73 analog /
0x79 analog with pressure; `[2]`,`[3]` buttons active low (byte 2 the high byte
of the logical word, SCE order: CROSS 0x0040, START 0x0800, LEFT 0x8000);
`[4..7]` right x, right y, left x, left y (analog only; digital leaves 0x80);
`[8..19]` pressure, id 0x79 only: right, left, up, down, triangle, circle,
cross, square, L1, R1, L2, R2, 0 or 255. **The game never reads pressure**:
`IosPadBuf` ends at the sticks and `pad.c` does not use press mode beyond
negotiating it, so `scePadInfoPressMode` answers 0 and the pad is not walked
into it; `scePadEnterPressMode` works for a caller that asks. `pad.c` forces
the sticks to 127 when the id's high nibble is not 7 or 5 (not analog).

**Rumble.** `scePadSetActAlign` records which actuator each of the six data
bytes drives (pad.c: byte 0 -> actuator 0, byte 1 -> actuator 1, the rest
0xFF = none). `scePadSetActDirect` data: actuator 0 is the small motor, on
when its byte is non-zero; actuator 1 is the large motor, strength 0..255.
They go to SDL as `high_frequency` (small) = 0 or 65535 and `low_frequency`
(large) = byte * 257, to every connected gamepad (`SDL_RumbleGamepad`, 250 ms,
refreshed every 8 vsyncs while on, stopped on zero). `rumble = false` mutes it.
The shock driver (`fumi/ios/shockdriver.c`) calls `scePadSetActDirect` itself.

## Tests

`input_test` (CTest `input`; `port/input/test/input_test.c`): the TOML subset,
binding resolution from config text, key names, quantise, dead zone, the
stick-fix table, merge rules, the binding step (keyboard, walk, mouse
decay, gamepad, merged devices), the libpad buffer against hand-written
DualShock 2 frames (digital, analog, pressure), script priority, mirror, and
rumble mapping. CPU only. `pad_script_test` (port/input/test) still drives
the scripted pad through `pad.c`'s state machine.

## Open items for the Phase 6 remap UI

- The UI edits the binding tables (`IcoBindings`) and writes `config.toml`;
  there is no writer yet (only `ico_ini_store` for the ini). Capture mode
  ("press the key for Cross") needs a raw "last pressed source" query from
  `input_sdl.c`.
- Gamepad stick bindings are fixed (left/right stick -> left/right stick);
  only digital sources are remappable onto sticks.
- No per-gamepad profiles, no multi-controller split (the game has one
  pad), no analog-trigger pressure into the 0x79 bytes, no gyro or touchpad.
- The mouse capture predicate (`boyGObj`, `game_pause`, `data_loading` in
  `window_host.c`) was derived from the sources, not observed; check it
  against the title and pause screens.
- Mirror mode and stick fix have config/hook only; the UI toggles call
  `ico_input_set_mirror` / `ico_input_set_stick_fix`.
- The pref folder is the executable's folder; packaging (Phase 5) may move
  it to SDL's pref path in `ico_host_pref_dir`.
- Escape-to-quit is still hardcoded.
