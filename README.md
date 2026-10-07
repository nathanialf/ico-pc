# ico-pc

A native PC port of **ICO** (Sony Computer Entertainment, 2001) for 64-bit
Windows and Linux, including the Steam Deck. It runs the game's own code, so
the game plays, sounds and times as the PAL PlayStation 2 release does, with
optional extras (higher resolution, widescreen, smoother motion, controller
remapping, achievements) that are off or neutral until you choose them.

> [!IMPORTANT]
> This project is not affiliated with Sony Interactive Entertainment or Team
> Ico. *ICO* is a trademark of its owners. No game data is in this
> repository or in the downloads: you supply your own disc image of the PAL
> release (SCES-50760), and the port reads the game's assets from it. See
> [`docs/LEGAL.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/LEGAL.md).

## What you need

- Your own image of the **PAL** disc, SCES-50760, as a plain `.iso` file.
  Other regions and editions are not supported.
- A 64-bit PC with a Vulkan driver (current NVIDIA, AMD or Intel drivers on
  Windows 10/11 and Linux; Mesa RADV on the Steam Deck).
- About 1 GB of free disk space for the game's extracted data.

## Install

**Windows:** unzip the release anywhere you can write to, keeping the files
together, and double-click `x64\ico_pc_x64.exe`.

**Linux:** unpack the `.tar.gz` somewhere writable (not `/usr`) and run
`./ico_pc`. Keep `ico_pc` and `libSDL3.so.0` together. The package needs
glibc 2.38 or later (SteamOS 3.5+, Debian 13, Ubuntu 24.04).

**Steam Deck:** in Desktop Mode unpack the Linux package, then Steam >
Add a Non-Steam Game > browse to `ico_pc`, with no launch options. Do the
first launch in Desktop Mode (the disc image dialog needs `zenity`, or put
the image beside `ico_pc` as `Ico_PAL.iso`), then play in Game Mode with the
default gamepad layout. The Deck's controls work with no setup. In Game Mode
the game is always shown full screen, whatever the Fullscreen row says.

There are no command-line options to learn: double-click or run it.

## First run

The first run needs your disc image once. It looks for it, in order:

1. `iso=` in `ico-pc.ini` beside the program;
2. a file named `Ico_PAL.iso` beside the program;
3. otherwise a file dialog opens and asks for it, and remembers the choice in
   `ico-pc.ini`. (On Linux the dialog needs `zenity`.)

It then checks the image, shows a small progress window and extracts the
game's data once (about 870 MB, into `ico.o2r`). Later runs use that file and
never open the `.iso`, so you can move or delete the image afterwards.

## Controls

The keyboard and a gamepad both work at the same time. Gamepad buttons are
mapped by position, so an Xbox, PlayStation or Switch pad all feel the same.

| PS2 button | gamepad | keyboard | mouse |
| --- | --- | --- | --- |
| Cross (confirm, jump, call Yorda) | south (A) | Space | left button |
| Circle | east | E | right button |
| Square | west | Q | |
| Triangle (back) | north | R | |
| L1 / R1 | shoulders | Tab or ` / F | middle button (R1) |
| L2 / R2 | triggers | Z / X | |
| L3 / R3 | stick clicks | V / B | |
| Start / Select | start / back | Enter / Backspace | |
| D-pad (menus) | d-pad | arrow keys | |
| Left stick (move) | left stick | W A S D (hold Left Shift to walk) | |
| Right stick (look) | right stick | I J K L | mouse movement |

Escape closes the game. Alt+Enter toggles fullscreen. Everything else can be
rebound in Options > Controls > Remap controls.

## Options

Open **Options** from the title screen, or press Start during play and
choose **Options** there: both open the same menu. Up and Down move,
Left and Right change a value, Cross opens or confirms, Triangle or Circle
goes back. Changes apply at once and are saved to `config.toml`. Every
Display option applies on its own.

The PS2 Options screen's settings are on these pages too, shown only when
Options is opened from the pause menu: Brightness (Display; Square puts back
the default, 7); Button
configuration, Vibration and Hold type (Controls); Film effect and Players
(Gameplay, once you have finished the game). They are kept in your save, as
on the PS2, not in `config.toml`.

- **Display:** a Preset, which is a shortcut: Original puts the four options
  below back to the PS2 picture (1x, 4:3, original filtering, half height);
  Enhanced sets the window's size, Auto aspect, anisotropic filtering and the
  full-height picture; the row reads Custom once you change any of them.
  Then resolution scale (1x to 4x or the window's size; fixed at 1x while the
  CRT filter is on), aspect ratio (4:3, 16:10, 16:9, Auto), fullscreen (shows
  what the window is; in Steam Deck Game Mode the game is always shown full
  screen, so this row matters in Desktop Mode and on desktops), vertical sync
  (off never tears unless your desktop or the Deck's "Allow Tearing" permits
  it), texture filtering, full-height picture, frame rate (original, uncapped
  or a fixed cap up to 240 fps), a CRT filter with several tube styles and a
  strength, and the video mode (PAL 50 Hz or 60 Hz; the default is 60 Hz).
  The video mode can only be changed when Options is opened from the title.
- **Audio:** master, music and effects volume, stereo or mono, and the output
  device.
- **Controls:** remapping, mouse sensitivity, "Circle goes back" (Circle backs
  out of menus like Triangle).
- **Gameplay:** "Shadows never take Yorda" (a gentler game; a few scripted
  scenes still show the capture) and an analogue stick fix that makes
  diagonals as strong as straight pushes. Both are off by default.
- **Language:** English, French, German, Italian, Spanish. Defaults to your
  system language when the game has it.
- **Achievements:** the list of achievements with their descriptions. Hidden
  ones show as ??? until unlocked. They are suspended while developer mode is
  on.
- **Extras** (from the title screen): a music and sound gallery, a model
  viewer (turn, zoom and play the animations of the characters and a few
  objects) and Credits, which replays the ending roll and unlocks once you
  have finished the game.
- **Photo mode:** a row in the pause menu during play. The game stays paused
  and you move a free camera around the scene. Left stick orbits, right stick
  dollies and pans, L1/R1 roll, R2 and L2 zoom, Square hides the help lines,
  Cross saves a PNG to the `screenshots` folder in your user folder,
  Triangle, Circle or Start leaves.
- **Developer mode:** restores the development build's debug menu. Leave it
  off for normal play.

**Mirror mode** is not in Options: when you start a New Game, a screen after
"Vibration" offers to play the whole game flipped left to right. The choice
belongs to that save.

## Saves

The port stores the game's memory card as a normal folder of files, in the
same layout the PS2 and PCSX2 use, so saves are interchangeable with a PS2
card. The folder is `memcard\` (Windows) or `memcard/` (Linux) in your user
folder:

- Windows: `%APPDATA%\ico-pc\ico-pc\`
- Linux: `~/.local/share/ico-pc/ico-pc/`

The same folder holds `config.toml` (all settings, which you can edit while
the game is closed), `ico.o2r` (the extracted data) and the achievements.
Back up your saves by copying `memcard`. Uninstall by deleting the program
folder and this folder.

**Importing a PS2 save.** The package includes `tools\mc_import.exe`
(`tools/mc_import` on Linux). It copies ICO's save out of a PS2 memory card
image (`.ps2`, `.bin`) or a `.psu` file into the saves folder:

```
mc_import --to <your memcard folder> FILE
```

Only ICO's save is taken; other games on the card are left alone, and
existing files are refused unless you add `--overwrite`. `.max` and `.cbs`
files are not supported: convert them to `.psu` first. A PCSX2 folder card's
`BESCES-50760ico` directory can simply be copied into `memcard`. The importer
was written from the public description of the card format and has not been
tried on a real card image yet, so check that the game lists the save.

## Logs and problems

Each run writes `logs\ico-pc.log` beside the program (replaced on the next
run). If something goes wrong it says why:

- `rhi_vk:` lines: no usable Vulkan driver, or a missing feature.
- A `CRASH:` block: send it with the `.map` file from the download.
- A `WATCHDOG:` block: the game stopped making progress for 30 seconds.

Press **F12** with a problem on screen to save a frame dump and picture (a
`dumps` folder) for a bug report. Dumps contain pictures from your disc, so
do not share them publicly.

## Reporting a problem

Send `logs/ico-pc.log`, which sits beside `ico_pc` on Linux and beside the
exe on Windows. It records your display options at start, every change you
make in Options, and every 10 seconds the frame rate, present mode, refresh
rate and window size. Say where your `config.toml` is if asked; it is in the
user folder described under Saves.

## Differences from the PS2 you will notice

- An **Options** line on the title screen, the same menu as the pause
  menu's **Options**, and a **Quit to desktop** line on the title screen.
  The PS2's own Options screen's settings are pages of that menu.
- The language and 50/60 Hz questions the PS2 asked at first start are
  skipped; both come from Options.
- New Game has one extra **Mirror mode** screen after Vibration.
- The language can be switched mid-game, where the PS2 only chose it at
  boot: the menus change at once, the game's own text and subtitles at the
  next area load.
- The staff roll runs about four seconds longer, for a line of port credit.
- Saves are host files, not a memory card image, and port 1 can optionally be
  a second card (`[paths] saves2` in `config.toml`).
- The game's logic is otherwise the PS2's; the port aims for the same timing
  and results, and where rendering differs from the console it is a bug worth
  reporting.

## Building from source

See [`docs/BUILDING.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/BUILDING.md). The build needs no disc image.

## Legal and licence

The code in this repository is MIT licensed ([`LICENSE`](https://github.com/nathanialf/ico-pc/blob/main/LICENSE)). The
licence covers the code written for this project and grants no rights in the
game, its data or anything else owned by Sony Interactive Entertainment or
Team Ico. [`docs/LEGAL.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/LEGAL.md) says what may and may not be in
the repository, and [`docs/THIRD_PARTY.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/THIRD_PARTY.md) lists the
third-party code the program uses. The game code began as a fork of the
[ICO decompilation](https://github.com/nathanialf/ico).
