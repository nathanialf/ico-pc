# When something goes wrong

[Back to the front page](../README.md)

## The log file

Every time the game runs, it writes what it is doing into a text file
named `ico-pc.log`, in a `logs` folder beside the program. On Windows that
is `x64\logs\ico-pc.log`. On Android it is in the game's folder
([where that is](ANDROID.md#where-your-files-are)). Each run starts the
file again and keeps the one from the run before as `ico-pc-previous.log`
in the same folder, so the log of a run that went wrong is still there
after you start the game once more.

The log records your display settings, every change you make to them, and
every 10 seconds how fast the game is running (every second for 30 seconds
after you press F11). When something goes wrong, it says what.

## The screen stays black

If the screen stays black but you can hear the game, the game is running
and only the picture is missing. See
[The screen stays black but I hear the game](FAQ.md#the-screen-stays-black-but-i-hear-the-game):
updating the graphics driver usually fixes it.

## Send a problem report

1. Open a report on the
   [issues page](https://github.com/nathanialf/ico-pc/issues).
2. Say what happened and what you were doing.
3. Attach `ico-pc.log`.
4. If you are asked for it, also attach `config.toml` from the folder with
   your saves ([where that is](PORTABLE_MODE.md#where-your-saves-are)).

## Save a picture of a problem

If something looks wrong on screen, press **F12** while it is showing. The
game saves three files in a `dumps` folder in the folder with your saves
([where that is](PORTABLE_MODE.md#where-your-saves-are)), all named
`frame-` and the date and time:

- a `.zip` file with a file that lets the developers see exactly how that
  moment was drawn, and a copy of the log (`ico-pc.log`);
- a `.rddump` file, the same drawing file on its own;
- a `.png` picture of that moment.

Attach the `.zip` to your report on the issues page. GitHub does not accept
`.rddump` files, which is why the `.zip` exists. The picture is optional,
and can be very large when the Resolution is high.

The files contain pictures from your disc, so use them only for a bug
report, and do not share them anywhere else.

## Error messages

The game shows a message box when it cannot go on. Here is what the
common ones mean.

**"No ICO disc image was found or chosen."** The game needs your disc
image the first time. Start it again and choose the file, or put the image
beside the program named `Ico_PAL.iso`.

**"... is not the disc image this port needs."** The file is not the PAL
version of ICO (SCES-50760), or it is damaged or changed. Other regions
and editions do not work.

**"Cannot read the disc image ..."** The file is incomplete or not a disc
image. A `.chd` must be complete on its own: one made as a difference from
another `.chd` does not work, so use the full `.chd`, the `.iso` or the
`.bin`. A `.cue` file with several files listed in it does not work either:
choose the `.bin` of the data track.

**"Could not extract the game's data ..."** There is not enough free space
(about 1 GB is needed), or the disc image is incomplete. Free some space
and try again.

**"Could not open the game window."** (on a PC) or **"Could not start the
game's graphics."** (on a phone) Your graphics driver or graphics chip does
not support Vulkan 1.2, the way the game talks to the graphics. Updating
the graphics driver usually fixes it. On a phone, install the latest
system update. The lines in the log that mention Vulkan say what is
missing.

**"ICO could not start the game's graphics with this phone's driver."**
(on a phone) The phone's own graphics driver cannot run the game. The box
offers **Choose a driver file**: on a phone with an Adreno (Qualcomm
Snapdragon) chip, pick a graphics driver package, a zip file such as
Turnip from the "AdrenoToolsDrivers" releases by K11MCH1 on GitHub. The
game adds it and asks you to close it and open it again: the driver is
used from the next start. If the file is not a driver package, the box
comes back so you can pick another one. If the driver does not start at
the next start either, the game says so and the box comes back.
**Quit** closes the game. Once the game runs, you can change the driver
later in Options > Graphics driver. See
[Using a different graphics driver](ANDROID.md#using-a-different-graphics-driver).

**"No disc image was chosen."** (on a phone) You left the file picker
without choosing a file. Start the game again and choose your ICO disc
image.

## The game closes by itself

If the game closes with an error, the log ends with a block that starts
with `CRASH:`. Send the log, and with the Windows or Linux version also
the `ico_pc.map` file from the download (`x64\ico_pc_x64.map` on
Windows). It helps the developers find where it happened.

If the game stops responding, it closes itself after a while: 30 seconds
while it is starting, 60 seconds once it is running. The log then has a
block that starts with `WATCHDOG:` and says what the game was doing. With
[ReShade](RESHADE.md) the game waits longer, because ReShade can take a
long time to get ready.

## Texture packs and model packs

If a texture or model from a pack does not show up, look in the log for
lines that start with `textures:` or `models:`. They say how many files
were found and why a file was not used.
[Texture packs](TEXTURE_PACKS.md#check-that-the-game-found-it),
[model packs](MODEL_PACKS.md#when-a-replacement-is-not-used).

## Known limits

On a few graphics drivers (rare), surfaces that sit very close together
may flicker.

## Linux

The Linux version needs a fairly new system: SteamOS 3.5, Debian 13,
Ubuntu 24.04 or newer. Keep `ico_pc` and `libSDL3.so.0` in the same
folder.

The window that asks for your disc image needs a small program named
`zenity`, which most desktops have. Without it, put the image beside
`ico_pc` named `Ico_PAL.iso` (or `Ico_PAL.chd`, `Ico_PAL.bin` or `Ico_PAL.cue`) instead.

## Steam Deck

1. In Desktop Mode, unpack the Linux download into a folder in your home
   folder.
2. Start `ico_pc` once in Desktop Mode, so it can ask for your disc image
   and set itself up. Or put the image beside `ico_pc` named
   `Ico_PAL.iso` and skip the question.
3. In Steam, choose **Add a Non-Steam Game**, browse to `ico_pc`, and add
   it. It needs no launch options.
4. Play it in Game Mode with the normal controller layout. The Deck's
   buttons work with no setup.

In Game Mode, the game always fills the screen, whatever the Window mode
row in Options says.

With Vertical sync off, the picture only tears if the Deck's "Allow
Tearing" setting is on.

## Android

If the fog looks wrong on your phone, for example if it covers the whole
picture, the log has lines that begin `rd: fog:`. They say which way the
game took to draw the fog and what it found, so attach the log to your
report.
