# Input

How a press reaches the game: SDL3 devices are sampled once per [vsync](ARCHITECTURE.md#terms),
the binding layer turns them into a virtual PS2 pad, and the host's
[libpad](ARCHITECTURE.md#terms) hands that pad to the game's own pad code as a DualShock 2 in port
0. A [pad script](ARCHITECTURE.md#terms) can stand in for the devices, and a recording writes what
the game read as a pad script. [ARCHITECTURE.md](ARCHITECTURE.md) places this layer among the
others.

## Files

| path | what it holds |
| --- | --- |
| `port/input/input.h` | the layers, the virtual pad, the binding tables and their API |
| `port/input/input_sdl.h`, `input_sdl.c` | the SDL3 device layer: keyboard, mouse, gamepads, touch events, rumble out (window build) |
| `port/input/bindings.c` | the default bindings and their config text, names, the per-vsync binding step, the mouse camera's stick |
| `port/input/input_config.c` | `config.toml`'s `[input]` and `[gameplay]` onto the bindings, the live table, the remap screen's capture and writer; `[touch]` onto the live layout edits and back |
| `port/input/keys.def` | the keyboard keys a binding can name, with their SDL scancodes |
| `port/input/vpad.c` | the virtual pad's arithmetic: merge, dead zone, stick fix, mirror, quantisation |
| `port/input/pad_host.c` | libpad: the DualShock 2 the game reads |
| `port/input/pad_script.h`, `pad_script.c` | the pad script reader and its timeline |
| `port/input/pad-boot.txt` | a pad script from power-on through the boot signs and the title to New Game |
| `port/input/input_record.h`, `input_record.c` | the pad recording |
| `port/input/mouse_look.h`, `mouse_look.c` | the mouse capture rule, Escape's button, photo mode's look accumulator |
| `port/input/mouse_camera.h`, `mouse_camera.c` | the mouse camera's speed and range for the game's hand camera |
| `port/input/pointer.h`, `pointer.c` | the mouse pointer in the menus |
| `port/input/touch.h`, `touch.c` | the touch overlay's input mapper |
| `port/input/touch_edit.h`, `touch_edit.c` | the layout edits (per posture and control), their config text and rails, and the layout editor's state machine |
| `port/input/CMakeLists.txt` | `ico_input` and the input tests |
| `port/input/test/` | the input unit tests |

Everything except `input_sdl.c` is plain C with no SDL, so the [headless
build](ARCHITECTURE.md#terms) and the tests link it (`port/input/input.h`, header comment;
`port/input/CMakeLists.txt`).

The layers, from the device to the game: `input_sdl.c` samples the devices into a raw snapshot
(`IcoInputRaw`), `bindings.c` turns it into a virtual pad (`IcoVirtualPad`), `pad_host.c` into
the DualShock 2 read buffer, and the game's own pad configuration (`ico2/fumi/ios/pad.c`,
unchanged) sits on top (`port/input/input.h`, header comment).

## Devices

`port/input/input_sdl.c` is the device layer of the [window build](ARCHITECTURE.md#terms); the
window (`port/platform/window_host.c`) drives it (`port/input/input_sdl.h`, header comment):

- `ico_input_sdl_init(path)` builds the scancode table from `keys.def`, loads the bindings from
  `config.toml` in the [user folder](ARCHITECTURE.md#terms) (defaults when the file is missing:
  `input: no <path>, default bindings`), opens the gamepads already plugged in, looks for a direct
  touch screen, and plugs the live pad into libpad (`ico_input_set_live(1)`).
- `ico_input_sdl_event(e)` takes one SDL event: keys by scancode, mouse buttons (SDL's middle
  and right swapped to the port's 2 right, 3 middle), motion, wheel, fingers, gamepad hotplug
  (`input: gamepad "<name>"`). Focus loss and going to the background release what is held.
- `ico_input_sdl_update()` runs once per vsync: sample up to 8 gamepads (buttons as 0 or 1, the
  triggers as 0..1; one snapshot per pad, `s_pad_raw`, and one with every pad merged by maximum
  and the sticks by the largest magnitude for the remap screen and the start sources), record
  the gamepads' new presses for the remap screen, take the mouse motion and the time since the
  last snapshot, run the binding step for the first player (slot 0's pad with the keyboard and
  the mouse) and again for the second (slot 1's pad alone, keyboard and mouse off in a copy of
  the bindings), add Escape's button, merge the touch overlay's pad into the first, and hand the
  two pads to libpad (`ico_input_set_vpad`, `ico_input_set_vpad_for_port`); then send the
  rumble, each slot its port's motors (slots 2 and up follow port 0).
- Gamepad slots (`open_pad`, `pad_slot`): slot 0 is the first player's pad, slot 1 the second's
  (libpad port 1, which `ico2/fumi/src/girl_act.c` connects to Yorda while `girlControlMode` is
  set), slots 2..7 drive no port. A pad keeps its slot across a reconnection by serial, then
  path, then a GUID no other known pad shares; a different pad plugged in while a player's slot
  stands empty takes that slot. The log names the slot (`input: gamepad slot N "<name>"`).

Rumble: libpad maps the game's actuator bytes through `scePadSetActAlign`'s table (the small
motor on or off to 0xFFFF, the large motor's level times 257; `port/input/pad_host.c`
(`ico_pad_rumble_map`)); the device layer sends them to every open gamepad for 250 ms, re-issued
every 8 vsyncs while running, zero with `[input] rumble = false`, and with no controller to the
phone's vibrator (`port/input/input_sdl.c` (`send_rumble`)).

While the menus' pointer is free and over a menu, the left mouse button is the pointer's click and
not Cross, so a click off every row never confirms the row the cursor is on; the remap screen's
capture still sees the press (`port/input/input_sdl.c` (`ico_input_sdl_update`)).

## Bindings

`port/input/bindings.c` turns a snapshot into a virtual pad. A binding table
(`IcoBindings`, `port/input/input.h`) maps each of 24 targets (the sixteen buttons in the pad's
bit order, then the eight stick directions) to up to four sources per device: keyboard keys,
mouse buttons (`left`, `right`, `middle`, `x1`, `x2`) and gamepad sources by position in SDL's
gamepad layout (`south`, `leftshoulder`, `dpup`, `lefttrigger`, the half-axes `leftx-` ..
`righty+`, ...; `gp_names`).

`ico_bindings_step` (`port/input/bindings.c`):

- Gamepad: a source is down at half its range or more; the analog sticks take the radial dead zone
  (`ico_input_deadzone`: shorter than the zone is zero, the rest rescaled from the zone's edge);
  digital stick targets make a vector of length at most 1, merged with the analog stick by
  magnitude.
- Keyboard and mouse: buttons by their rows; the stick directions as digital vectors; while a
  walk key is down the left stick is scaled by `walk_scale`; the mouse camera's stick (below)
  merges into the right stick.
- The gamepad pad and the keyboard/mouse pad merge with `ico_vpad_merge`: buttons ORed, each stick
  the vector with the larger magnitude, a tie to the gamepad's.

The menus get their own button word (`IcoVirtualPad.menu_buttons`): the gamepad's face buttons
and d-pad by position whatever the remap says, and a source moved onto a face or d-pad target
keeps its own default target there (a shoulder moved onto Cross still works as a shoulder); other
sources and every other device as in play (`port/input/bindings.c` (`gp_menu_word`)). It reaches
the game through two bytes of libpad's buffer (below).

`ico_bindings_start_sources` records which devices hold Start; `ico_input_start_source` names
them for the movie player's skip line (`port/input/pad_host.c`, `ico_input_start_source`;
[FMV.md](FMV.md#the-pad-during-movies)).

### The config keys

The keys, their defaults and ranges are in [CONFIG.md](CONFIG.md#input). The defaults are the
same on every platform and live as config text, parsed by the same code as a player's file
(`port/input/bindings.c` (`default_text`, `set_defaults_from_text`)).

The rows live in `[input.kb]`, `[input.mouse]` and `[input.pad]`, one key per target name (`l2`,
`r2`, `l1`, `r1`, `triangle`, `circle`, `cross`, `square`, `select`, `l3`, `r3`, `start`, `up`,
`right`, `down`, `left`, `lstick_up` .. `rstick_right`), the value a name, a comma list or an
array of names, or `"none"`; `[input.kb]` also has `walk`. The default rows are in
`default_text` (`ico_bindings_default_text`). Names are case-insensitive and ignore blanks and
underscores, with aliases such as `return`, `grave`, `lshift`, `pgup` (`name_eq`, `key_alias`).
An unknown key or a bad value is logged (`input: ignoring "<key> = <value>" (unknown key or bad
value)`), an unknown name as `input: <key>: unknown key "<name>"` (or `mouse button`,
`gamepad source`), more than four sources as `input: <key>: only the first 4 sources are used`, and
the rest still applies (`ico_bindings_set`, `bind_list`).

A new `[input]` switch needs its field in `IcoBindings` (`port/input/input.h`), its default line in
`default_text` and its parse in `ico_bindings_set` (`port/input/bindings.c`), its name in
`scalar_keys` so `config.toml` is read (`port/input/input_config.c` (`apply_keys`)), and its write
in `ico_input_write_bindings`; the `input` test covers the parse, and
[CONFIG.md](CONFIG.md#input) lists the keys.

`port/input/keys.def` lists the keys a binding can name as `KEY(ID, "config name", SDL
scancode)`: physical positions, so WASD stays WASD on any layout. Escape, Android's Back and
Alt+Enter are reserved and not listed (`port/input/keys.def`, header comment).

`port/input/input_config.c` applies `config.toml`'s `[input]` keys and `[gameplay] stick_fix`
(`ico_input_apply_toml`), owns the live table the Settings menu's remap screen edits in place
(`ico_input_live_bindings`; a change applies on the next vsync), records every new press for the
remap screen's capture (`ico_input_note_press`, `ico_input_last_press`), binds one source with
swap semantics (`ico_bindings_assign`: one key, one action; the target that loses the source
receives the old row), and writes the tables back through `port/config` for each row or setting
that differs from the default or is already in the file (`ico_input_write_bindings`). It is kept
apart from `bindings.c` so that a build without `config.toml` does not pull in the TOML reader
(`port/input/input_config.c`, header comment). The remap screen itself is [UI.md](UI.md)'s.

## The virtual pad and libpad

### The virtual pad

`IcoVirtualPad` holds the buttons (the game's logical word, active high), the menus' word and
four stick floats from -1 to +1, x to the right and y downwards (`port/input/input.h`).
`ico_input_vpad_to_frame` turns it into a byte frame: the optional stick fix, the mirror (stick X
negated on both sticks), then `ico_input_quantise` (0 to 128, the DualShock's idle; -1 to 0; +1 to
255) (`port/input/vpad.c`).

The stick fix scales a stick vector by 1 + 0.2 x d / 45, d the angle in degrees to the nearest
axis, so the off-axis divisor of the game's `iosPadNormalizeStick` (`ico2/fumi/ios/pad.c`)
cancels; it never shrinks a vector and is capped so neither component leaves -1..+1. No libm: a
small arctangent, accurate to 0.25 degrees (`port/input/vpad.c` (`ico_input_stick_fix`,
`atan01_deg`)).

`[gameplay] stick_fix` switches it; its default and the option's readers are
[GAME_FEATURES.md](GAME_FEATURES.md#stick-fix)'s.

### libpad

`port/input/pad_host.c` is libpad: a DualShock 2 in port 0, slot 0, when something feeds it, and
one in port 1, slot 0, while a gamepad sits in the device layer's slot 1
(`ico_input_set_gamepad_present`); each port has its own virtual pad, mode, actuator table and
motors; the script, the recording, the keyboard and the mouse belong to port 0 (`port/input/pad_host.c`,
header comment):

- Feeds, in order: a loaded pad script, then the live virtual pad (when the device layer has
  plugged it in), else a released, centred frame (`ico_input_frame`). With neither feed,
  `scePadGetState` reports no controller, `ico2/fumi/ios/pad.c`'s `controler_stable_check` keeps
  the port's error flag set and never calls `scePadRead`, and the game runs as a console with no
  pad plugged in, the headless default.
- The pad powers up in digital mode and answers the game's queries so that
  `controler_stable_check` walks it into analog mode (locked), one step per
  [Main tick](ARCHITECTURE.md#terms); the first read is on tick 7 (id 0x73) and compares against
  a never-filled buffer, so a script's first press belongs on tick 8 or later.
- `scePadInfoPressMode` answers 0: the game never reads the pressure bytes, so the pad is not
  walked into pressure mode; `scePadEnterPressMode` still works (id 0x79 and 12 pressure bytes).

`scePadRead` fills 32 bytes in Sony's libpad layout (`port/input/pad_host.c` (`scePadRead`)):

| byte | content |
| --- | --- |
| 0 | status, 0 = ok (0xFF when disconnected) |
| 1 | terminal id: 0x41 digital, 0x73 analog, 0x79 analog with pressure |
| 2, 3 | the buttons, active low: byte 2 the logical word's high byte, byte 3 the low byte |
| 4-7 | right x, right y, left x, left y (analog ids; centred in digital mode) |
| 8-19 | pressure, id 0x79 only: 0 or 255 per button |
| 20, 21 | the menus' word, active low, high byte first: the port's own bytes; equal to 2, 3 for a script |

The game reads bytes 20 and 21 through `ico_pad_menu_word` where it uses the default button table:
`iosPadRead` takes the menus' word when its configuration is `iosPadConfDefault`, so menus and
the readers of `pad[0]` with the default table see the gamepad's face buttons by position
(`ico2/fumi/ios/pad.c` (`iosPadRead`); `port/input/input.h` (`ico_pad_menu_word`)). Each read is
also sampled for the pad recording (`ico_input_record_sample`).

## Pad scripts and recordings

### Pad scripts

A pad script drives the pad instead of a person (`port/input/pad_script.h`, header comment). One
entry per line:

    <tick> <buttons-hex> [lx ly rx ry]

- `#` starts a comment; blank lines are ignored.
- `<tick>` is a decimal Main tick, counted from 0; from that tick on the
  pad returns the line's values until the next line's tick. Ticks increase strictly.
- `<buttons-hex>` is the game's logical word, active high, 0 to ffff, with or without `0x`:
  L2 0001, R2 0002, L1 0004, R1 0008, Triangle 0010, Circle 0020, Cross 0040, Square 0080, Select
  0100, L3 0200, R3 0400, Start 0800, Up 1000, Right 2000, Down 4000, Left 8000 (`ICO_PAD_*`).
  This is Sony libpad's order (byte 2 high), not ps2sdk's.
- `lx ly rx ry` are 0 to 255 each, decimal or `0x` hex, 128 the centre; all four or none.
- Before the first line's tick the pad is connected, released, sticks centred.

The tick is the one the [trace](ARCHITECTURE.md#terms) and `--ticks` count: the host counts a tick
when Main reports it done (`ico_host_main_tick`, which sets `ico_pad_script_set_tick`), so the pad
read of tick t sees the line for tick t. Ticks are not vsyncs: a movie or a stage load may take
many vsyncs and no tick (`port/input/pad_script.h`, Tick; `port/platform/trace_host.c`).

A script is loaded from `--pad-script FILE` or the ini's `pad_script=` (`[dev] pad_script`); the
[headless build](ARCHITECTURE.md#terms) also takes `pad-script.txt` beside the executable when it
exists; a window build takes a script only when one is named, so a stray file never replaces the
live controller (`port/platform/main_host.c` (`setup_pad`)). A bad script stops the run with the
file and line (`pad-script: <file>:<line>: ...`). The Windows package removes `pad-script.txt`
(`tools/package_win.sh`).

`port/input/pad-boot.txt` plays from power-on through the boot signs and the title to New Game,
repeating a cycle of 3-tick presses that each advance the screen that is up or do nothing there,
since load and memory card latency move the screens' ticks; its comments walk every screen
(`port/input/pad-boot.txt`, header comment). The headless UI checks use its presses
(`port/ui/test/headless_common.py`).

### Recordings

The pad recording writes what the game read from the pad as a pad script, so the headless build
replays a session with `pad_script=<the file>` (`port/input/input_record.h`, header comment). The
sample is taken in `scePadRead`, keyed by the Main tick the read belongs to, which is the script's
own key. The play word is recorded; the menus' word is not (it equals the play word in a script).

The file is a header of `#` lines, then one line per tick whose value differs from the one before:

    <tick> <buttons-hex> lx ly rx ry

with the buttons as four hex digits and the sticks in decimal, and a closing comment `# end at
tick T: N lines, C ticks read twice with different values` (C is 0 unless something other than
Main reads the pad). Lines are written after the vsync step that read them
(`ico_input_record_poll`) and flushed every 60 vsyncs (`FLUSH_POLLS`); F12 flushes them at once
(`ico_input_record_flush`, called from `port/platform/window_host.c`).

`port/platform/main_host.c` (`record_open`, `record_header`) opens it: on by default in the window
build as `logs/input-<time>.txt`, off headless; the ini's `input_record=` (`[dev] input_record`)
takes `false`, `true` or a path. The header (`# ico-pc input recording`) carries the build, the
config values the simulation depends on (`record_keys`, which includes `gameplay.hold_type`), the effective fixed clock and stick fix,
and the [native frame](ARCHITECTURE.md#terms)'s width and aspect; a width change adds
`# tick N gs_width W`. Loading a recording as the pad script compares each header value with the
run's and logs each difference (`record_check`), since the replay may then not follow the session.

## The mouse camera and the pointer

### Capture

`port/input/mouse_look.h` decides what the mouse does. The window fills `IcoCaptureState` from the
game's state once a vsync and passes `ico_mouse_capture_rule`'s answer to
`ico_input_sdl_set_capture` (`port/platform/window_host.c`):

- `ICO_CAPTURE_STICK` in play (the boy in a stage past the title, on the play or scene layout,
  nothing paused or loading, no movie, model viewer or credits): the pointer is hidden and held
  (relative mode), its motion is the right stick.
- `ICO_CAPTURE_DELTA` in photo mode: hidden and held, its motion goes to an accumulator photo
  mode's screen takes once a Main tick (`ico_mouse_look_take`, `port/ui/photo_ui.c`).
- `ICO_CAPTURE_OFF` otherwise, and always without the focus or with the mouse camera off: the
  pointer is free.

Any change of mode starts the look from centre (`port/input/input_sdl.c`
(`ico_input_sdl_set_capture`)).

### The mouse's stick

In play the captured motion moves a held look offset: 400 counts cross the whole offset at
sensitivity 1. After `mouse_hold` seconds without motion the offset relaxes to centre with a 0.25 s
time constant, so the camera swings back behind Ico; with `mouse_return` off it stays. The game's
camera is position control with a dead zone of 48, so the stick is the offset's direction at
48.5/127.5 (just past the dead zone) plus its length up to 120/127.5 (`port/input/bindings.c`
(`mouse_look`); `port/input/input.h` (`IcoBindings.mouse_camera`)); the relax runs on the
snapshot's real time step.

`IcoBindings.mouse_drives` is set while the mouse's stick won the merge with the keys, the gamepad
and the touch look pad (`ico_bindings_mouse_merged`). Only then do `mouse_camera_speed` and
`mouse_full_range` apply: the game's hand camera (`ico2/omori/src/hand-camera.c`) asks
`ico_mouse_camera_speed` (exactly 1 otherwise) and `ico_mouse_camera_full_range`, and steps with
`ico_mouse_camera_step`, which is the game's own step at speed 1 (`port/input/mouse_camera.h`).

### Escape and Back

Escape and Android's Back never reach the bindings. They press Start in play (the pause menu),
nothing on the game's Button configuration screen (where any button is assigned to the highlighted
action), and Triangle everywhere else; never a quit (`port/input/mouse_look.h`
(`ico_escape_target`); `port/platform/window_host.c` (`escape_event`)). The button is chosen at the
press and held until the release, and a press lasts at least two steps
(`ICO_ESCAPE_TAP_STEPS`), since the game reads the pad every second vsync (`ico_escape_latch`,
`ico_escape_take`).

### The pointer

`port/input/pointer.h` is the mouse in the menus. While the capture mode is off, the device layer
feeds it the pointer's place (0..1 across and down the window), left-button clicks, the wheel
(whole notches, up positive, SDL's flipped direction undone) and leaving the window; the menus'
glue takes what happened once a Main tick (`port/ui/ui_mouse.c` (`ui_mouse_tick`)) and says whether
a menu is up (`ico_pointer_set_menu`). Motion in the first 100 ms after the pointer is freed is
ignored, since SDL moves it back to where it was held (`port/input/input_sdl.c`
(`POINTER_SETTLE_NS`)). The menus' side is [UI.md](UI.md)'s.

## Touch

`port/input/touch.h` maps fingers on a direct touch screen to a virtual pad, merged with the
bindings' like any other source before the stick fix, the mirror and the quantisation
(`port/input/touch.h`, header comment):

- **Layout**: every control is a fixed multiple of one unit q (output pixels per layout
  millimetre) from an edge of the safe area, in a PlayStation pad's arrangement, the often used
  controls at the lower corners; q follows the screen's density and the size setting, clamped so
  the layout fits, and a fold splits or moves the layout (`port/input/touch.c`, layout comment).
- **Left stick**: the first finger down in its area is the stick's centre while it stays down; its
  offset is the deflection, with a small dead zone for jitter and no rescaling, so the game's own
  reading decides walk and run as with a real stick.
- **Look pad**: a finger's offset from where it went down is the right stick; after it lifts the
  stick decays by `mouse_decay` per vsync.
- **Buttons**: a finger holds the button it is on, sliding moves the hold, a tap between two steps
  is held for one step; up to 10 fingers at once.
- **Visibility**: `touch_mode` Off never maps or draws; Auto hides the overlay while a gamepad is
  connected (and drops the touches) and 5 s after the last finger event, with 300 ms fades; Always
  draws at full opacity and maps with a gamepad too. `touch_opacity` scales the drawing.

`input_sdl.c` wires it: the finger events, cancel on focus loss and backgrounding, the step once a
vsync (`ico_touch_update`), the layout from the output's size, safe area, density and fold that
the window hands over (`ico_input_sdl_set_touch_layout`, `ico_input_sdl_set_touch_env`), and a
copy for drawing taken at the step (`ico_input_sdl_touch_overlay`). The Settings rows for touch
appear once a touch screen is known (`ico_input_sdl_touch_present`; `input: touch screen (...)`).
The overlay's drawing is `port/ui/touch_ui.c` ([UI.md](UI.md)).

### Layout edits

A player's arrangement is an `IcoTouchEdits` (`port/input/touch_edit.h`): per posture
(`ICO_TOUCH_RULE_FULL`, `SPLIT`, `TABLETOP`, `ICO_TOUCH_POSTURES`) and per editable control
(`ICO_TOUCH_CTL_*`: the D-pad as one control, the four faces, R1, L1, L2, R2, Select, Start, the
stick and the look pad) an `IcoTouchCtlEdit`, plus `snap`, `grid` and a generation `gen` that
every change bumps.

- **Storage**: an anchor edge on each axis (`ax` left or right, `ay` top or bottom) and an offset
  in q from it, the unit the defaults are written in (`port/input/touch.c`, layout comment), so a
  control keeps its real distance from its corner on every screen, in either orientation, outside
  the safe area, and it scales with the Touch size setting like the defaults. The offset is the
  control's centre; for the look pad, a rectangle, it is the distance of its two edges on the
  anchor sides, with its width and height `w` and `h` in q. `scale` multiplies the default size
  (`ICO_TOUCH_EDIT_SCALE_MIN` 0.7 to `MAX` 1.6; a face at 0.7 is 7.7 mm, Android's 48 dp), `alpha`
  multiplies the Touch opacity row's value, 0 is hidden. `set` 0 is the default place.
- **Applied inside the layout builder**: `ico_touch_layout_edits` (`ico_touch_layout_env` is the
  same with no edits) places the defaults, then `apply_edits` (`port/input/touch.c`) lays each
  set control of the layout's posture over them: the centre from its anchors, the size from the
  default dimension times `scale`, the box clamped inside the region and out of the hinge band
  (`ico_touch_edit_clamp`: the nearer side of the band that holds it; shrunk toward the minimum
  scale when neither does; never dropped), the rects written as `place()` writes them, `alpha`
  into the button's `alpha`, `stickAlpha` and `lookAlpha`, and `editGen` from `gen`. A moved
  stick's area is a square of 4.4 R around its home cut to its side of the band; a moved look
  pad has its own rect; the look sensitivity radius stays the default. The flat-crease check that
  sends a layout to `TABLETOP` sees the edited positions. The same runs on every rebuild (another
  phone, a rotation, a fold, a safe-area change), so a stored edit can never leave the screen. An
  unedited layout is the default to the byte (`touch_edit` `test_defaults`).
- **Hidden**: the hit test skips a button at `alpha` 0, the step skips the stick's area at
  `stickAlpha` 0 and the look pad at `lookAlpha` 0; a hidden button is also left out of the
  flat-crease check (`ico_touch_step`, `hit`, `controls_touch`).
- **The rails**: `ico_touch_ctl_hideable` is true for Select, L1, L2, R2 and the look pad only;
  the D-pad, the faces, R1, Start and the stick (the menus and the game need them) keep
  `alpha` at or above `ICO_TOUCH_EDIT_ALPHA_MIN` (0.25), and `scale` is always in range.
  `ico_touch_edit_sanitize` enforces them on every editor change and on every parsed config line.
- **Config text**: `ico_touch_edit_text` and `ico_touch_edit_parse`, one control as
  whitespace-separated tokens `l<n>` or `r<n>`, `t<n>` or `b<n>`, optional `s<scale>` and
  `o<percent>`, and for the look pad `w<n> h<n>`; numbers are plain decimals, locale-free, offsets
  and sizes with two decimals and trailing zeros dropped, the scale always with two, the opacity a
  whole percent (`r20 b10.5 s1.00 o100`, `l50 t0 w60 h30 o100`). `default` is an unset control.
  `hidden` alone is a hideable control at its default place with `alpha` 0
  (`ICO_TOUCH_ANCHOR_DEFAULT` anchors); a hidden control that was moved writes its position, `s1.00`
  and `o0` instead, so it keeps its place when shown again. A line with a token it does not know,
  a value out of range (offsets 0 to 500, `s` 0.7 to 1.6, `o` 0 to 100, `w` and `h` 20 to 500), a
  token twice, a missing anchor, or `hidden` on a control that cannot be hidden is rejected and the
  control stays at its default.
- **Config**: `[touch]` holds `snap`, `grid` (2, 4 or 8) and one table per posture (`full`,
  `split`, `tabletop`) of `<control> = "<text>"` ([CONFIG.md](CONFIG.md#touch)).
  `ico_input_apply_touch_toml` reads it at start, `ico_input_write_touch_edits` writes the live
  edits (a key only when it differs from the default or is already in the file; a reset control
  whose key is in the file becomes `default`; an untouched config gains nothing),
  `ico_input_reload_touch_edits` reads the saved file back, and `ico_input_live_touch_edits` is the
  one live table, as `ico_input_live_bindings` is for the bindings (`port/input/input_config.c`).
  A bad line is logged once (`touch: ignoring full.l2 = "..." (...)`) and ignored.

### The layout editor

`IcoTouchEditor` (`port/input/touch_edit.h`) is a state machine over a working copy of the live
edits for the posture the phone is in (`ico_touch_edit_begin`). The finger events arrive through
`ico_touch_edit_event` and are queued; `ico_touch_edit_tick` (once a Main tick, with the preview
layout built from the working copy and the gamepad's `ICO_PAD_*` word) drains them and returns
`ICO_TOUCH_ED_LEAVE` once Done was taken; `ico_touch_edit_end(ed, 1)` copies the work to the live
edits (the generation moves past any preview built during the session); `ico_touch_edit_view`
fills the `IcoTouchEditView` the drawing reads.

- **Gestures**: a tap selects (on empty space, deselects); a move of more than
  `ICO_TOUCH_EDIT_DRAG_MM` (2) drags; on release the anchors become the nearer edge on each axis
  and, with `snap`, the offsets round to multiples of `grid`; a second finger pinches, scale times
  the ratio of the fingers' distances, rounded to 0.05 on release. A cancel (focus loss) or a
  posture change drops the gesture and puts the control back; earlier edits stay.
- **Refusing**: a drop that covers another control's middle springs back and shows the refused
  note for `ICO_TOUCH_EDIT_REFUSED` ticks; a change that would put a control on a flat crease and
  fold the layout to `TABLETOP` is undone and refused. The overlap mask uses the controls' real
  shapes (discs for the round buttons, the D-pad as two bars); the look pad is on neither side of
  either rule because it lies under R1, R2 and Start by design.
- **Toolbar**: ten keys (`ICO_TOUCH_ED_TB_*`: Done, Undo, Reset, Snap, Grid, Size -, Size +,
  Opacity -, Opacity +, All) in a 2 x 5 block of 6 q x 7 q keys at the one spot the minimum
  region leaves clear of every default control (FULL and TABLETOP at x Rt-53 to Rt-19, y T+13
  to T+28; SPLIT as two columns by five rows on the left side); the derivation is the header
  comment of `port/input/touch_edit.c`, and `ico_touch_edit_toolbar` returns the boxes. The
  toolbar wins the hit test over a control dragged under it. Reset turns the first row, or in
  SPLIT the first three rows, into the confirm strip (`ICO_TOUCH_ED_CF_*`: this screen, all
  screens, cancel). Size and Opacity steps are 0.1 and 10 % on the selected control, or on every
  control with All; Undo is `ICO_TOUCH_EDIT_UNDO` (8) deep.
- **Gamepad**: L1 and R1 cycle the selection, the D-pad nudges one grid step (the first press at
  once, then after `ICO_TOUCH_EDIT_REPEAT_DELAY` ticks held every `ICO_TOUCH_EDIT_REPEAT`
  ticks), L2 and R2 change the size, Square and Cross the opacity (Cross with nothing selected
  toggles All), Select undoes, Start opens the Reset strip (left and right move, Cross picks,
  Circle, Triangle or Start cancels), Triangle and Circle are Done. A nudge obeys the same
  refusals as a drop.

The editor is driven by the Edit layout page (`port/ui/touch_edit_ui.c` (`ui_touch_edit_proc`),
[UI.md](UI.md#the-touch-layout-editor)); `input_sdl.c` wires it:

- `touch_rebuild` builds the layout with `ico_touch_layout_edits`, from the editor's working copy
  while it is active and from `ico_input_live_touch_edits` otherwise; `ico_input_sdl_init` applies
  the file's edits after the bindings so the first layout carries them.
- `touch_finger` hands the finger events to `ico_touch_edit_event` while the editor is active and
  to the mapper otherwise; `touch_cancel` also sends the editor a cancel.
- `touch_step` rebuilds the layout when the size or `editGen` changed; while editing it steps the
  mapper with the mode Off (so a drag never presses a button, and nothing merges), draws at opacity
  1 whatever the Touch opacity row says, clears the debug info and fills the snapshot's `edit`
  view. `touch_synth_mouse` is unchanged: the menus on a phone are the overlay's own D-pad and
  faces, which is why the rails keep them.
- The window host gives the page its hooks (`ico_input_sdl_touch_editor`,
  `ico_input_sdl_touch_edit_begin`, `ico_input_sdl_touch_edit_end`, `ico_input_sdl_touch_rule`,
  `ico_input_sdl_touch_layout`; `port/platform/window_host.c` (`ui_settings_set_touch_edit_host`));
  begin cancels the mapper's fingers first. A fold, density or safe-area change rebuilds the layout
  with the edits, and the editor follows a posture change.

## Tests

| test | checks |
| --- | --- |
| `input` (`input_test.c`) | binding resolution from config text, the `[touch]` tables onto the layout edits and back (a config nobody edited gains nothing, a reset control is rewritten as `default`, a bad grid or line is rejected), the dead zone and quantisation, the stick fix, merging, the libpad buffer against a hand-written DualShock 2 frame, rumble, Escape's button |
| `input_record` (`input_record_test.c`) | the recording's line format, the round trip through the script reader over a long session, the "only when it changes" rule, a second read in a tick, the poll's timing, a scripted session recorded through libpad and loaded back |
| `touch` (`touch_test.c`) | the mapper's layouts over phones, a notch, foldables, a tablet and monitors at every size, the hinge rule, the floating stick, the d-pad and face buttons, sliding, cancel, the look pad's decay, hiding and fades, the merged pad with the stick fix off and on |
| `touch_edit` (`touch_edit_test.c`) | an unedited layout equal to the default field by field and byte for byte over the screen table, the anchor arithmetic on four screens with and without insets, the clamp into the region and out of the band, hidden controls and the rails, the stick's and the look pad's areas, the flat crease seeing the edits, the config text round trip with bad and out-of-range lines, the editor fed synthetic fingers and gamepad words (tap, drag and snap, the refused drop, overlap, cancel, pinch, the steppers, All, the posture switch, Undo, Reset, Done), and the toolbar clear of every default control on the screen table and the smallest region of each posture |
| `mouse_camera` (`mouse_camera_test.c`) | the camera step arithmetic, and the speed and range applied only while the mouse drives the camera |
| `pad_script` (`pad_script_test.c`) | the script reader and what libpad then shows the game's pad state machine, replayed from `pad.c`'s libpad calls in their order |
| `movie_pad` (`port/platform/test/movie_pad_test.c`) | the Start edge through the game's own pad code during a movie ([FMV.md](FMV.md)) |

The first five are registered in `port/input/CMakeLists.txt`, `pad_script` in
`port/null/CMakeLists.txt`, `movie_pad` in `port/platform/CMakeLists.txt`. Run them with
`ctest --preset linux-x64 -R '^(input|input_record|touch|touch_edit|mouse_camera|pad_script|movie_pad)$'`
([TESTING.md](TESTING.md)).
