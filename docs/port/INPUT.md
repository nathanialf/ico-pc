# Input

The game reads one PS2 pad. The host builds that pad from whatever is
plugged into the PC, in layers (`port/input/`):

| layer | where | does |
|---|---|---|
| device | `input_sdl.c` (window build only) | SDL3 events: keyboard, mouse, any gamepad SDL recognises (with hotplug); one raw snapshot per vsync |
| bindings | `bindings.c`, `input_config.c` | raw snapshot to virtual pad, from the binding tables in `config.toml` |
| virtual pad | `vpad.c`, `pad_host.c` | sixteen buttons and two sticks as floats: merge, dead zone, stick fix, mirror, quantise |
| libpad | `pad_host.c` | the `scePad*` subset: a connected analog DualShock 2 and its 32-byte read buffer |
| game key config | `fumi/ios/pad.c` (`PadConf`, the key config screen) | the game's own code, unchanged, on top of the buffer |
| pad script and recording | `pad_script.c`, `input_record.c` | a scripted pad for headless runs, and the recording of what the game read |

`ico_window_pump` (`port/platform/window_host.c`) hands SDL events to the
device layer and calls `ico_input_sdl_update` once per vsync; the game's pad
read (once per Main tick) takes whatever the virtual pad holds at that
moment.

## Sources and priority

1. The **pad script** (`--pad-script` or `pad_script=`), when one is
   loaded, is the only source. Headless runs and replays stay deterministic
   and byte-exact: no stick fix or mirror is applied to it, and nothing
   rumbles.
2. Otherwise the **live** sources are merged: **gamepad** (all connected
   pads, maximum) and **keyboard and mouse**. Buttons are ORed; for each
   stick the vector with the larger magnitude wins. A keyboard works with a
   pad plugged in.
3. With neither (headless with no script) no controller is plugged in
   (`scePadGetState` 0), and the game runs as on a console with no pad.

The window build always has the keyboard, so its pad is always connected;
`ico_input_sdl_init` calls `ico_input_set_live(1)`. The headless build
links no SDL.

## Defaults

Gamepad buttons follow SDL's standard layout by position, so an Xbox,
PlayStation or Switch pad maps alike.

