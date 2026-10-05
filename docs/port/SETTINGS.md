# Settings

ico-pc has a Settings menu of its own, drawn in the style of the game's
menus. Everything in it is saved in `config.toml` (docs/port/CONFIG.md says
where that file is), so you can also edit the file by hand while the game is
closed.

## Opening it

- **On the title screen:** move down from "New Game" (or "Continue / New
  Game") to "Settings" and press Cross. Triangle (or the menu's "Back")
  returns to the title.
- **During play:** press START to pause, choose "Options", then move to
  "Settings" (below the last option) and press Cross. Triangle goes back
  to Options.

The menu works like the game's own Options screen: up and down move the
cursor, left and right change the value on the selected line, Cross opens a
section or confirms, Triangle goes back. Changes take effect at once and
are written to `config.toml` when you leave a screen.

## The language and 50/60 Hz screens

The PlayStation 2 game asked for the language and for 50 Hz or 60 Hz at
every first start. ico-pc skips both screens:

- the **language** is taken from `[game] language` in `config.toml`, or,
  when it is `"auto"` (the default), from your system's language (English
  if the game does not have yours);
- the **video mode** is taken from `[video] video_mode`: `"pal50"` (the
  default, what the PAL game used unless you chose otherwise) or `"60hz"`.

Both can be changed in Settings. If your memory card already holds the
game's system file (written by the game when you save), the game uses the
values stored there, as it always did, unless `config.toml` names a
language or a video mode explicitly: then that wins. The card is updated
with the values in use the next time you save.

## What is in it

**Display**

| line | values | what it does |
| --- | --- | --- |
| Preset | Original, Enhanced | Original is the PS2 picture; Enhanced turns the options below on (docs/port/DISPLAY.md) |
| Resolution | Window, 1x to 4x | how sharp the 3D scene is (Enhanced) |
| Aspect ratio | 4:3, 16:10, 16:9, Auto | widescreen (Enhanced) |
| Fullscreen | On, Off | borderless fullscreen; Alt+Enter also switches |
| Vertical sync | On, Off | waits for the screen's refresh |
| Texture filtering | Original, Trilinear, Anisotropic | (Enhanced) |
| Full-height picture | On, Off | keeps all 512 lines (Enhanced) |
| Frame rate | shown when `[video] framerate` is in `config.toml` | read-only in this version |
| Video mode | PAL 50 Hz, 60 Hz | the boot screen's choice. The game runs at 25 updates a second in PAL 50 Hz and 30 in 60 Hz, as on the console; switching resets the picture the way the boot screen did |

**Audio**

| line | values | what it does |
| --- | --- | --- |
| Volume | 0 % to 100 % | saved as `[audio] volume`. The audio output does not apply it yet; the line says so |

**Controls**

| line | what it does |
| --- | --- |
| Remap controls | opens the remap screen (below) |
| Analogue stick fix | On makes diagonals on the stick as strong as straight pushes (docs/port/INPUT.md, "Stick fix"); Off is the original |
| Mouse sensitivity | 0.25 to 4: how fast the mouse turns the camera |

**Gameplay**

| line | what it does |
| --- | --- |
| Shadows never take Yorda | On: the shadows fight Ico and never carry Yorda off. For a less stressful game; a few scripted scenes still show the capture (docs/port/OPTIONS.md) |
| Mirror mode | chosen when you start a New Game, not here |

**Language**: English, Français, Deutsch, Italiano, Español. The Settings
menu changes at once; the game's own subtitles and menu pictures follow the
next time the game loads them (the next room, the next menu).

**Achievements**: the list of achievements, unlocked or locked, with the
description of the selected one. Hidden achievements show as "???" until
unlocked. "Assisted" means it was unlocked in a run where developer mode,
"Shadows never take Yorda" or a developer start stage was on; doing it again
without them makes it a normal unlock (docs/port/ACHIEVEMENTS.md).

**Developer mode**: On restores the development build's debug menu (SELECT
opens it) and pauses achievements while it is on (docs/port/DEVELOPER_MODE.md).

## Remapping the controls

The remap screen lists the PS2 pad's sixteen buttons and the eight stick
directions, with what is bound to each on the keyboard (and mouse) and on a
gamepad.

- **Cross** on a line: "Press a key or button…". The next key, mouse
  button, gamepad button, trigger or stick push is bound to that PS2
  button, for that device only (a key replaces the keyboard binding, a pad
  button the gamepad binding). The same key or button is taken off any
  other PS2 button it was on. If nothing is pressed for 10 seconds the
  binding is left as it was.
- **Square** on a line clears all of its bindings.
- **Reset to defaults** (at the end of the list) puts every binding back.
- **Triangle** or **Back** leaves; the bindings are written to
  `[input.kb]`, `[input.mouse]` and `[input.pad]` in `config.toml`.

Escape always closes the game and cannot be bound. The menus need Cross,
Triangle and the directions; if you clear or move those, use the keyboard's
defaults that remain, or "Reset to defaults". Gamepad sticks always drive
the sticks; binding a stick direction adds a button that does the same.

## Notes

- A setting that the window has to apply (resolution, fullscreen, vsync,
  filtering, aspect) changes at the next frame.
- Developer mode, the stick fix and "Shadows never take Yorda" only change
  what the options above describe; the rest of the game is unchanged.
