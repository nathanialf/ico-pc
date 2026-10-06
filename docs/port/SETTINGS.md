# Settings

ico-pc has a Settings menu of its own, drawn in the style of the game's
menus. Everything in it is saved in `config.toml` (docs/port/CONFIG.md says
where that file is), so you can also edit the file by hand while the game is
closed.

## Opening it

- **On the title screen:** move down from "New Game" (or "Continue / New
  Game") to "Settings" and press Cross. Triangle, Circle (or the menu's
  "Back") returns to the title. "Settings" and "Quit to desktop" appear
  together with "New Game", once the memory card check has finished.
- **During play:** press START to pause, choose "Options", then move to
  "Settings" (below the last option) and press Cross. Triangle or Circle
  goes back to Options.

The menu works like the game's own Options screen: up and down move the
cursor, left and right change the value on the selected line, Cross opens a
section or confirms, Triangle or Circle goes back (on a gamepad Circle is
the east button, B on an Xbox layout). Changes take effect at once and are
written to `config.toml` when you leave a screen.

## Quitting

The title screen has a "Quit to desktop" line under "Settings". Cross on it
asks "Quit to desktop?" with Yes and No (the cursor starts on No). Yes
closes the game exactly as closing the window does: anything Settings has
not written yet is saved to `config.toml`, the achievements are written,
the sound stops and the window closes. No, Triangle or Circle return to the
title. During play the pause menu's own "End Game" returns to the title as
on the PS2; there is no "Quit to desktop" there (close the window or press
Escape).

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
| Frame rate | Original, Uncapped, 60 fps, 120 fps, 144 fps, 240 fps ("Uncapped" and "fps" are translated: Illimité, Unbegrenzt, Illimitato, Sin límite) | (both presets) how often the picture is redrawn, smoothing motion between the game's updates (docs/port/DISPLAY.md, "Smooth motion"); saved as `[video] framerate` (`"original"`, `"uncapped"` or the number). Default Uncapped; Original keeps one picture per update (the PS2's cadence) in either preset. Right steps in the order listed and wraps; a number from `config.toml` that is not listed (`"100"`) steps to the listed rate above it (Right) or below it (Left), past the ends to Original (Right) or Uncapped (Left) |
| Video mode | PAL 50 Hz, 60 Hz | the boot screen's choice. The game runs at 25 updates a second in PAL 50 Hz and 30 in 60 Hz, as on the console; switching resets the picture the way the boot screen did. It changes only when Settings was opened from the title screen (the tick rate arms the game's timers): opened from the pause menu the row shows the value followed by "(title only)" ("titre seul", "nur Titel", "solo titolo", "solo título") and Left and Right do nothing |
| Menu text | Port font, Classic | how the game's own menus (title, Options, pause, save and load, game over, the boot screens) draw their words. Port font (the default) writes them in the same typeface as this Settings menu, at the same places and sizes; Classic shows the PlayStation 2's original lettering. The logo, the copyright line, the button symbols and the pictures are the originals either way. Changes at the next frame; saved as `[game] classic_menu_text` |

**Audio**

| line | values | what it does |
| --- | --- | --- |
| Volume | 0 % to 100 % | saved as `[audio] volume` and applied at once: the SDL output scales its blocks by it (docs/port/AUDIO.md, "Output") |

**Controls**

| line | what it does |
| --- | --- |
| Remap controls | opens the remap screen (below) |
| Mouse sensitivity | 0.25 to 4: how fast the mouse turns the camera |
| Circle goes back | On (the default): Circle (gamepad B) also backs out of the game's own menus wherever Triangle does (below); Off: only Triangle, as on the PS2. Saved as `[game] circle_back` |

**Gameplay**

| line | what it does |
| --- | --- |
| Shadows never take Yorda | On: the shadows fight Ico and never carry Yorda off. For a less stressful game; a few scripted scenes still show the capture (docs/port/OPTIONS.md) |
| Analogue stick fix | On makes diagonals on the stick as strong as straight pushes (docs/port/INPUT.md, "Stick fix"); Off is the original |

Mirror mode is not a Settings line: it is chosen when you start a New Game
(below). The films follow it.

**Language**: English, Français, Deutsch, Italiano, Español. The Settings
menu changes at once, and so does the game's menu text with Menu text set to
Port font; the game's own subtitles, and its menu pictures with Classic, follow the
next time the game loads them (the next room, the next menu). The row works
from the title and from the pause menu; a change mid-run chooses
language-dependent objects at once, where the PS2 could set the language only
at boot (docs/port/DIVERGENCES.md, A23).