| PS2 | gamepad | keyboard | mouse |
|---|---|---|---|
| Cross | south (A / Cross) | Space | left button |
| Circle | east | E | right button |
| Square | west | Q | |
| Triangle | north | R | |
| L1 | left shoulder | Tab, Backquote (`) | |
| R1 | right shoulder | F | middle button |
| L2 | left trigger (at least 50 %) | Z | |
| R2 | right trigger (at least 50 %) | X | |
| L3 / R3 | stick clicks | V / B | |
| Start | start | Enter | |
| Select | back | Backspace | |
| D-pad | d-pad | arrow keys | |
| Left stick | left stick | W A S D at full deflection; hold Left Shift to walk (0.5) | |
| Right stick | right stick | I J K L at full deflection | movement (below) |

Fixed and not bindable: **Escape** closes the game, **Alt+Enter** toggles
fullscreen (desktop resolution, the picture letterboxed by the presenter),
and the developer hotkeys F11 and F12 (docs/port/TESTING.md). The arrow
keys are the D-pad because the game's menus move on it, which leaves IJKL
for the right stick.

On the keyboard sticks, opposite keys cancel, and two keys on different
axes give a unit vector rather than the square's corner. The walk key
scales the keyboard's left stick only.

**Mouse.** While the window has focus and the game is in a stage (the boy
exists, the game is not paused and not loading: `boyGObj`, `game_pause`,
`data_loading`, checked in `window_host.c`) the mouse is captured (SDL
relative mode, cursor hidden). Its motion pushes the right stick, which is
the game's limited camera pan. Each vsync
`stick = stick * mouse_decay + motion_counts * 0.015 * mouse_sensitivity`,
clamped to the unit circle, so the stick returns to centre by a factor of
`mouse_decay` per vsync after the mouse stops (default 0.80: under 1 %
after 20 vsyncs). The mouse feeds a stick, rather than turning the camera
directly, because the game's camera only takes stick input. While not
captured (menus, focus lost) motion is ignored and the stick decays. The
buttons work either way.

**Dead zone.** Gamepad sticks have a radial dead zone with rescaling
(default 0.12): inside it the stick is exactly centred, outside it runs
from 0 to 1 over the rest of the range, so an idle stick is byte 128 on
the wire, like a DualShock's. The game's own 48-unit dead zone
(`iosPadNormalizeStick`) is untouched.

**Quantising.** `byte = floor((v + 1) * 127.5 + 0.5)`, clamped to 0..255:
0 gives 128, -1 gives 0, +1 gives 255, with y positive downwards (0 is up
on the PS2).

## config.toml

The bindings live in `config.toml` (docs/port/CONFIG.md says where). The
reader is a small TOML subset (`ico_toml_*` in
`port/platform/host_config.c`): `[sections]`, `key = value`, strings in
`"..."` or `'...'`, `true`/`false`, numbers, one-line arrays, `#`
comments. `[input.kb]` with `cross = ...` is the same as `[input]` with
`kb.cross = ...` (a key's path is section plus key). A missing file means
all defaults; a bad name is logged and skipped and the rest still applies.

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

A target that appears in the file **replaces** that device's defaults for
it (up to four sources per target); `""` or `"none"` clears it. A
comma-separated string (`"Tab, Backquote"`) is read like an array, which
is the form the Settings menu writes. Key names are physical positions
(scancodes), case-insensitive, with blanks and `_` ignored: `A`-`Z`,
`0`-`9`, `Space`, `Enter`, `Tab`, `Backspace`, `Backquote`, `Minus`,
`Equals`, `LeftBracket`, `RightBracket`, `Backslash`, `Semicolon`, `Quote`,
`Comma`, `Period`, `Slash`, `LeftShift`, `RightShift`, `LeftCtrl`,
`RightCtrl`, `LeftAlt`, `RightAlt`, `Up`, `Down`, `Left`, `Right`,
`Insert`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`,
`Keypad0`-`Keypad9`, `KeypadEnter`, `KeypadPlus`, `KeypadMinus`, `F1`-`F4`.
The list is `port/input/keys.def`; short forms such as `LShift`, `Return`
and `Grave` are accepted. `ico_bindings_default_text()` returns the default
configuration as text.

Gamepad sticks always drive the sticks; binding a stick direction adds a
digital source that does the same. Only digital sources can be remapped
onto the sticks.

## Stick fix

`iosPadNormalizeStick` (`fumi/ios/pad.c`) divides the stick's length by
`1 + 0.2 * d / 45`, where d is the angle in whole degrees to the nearest
axis (0 to 45), so a diagonal push reads up to 17 % weaker than the same
push along an axis. The fix is **off by default**, because the original
maths are the reference.

`[gameplay] stick_fix = true` pre-scales each stick vector by the same
factor, with d continuous, before quantising, so the game's divisor
cancels: the length the game works with is the pad's own, within 0.5 %,
and never smaller. The result is capped so no component leaves the byte
square (keeping the direction), and the scale never shrinks a vector.

| d (degrees off an axis) | 0 | 10 | 22.5 | 30 | 40 | 45 |
|---|---|---|---|---|---|---|
| scale | 1.000 | 1.044 | 1.100 | 1.133 | 1.178 | 1.200 |

It applies to the live sources only, since a script is exact bytes. The
angle comes from a small arctangent that needs no libm, accurate to 0.25
degrees (a scale error under 0.001).

## Mirror mode

`ico_input_set_mirror(int)` (forwarding to `ico_opt_set_mirror`,
docs/port/OPTIONS.md) negates stick X on both sticks after the stick fix
and before quantising, so the centre stays 128. It applies to the live
sources only.

## libpad

`pad_host.c` is the whole libpad subset the game links
(`port/compat/libpad.h` lists it): one DualShock 2 in port 0 slot 0, port 1
empty. It powers up digital, and `pad.c`'s state machine walks it to analog
on fixed ticks (the header of `pad_host.c` has the tick table), so the
scripted pad reaches the same state on the same tick in every run.

`scePadRead` fills 32 bytes:

- `[0]` 0 (ok);
- `[1]` the id: 0x41 digital, 0x73 analog, 0x79 analog with pressure;
- `[2]`, `[3]` the buttons, active low; byte 2 is the high byte of the
  logical word, in SCE order (CROSS 0x0040, START 0x0800, LEFT 0x8000);
- `[4..7]` right x, right y, left x, left y (analog only; digital leaves
  0x80);
- `[8..19]` pressure, for id 0x79 only: right, left, up, down, triangle,
  circle, cross, square, L1, R1, L2, R2, each 0 or 255.

The game never reads pressure: `IosPadBuf` ends at the sticks and `pad.c`
does not use press mode beyond negotiating it, so `scePadInfoPressMode`
answers 0 and the pad is not walked into it. `scePadEnterPressMode` still
works for a caller that asks. `pad.c` forces the sticks to 127 when the
id's high nibble is not 7 or 5 (not analog).

**Rumble.** `scePadSetActAlign` records which actuator each of the six
data bytes drives (`pad.c` sets byte 0 to actuator 0, byte 1 to actuator 1,
the rest 0xFF, none). In `scePadSetActDirect`'s data, actuator 0 is the
small motor, on when its byte is non-zero, and actuator 1 is the large
motor, strength 0 to 255. They go to SDL as `high_frequency` (small) 0 or
65535 and `low_frequency` (large) byte * 257, to every connected gamepad
(`SDL_RumbleGamepad`, 250 ms, refreshed every 8 vsyncs while on, stopped on
zero). `rumble = false` mutes it. The shock driver
(`fumi/ios/shockdriver.c`) calls `scePadSetActDirect` itself.

## The pad script

The pad script (`port/input/pad_script.h`, which is the reference) drives
the pad from a text file, one entry per line:

```
<tick> <buttons-hex> [lx ly rx ry]
```

- `#` starts a comment; blank lines are ignored.
- `<tick>` is a decimal Main tick. From that tick on the pad returns the
  line's values, until the next line's tick. Ticks must increase strictly.
- `<buttons-hex>` is the game's logical button word, active high (1 =
  pressed), in hex with or without `0x`, 0 to ffff.
- `lx ly rx ry` are the sticks, 0 to 255 each, decimal or `0x` hex (128 is
  centre, 0 is left or up). All four or none; omitted means centred.
- Before the first line's tick the pad is connected with nothing pressed
  and the sticks centred.

A Main tick is one pass of the game's `Main` loop (`common/src/main.c`),
normally once every `systemStatus[1]` vsyncs (2 in PAL, so 25 a second).
Ticks count from 0, and the pad read of tick t sees the line for tick t.
The trace's line for tick t and `--ticks N` (ticks 0 to N-1) use the same
numbering. Ticks are not vsyncs: a movie or a stage load may take many
vsyncs and no Main tick, which is why scripts are keyed on ticks and stay
valid when loading takes longer.

The buttons are the word the game itself works with, `pad[0].now` and
`pad[0].flags`, which `iosPadRead` builds as
`((byte 2 << 8) | byte 3) ^ 0xFFFF` of libpad's buffer: SCE libpad's order
(byte 2 high), not the ps2sdk order.

| bit | button | bit | button |
|---|---|---|---|
| 0001 | L2 | 0100 | SELECT |
| 0002 | R2 | 0200 | L3 |
| 0004 | L1 | 0400 | R3 |
| 0008 | R1 | 0800 | START |
| 0010 | TRIANGLE | 1000 | UP |
| 0020 | CIRCLE | 2000 | RIGHT |
| 0040 | CROSS | 4000 | DOWN |
| 0080 | SQUARE | 8000 | LEFT |

The game acts on the press edge (`pad[0].flags`), so holding a button
never repeats; a script presses for a few ticks and releases.
`port/input/pad-boot.txt` walks from power-on through the boot signs and
the title to New Game, with comments on every screen it answers;
`pad-boot-mirror.txt` does the same and picks mirror mode.

### The pad recording

The window build records what the game read from the pad each Main tick
(`[dev] input_record`, on by default; `port/input/input_record.h`). The
sample is taken where the game reads the pad (`scePadRead`), keyed by the
Main tick the read belongs to, so the file is itself a pad script: the
headless build replays the session with `pad_script=` set to it. The file
is the header lines (each starting with `#`, naming the build and the
config values the simulation depends on), then one
`<tick> <buttons-hex> lx ly rx ry` line per tick whose value differs from
the one before (buttons as four hex digits, sticks in decimal), and a
closing comment `# end at tick T: N lines, C ticks read twice with
different values` (C is 0 unless something other than Main reads the pad,
such as the debug motion viewer). Lines reach the disc at most a second
after the step that read them; F12 flushes them at once.
docs/port/TESTING.md describes the replay.

## Remap screen

The Settings menu's "Remap controls" screen (docs/port/SETTINGS.md for the
player, docs/port/UI.md, "Settings menu", for the layout) edits the binding
tables live:

