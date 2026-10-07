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

## Frequently asked questions

**Ico walks instead of running when I push the stick all the way, or keeps
switching between walking and running. Why can't he run?**
That is how the original game behaves. It only treats the eight stick
directions (up, down, left, right and the four diagonals) as a full push,
so a push anywhere in between never reaches running speed. The port keeps
the original behaviour by default. Turn on **Options > Gameplay > Analogue
stick fix** to make every direction count as a full push. (Reported in
[issue 5, "Controller Stick Issue"](https://github.com/nathanialf/ico-pc/issues/5)
and [issue 6, "Ico can't run"](https://github.com/nathanialf/ico-pc/issues/6).)

**How do I use a texture pack?**

1. Copy the pack's `SCES-50760` folder into a folder named `textures`
   beside the program (on Windows, inside the `x64` folder), or into
   `textures` in your user folder (`%APPDATA%\ico-pc\ico-pc\textures` on
   Windows, `~/.local/share/ico-pc/ico-pc/textures` on Linux).
2. Start the game.
3. Check that **Options > Display > Texture pack** reads **On**.
   `logs/ico-pc.log` says how many textures it found.

Packs made for PCSX2 work as they are, with nothing to rename or convert;
Sad Origami's PAL pack is the one tested. The details are under
[Texture packs](#texture-packs).

## What you need

- Your own image of the **PAL** disc, SCES-50760, as a plain `.iso` file
  or as a `.chd` (the compressed image PCSX2 and other emulators read).
  Other regions and editions are not supported.
- A 64-bit PC with a graphics driver that supports Vulkan 1.2: current
  NVIDIA, AMD or Intel drivers on Windows 10/11 and Linux, and the Steam
  Deck's own driver.
- About 1 GB of free disk space for the game's data, which the first run
  copies out of the disc image.

## Install

**Windows:** unzip the release anywhere you can write to, keeping the files
together, and double-click `x64\ico_pc_x64.exe`. On Windows, "beside the
program" in this README means inside that `x64` folder.

**Linux:** unpack the `.tar.gz` somewhere writable (not `/usr`) and run
`./ico_pc`. Keep `ico_pc` and `libSDL3.so.0` together. The package needs
glibc 2.38 or later (SteamOS 3.5+, Debian 13, Ubuntu 24.04).

**Steam Deck:** in Desktop Mode unpack the Linux package, then Steam >
Add a Non-Steam Game > browse to `ico_pc`, with no launch options. Do the
first launch in Desktop Mode (the disc image dialog needs `zenity`, or put
the image beside `ico_pc` as `Ico_PAL.iso` or `Ico_PAL.chd`), then play in
Game Mode with the default gamepad layout. The Deck's controls work with no
setup. In Game Mode the game is always shown full screen, whatever the
Fullscreen row says.

There are no command-line options to learn: double-click or run it.

## First run

The first run needs your disc image once. It looks for it in this order:

1. a file named `Ico_PAL.iso` or `Ico_PAL.chd` beside the program (the
   `.iso` first when both are there);
2. the `iso=` line in `ico-pc.ini` beside the program, which may name a
   `.iso` or a `.chd` anywhere;
3. otherwise a file dialog opens and asks for it, and remembers the choice
   in `ico-pc.ini`. (On Linux the dialog needs `zenity`.)

It then checks that the image is the PAL disc, shows a small progress window
and copies the game's data out of it once (about 870 MB, into a file named
`ico.o2r` in your user folder, see [Saves](#saves)). A `.chd` gives the same
data as the `.iso` it was made from. Later runs use that file and never
open the disc image, so you can move or delete the image afterwards.

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
the default, 7); Button configuration, Vibration and Hold type (Controls);
Film effect and Players (Gameplay, on a New Game+ journey). They are kept
in your save, as on the PS2, not in `config.toml`.

- **Display:** a Preset, which is a shortcut: Original puts the four options
  below back to the PS2 picture (1x, 4:3, original filtering, half height);
  Enhanced sets the window's size, Auto aspect, anisotropic filtering and the
  full-height picture; the row reads Custom once you change any of them.
  Then resolution scale (1x to 4x or the window's size; fixed at 1x while the
  CRT filter is on), aspect ratio (4:3, 16:10, 16:9, 21:9, 32:9, or Auto,
  which follows the window's shape up to 32:9), fullscreen (in Steam Deck
  Game Mode the game is always shown full screen, so this row matters in
  Desktop Mode and on desktops), vertical sync (off never tears unless your
  desktop or the Deck's "Allow Tearing" permits it), texture filtering,
  Texture pack (see [Texture packs](#texture-packs)), full-height picture,
  frame rate (original, uncapped or a fixed cap up to 240 fps), a CRT filter
  with several tube styles and a strength, and the video mode (PAL 50 Hz or
  60 Hz; the default is 60 Hz). The video mode row is shown only when
  Options is opened from the title.
  On a wider screen you see more of the world to the sides, while the
  menus, subtitles and movies stay in a 4:3 box in the middle. At the far
  sides of a very wide picture, some things can appear a moment late, since
  the game only expected a 4:3 view.
- **Audio:** master, music and effects volume, stereo or mono, and the output
  device.
- **Controls:** remapping, mouse sensitivity, "Circle goes back" (Circle backs
  out of menus like Triangle).
- **Gameplay:** "Shadows never take Yorda" (a gentler game; a few scripted
  scenes still show the capture) and "Analogue stick fix", which makes every
  stick direction count as a full push, so Ico runs whichever way you push
  (see the [questions](#frequently-asked-questions) above). Both are off by
  default.
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
  and you move a free camera around. Left stick orbits, right stick
  moves the camera in and out and to the sides, L1/R1 roll, R2 and L2 zoom,
  Square hides the help lines, Cross saves a picture (PNG) to the
  `screenshots` folder in your user folder, Triangle, Circle or Start
  leaves.
- **Developer mode:** restores the debug menu the game's developers used.
  Leave it off for normal play.

**The pause menu** also shows your journey's numbers in a panel on the
right: play time, deaths, how often Yorda was captured, saves and enemies
defeated on this journey, whether New Game+ and Mirror mode are on, and how
many achievements you have. Any assists that are on (Shadows never take
Yorda, the stick fix, Developer mode) are listed under Assists, and in the
castle's early areas the panel names the area you are in.

## Starting a New Game: Mirror mode and New Game+

**Mirror mode** and **New Game+** are not in Options: when you start a New
Game, the New Game screen after "Vibration" has a row for each, Off or On.
Up and Down move between the rows, Left and Right pick, Cross starts.

- **Mirror mode** plays the whole game flipped left to right.
- **New Game+** plays the second journey, the one the PS2 gives you after
  finishing the game: Yorda's words are translated, and the ending and some
  items change. It starts On when you begin from a finished game's save and
  Off otherwise, and you can change it either way. Off plays the first
  journey even after you have finished the game.

Two things follow from the New Game+ choice. On, in a game you have not
finished, the ending does not offer to save a finished game, and finishing
counts for the "Once More" achievement. Off, after finishing, Film effect
and Players are not in Options for that journey. Both choices belong to
that save.

## Texture packs

The port loads texture packs made for the PCSX2 emulator, as they are:
the same folders and file names, PNG or DDS files, nothing to convert. The
pack tested with this release is Sad Origami's ICO PAL HD pack (see
[Special thanks](#special-thanks)).

**Installing.** A PCSX2 pack has a folder named `SCES-50760` (the PAL
disc's number) with a `replacements` folder inside. Copy that `SCES-50760`
folder into a folder named `textures`, either beside the program or in
your user folder (see [Saves](#saves)):

- beside the program: `textures/SCES-50760/replacements/...` next to
  `ico_pc` (on Windows, inside the `x64` folder);
- or in your user folder: `%APPDATA%\ico-pc\ico-pc\textures\SCES-50760\...`
  on Windows, `~/.local/share/ico-pc/ico-pc/textures/SCES-50760/...` on
  Linux.

A pack copied without its `SCES-50760` folder (`textures/replacements`) is
found too. Start the game: `logs/ico-pc.log` has a line such as
`textures: 883 replacements from ...` for each folder it read, or
`textures: no texture pack (looked in ...)` naming where it looked.

**Turning it off.** **Options > Display > Texture pack** switches the pack
On or Off at once. It reads **None installed** when no pack was found.
Textures the pack does not cover stay as the game draws them.

**Graphics card.** Sad Origami's pack, like other PCSX2 packs, keeps most
of its pictures in a compressed form (DDS files with "BC" compression) that
the graphics card unpacks. Every desktop graphics card and the Steam Deck can do this. If
a card cannot, the log says so and those files are skipped.

**Memory.** Two settings in `config.toml` (in your user folder, under
`[video]`; edit it while the game is closed) control how much a pack may
use:

- `texture_pack_budget_mb` is how much memory, in MB, the pack may take
  (2048 by default). Textures past that limit stay the game's own, and the
  log says when the limit was reached. Raise it if your graphics card has
  more memory and the log shows the limit.
- `texture_pack_precache` (`true` by default) reads the whole pack into
  memory in the background from the start, as the pack's author
  recommends: the game's subtitles are separate pictures, and without this
  the original subtitle can flash up for a moment before the pack's. Set it
  to `false` to read each texture only when the game first shows it, which
  uses less memory.

**For pack makers.** With Developer mode on, a **Dump textures** row
appears under it in Options. While it is On, each texture is saved as a
PNG when the game loads it, under the name PCSX2 gives it, in
`textures/SCES-50760/dumps` in your user folder, so you can check that a
replacement will be picked up. Achievements are suspended while Developer
mode is on.

## Saves

The port keeps the game's memory card as a normal folder of files, laid out
the way PCSX2 keeps a memory card as a folder, so saves move between the
two. The folder is `memcard` in your user folder:

- Windows: `%APPDATA%\ico-pc\ico-pc\`
- Linux: `~/.local/share/ico-pc/ico-pc/`

The same user folder holds `config.toml` (all settings, which you can edit
while the game is closed), `ico.o2r` (the game's data, copied from your
disc image on the first run), the achievements, your screenshots and any
texture pack you put there. Back up your saves by copying `memcard`.
Uninstall by deleting the program folder and the user folder.

**Portable mode.** To keep everything beside the program instead, make a
folder named `userdata` next to the program, or remove the `#` from the
`# portable=1` line in `ico-pc.ini`. The program then keeps `memcard`,
`config.toml`, `ico.o2r`, the achievements and the screenshots in
`userdata`, and `logs/ico-pc.log` says so in a line that ends in
"(portable)". To move an existing install, close the game and copy
everything from the user folder above into `userdata` before the next
start; the program does not copy it for you. Put `portable=0` in
`ico-pc.ini` to use the user folder even when `userdata` exists.

**Importing a PS2 save.** The package includes `tools\mc_import.exe`
(`tools/mc_import` on Linux). It copies ICO's save out of a PS2 memory card
image (`.ps2`, `.bin`) or a `.psu` file into the saves folder. Run it from a
command prompt or terminal:

```
mc_import --to <your memcard folder> FILE
```

Only ICO's save is taken; other games on the card are left alone, and
existing files are refused unless you add `--overwrite`. `.max` and `.cbs`
files are not supported: convert them to `.psu` first. From a PCSX2 memory
card kept as a folder, the `BESCES-50760ico` folder can simply be copied
into `memcard`. The importer was written from the public description of the
card format and has not been tried on a real card image yet, so check that
the game lists the save.

## Logs and problems

Each run writes `logs/ico-pc.log` beside the program (replaced on the next
run). It records your display options at start, every Display change you
make in Options, and every 10 seconds the frame rate, refresh rate and
window size. If something goes wrong it says why:

- If the game window does not open, the lines that mention Vulkan (the way
  the game talks to your graphics driver) say what is missing. Updating the
  graphics driver usually fixes it.
- If the game closes with an error, the log has a block that starts with
  `CRASH:`. Send it with the `ico_pc.map` file from the download.
- If the game stops responding (30 seconds while starting, 60 seconds once
  it is running), the program closes it and the log has a block that starts
  with `WATCHDOG:` saying what it was doing.
- If the disc image cannot be read, a line naming the image says why. A
  `.chd` must be complete on its own: one made as a difference from another
  `.chd` is refused, so use the full `.chd` or the `.iso`.
- If a texture pack file cannot be used, a line starting with `textures:`
  names the file, and the game shows its own texture there.

Send that log with a problem report on the
[issues page](https://github.com/nathanialf/ico-pc/issues), and
`config.toml` from the user folder described under Saves if asked. Press
**F12** with a problem on screen to save a copy of that frame and a picture
of it (in a `dumps` folder in your user folder) for a bug report. They
contain pictures from your disc, so do not share them publicly.

## Differences from the PS2 you will notice

- An **Options** line on the title screen, the same menu as the pause
  menu's **Options**, and a **Quit to desktop** line on the title screen.
  The PS2's own Options screen's settings are pages of that menu.
- The language and 50/60 Hz questions the PS2 asked at first start are
  skipped; both come from Options.
- New Game has an extra screen after Vibration with two rows,
  **Mirror mode** and **New Game+**.
- The language can be switched mid-game, where the PS2 only chose it at
  boot: the menus change at once, the game's own text and subtitles at the
  next area load.
- The staff roll runs about four seconds longer, for a line of port credit.
- Saves are ordinary files, not a memory card image, and a second memory
  card (the PS2's second slot) can be added with `saves2` under `[paths]`
  in `config.toml`.
- The pause menu shows your journey's numbers on the right.
- The game's rules are otherwise the PS2's; the port aims for the same
  timing and results, and where the picture differs from the console it is
  a bug worth reporting.

## Building from source

See [`docs/BUILDING.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/BUILDING.md). The build needs no disc image.

## Special thanks

- Sad Origami, for the ICO PAL HD texture pack and for letting the port
  carry it
  ([GBAtemp thread](https://gbatemp.net/threads/ps2-ico-pal-sces-50760-in-progress.671638/)).

## Legal and licence

The code in this repository is MIT licensed ([`LICENSE`](https://github.com/nathanialf/ico-pc/blob/main/LICENSE)). The
licence covers the code written for this project and grants no rights in the
game, its data or anything else owned by Sony Interactive Entertainment or
Team Ico. [`docs/LEGAL.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/LEGAL.md) says what may and may not be in
the repository, and [`docs/THIRD_PARTY.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/THIRD_PARTY.md) lists the
third-party code the program uses. The game code began as a fork of the
[ICO decompilation](https://github.com/nathanialf/ico) project.