**Achievements**: the list of achievements, unlocked or locked, with the
description of the selected one. Hidden achievements show as "???" until
unlocked. Achievements are suspended while developer mode or a developer
start stage is on, and for the rest of that run (docs/port/ACHIEVEMENTS.md,
"Suspension"); "Shadows never take Yorda" does not suspend them.

**Extras**: opens the Extras page (below). The row sits after Achievements
and is there only when Settings was opened from the title; from the pause
menu it is hidden, the rows below it move up, and the cursor skips it.

**Developer mode**: On restores the development build's debug menu (SELECT
opens it) and suspends achievements while it is on (docs/port/DEVELOPER_MODE.md).

## Extras

Settings > Extras (title only; the galleries leave the stage, and the title
menu has no room for more rows) opens a page with **Music**, **Models**,
**Credits** and Back. The three entries are not available yet: Cross on one
does nothing but write `extras: <entry> not available yet` to the log. Credits
shows the locked style, its label and its value ("Locked") greyed and, with
the cursor on it, the note "Finish the game to unlock"; it stays locked until
the ending has been reached. docs/port/EXTRAS.md says what each entry will be
and which package adds it.

## Mirror mode

Mirror mode plays the whole game flipped left to right: the castle, Ico and
Yorda, the camera's turns. The stick's left and right are swapped with it,
so pushing the stick left still moves Ico left on the screen, and the sound
is swapped between the left and right speakers. Subtitles, menus and the
other text are not flipped: they read normally. The game itself is the same
game; only what you see and hear is flipped.

It is chosen when you start a New Game: after "Vibration", the "Mirror
mode" screen offers Off (selected) and On. Left and right move between
them, Cross or START confirms, Triangle or Circle goes back to the
vibration screen.

The choice belongs to that game and goes with its saves:

- Saving the game in a slot remembers the choice for that slot (in
  `config.toml`, not on the memory card: the card files stay exactly as the
  PS2 writes them).
- Loading a slot puts the game back in the mode it was saved in.
- Saving over a slot that held a game in the other mode: the new game's
  choice wins.
- A save made on a PS2 (or copied in from another card) has no entry and
  loads in normal mode, as does a slot whose save was replaced outside
  the port.
- The title screen is always in normal mode. A cleared game's "New Game"
  (loading a cleared save starts a new game) asks again.

The Settings menu does not show the choice and cannot change it. The films
are flipped with the game.

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
- The gamepad column names sources by position in the menu's language:
  South, East, West, North (the face buttons), L1 R1 L2 R2 L3 R3, Select,
  Start, the D-pad and the two sticks' four directions ("D-pad Up",
  "L-stick Left"; "Croix haut", "Kreuz oben", "Stick G gauche", ...).
- **Triangle**, **Circle** or **Back** leaves (while "Press a key or
  button…" is shown, Circle is bound like any other button instead); the
  bindings are written to
  `[input.kb]`, `[input.mouse]` and `[input.pad]` in `config.toml`.

Escape always closes the game and cannot be bound. The menus need Cross,
Triangle and the directions; if you clear or move those, use the keyboard's
defaults that remain, or "Reset to defaults". Gamepad sticks always drive
the sticks; binding a stick direction adds a button that does the same.

## Circle goes back

On the PS2, Triangle backs out of the game's menus and Circle does nothing
there. ico-pc's own screens (Settings and its pages, the remap and
achievement lists, the Mirror mode and Quit screens) always take Circle as
Triangle. In the game's own menus Circle does the same while "Circle goes
back" (Settings > Controls, `[game] circle_back`) is On, the default:

- the Options screen and the Settings line in it (back to the pause menu);
- the pause menu (closes it, as Triangle and START do);
- the vibration screen after New Game (back to the title);
- the memory card screens: choosing the slot, choosing the save to load or
  to save over, "Save?", the overwrite and format questions, the
  "End Game" question and the delete result, each where Triangle cancels.

It changes nothing where Circle or Triangle mean something else: on the
Button Configuration screen Circle is one of the buttons you assign, and on
the Brightness screen Triangle resets the setting (Circle does nothing
there either way). The title screen and the game over screen have no back
action. Off, every menu checks Triangle alone, as on the PS2.

## Notes

- A setting that the window has to apply (resolution, fullscreen, vsync,
  filtering, aspect) changes at the next frame.
- Developer mode, the stick fix and "Shadows never take Yorda" only change
  what the options above describe; the rest of the game is unchanged.