- **The live table.** `input_sdl.c` steps `ico_input_live_bindings()`
  (`input_config.c`) rather than a private copy, so the screen changes the
  table the next vsync uses.
- **The last press.** `input_sdl.c` reports each new press to
  `ico_input_note_press(kind, code)`: a key down (not a repeat, and only
  the keys of `keys.def`, so Escape and Alt+Enter never arrive), a mouse
  button down, and, per snapshot, a gamepad source crossing half (buttons,
  triggers, and each stick axis direction as `leftx-` to `righty+`; it
  re-arms below a quarter). `ico_input_last_press(&kind, &code)` returns a
  sequence number that changes with every press, and the screen binds the
  first press after the one that started the capture.
- **Names.** The screen's gamepad column writes each source through
  `UI_STR_PAD_*` (`port/ui/strings.h`), in ICO_GP_* order, in the menu's
  language: the face buttons by position (South, East, West, North), the
  shoulders, triggers and stick clicks as the game shows them (L1, R1, L2,
  R2, L3, R3), Select, Start, and the D-pad and stick directions
  ("D-pad Up", "L-stick Left"). These are display names only; the config
  names (`south`, `leftshoulder`, `leftx-`, `left`, `x1`) are
  `gp_names[]` and `ico_mouse_names[]` in `bindings.c`, one table each for
  the parser and the writer (`input_config.c` uses `ico_mouse_names` too).
- **Assigning.** `ico_bindings_assign(b, target, kind, code)` makes that
  device's row for the target the source alone (the other devices' rows
  stay) and takes the source off the device's other targets, so a key does
  one thing. `ico_bindings_clear(b, target)` empties all three rows
  (Square). "Reset to defaults" copies the default rows and keeps the
  scalar settings.
- **Writing.** `ico_input_write_bindings(b)` writes, through
  `ico_config_set_string`, every `[input.kb]`, `[input.mouse]` and
  `[input.pad]` target whose row differs from the default or is already in
  the file, as a string (`"K"`, `"Tab, Backquote"`, `"none"`), and
  `mouse_sensitivity`. The Settings menu then saves (`ico_config_save`) and
  reloads the live table with `ico_input_reload_bindings`: the defaults,
  then every `[input]` key as the config reads it, the same walk as
  `ico_input_apply_toml` (without `[gameplay] stick_fix`, which
  `port/game/options.c` owns).

## Tests

- `input_test` (ctest `input`, CPU; `port/input/test/input_test.c`): the
  TOML subset, binding resolution from config text, key names, quantising,
  the dead zone, the stick-fix table, the merge rules, the binding step
  (keyboard, walk, mouse decay, gamepad, merged devices), the libpad buffer
  against hand-written DualShock 2 frames (digital, analog, pressure),
  script priority, mirror, and the rumble mapping.
- `pad_script_test` (`port/input/test`): the scripted pad through `pad.c`'s
  state machine.
- `input_record_test` (`port/input/test`): the recording's line format and
  the round trip through the script reader.
- `settings_test` (`port/ui/test`): assign, clear, the row text, the
  capture's state machine and the write and reload round trip.

Not supported: per-gamepad profiles, splitting several controllers (the
game has one pad), analog trigger pressure into the 0x79 bytes, gyro and
touchpad. Open items are in docs/TODO.md.
