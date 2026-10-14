# When something goes wrong

[Back to the front page](../README.md)

## The log file

Every time the game runs, it writes what it is doing into a text file
named `ico-pc.log`, in a `logs` folder. Where that folder is:

- Windows: beside the program, so the file is `x64\logs\ico-pc.log`.
- Linux and Steam Deck: beside `ico_pc`.
- Mac: in the game's folder, usually
  `~/Library/Application Support/ico-pc/ico-pc/logs`.
- iPhone: in ICO's Documents folder.
- Android: in the game's folder
  ([where that is](ANDROID.md#where-your-files-are)).

Each run starts the file again and keeps the one from the run before as
`ico-pc-previous.log` in the same folder, so the log of a run that went
wrong is still there after you start the game once more.

The first lines of the log say which version of the game you have. The
log also records your display settings, every change you make to them, and
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
3. Say which version of the game you have. A downloaded release shows it
   in the top-right corner of the Options menu's first page, and the log
   says it at the top.
4. Attach `ico-pc.log`.
5. If you are asked for it, also attach `config.toml` from the folder with
   your saves ([where that is](PORTABLE_MODE.md#where-your-saves-are)).

If you are asked for a timing file, close the game, open `config.toml` in a
text editor and add these two lines at the end:

```toml
[dev]
perf_log = true
```

If the file already has a `[dev]` line, add only the `perf_log = true` line
under it. On every system except the iPhone the game then also writes
`ico-pc-perf.csv` in the same `logs` folder as `ico-pc.log`, one line for
each picture it draws. Play until the problem
shows, close the game, and attach that file with the log. Then delete what
you added, so the game stops writing the file.

## Save a picture of a problem

If something looks wrong on screen, press **F12** while it is showing. The
game saves three files in a `dumps` folder in the folder with your saves
([where that is](PORTABLE_MODE.md#where-your-saves-are)), all named
`frame-` and the date and time:

- a `.zip` file with a file that lets the developers see exactly how that
  moment was drawn, and a copy of the log (`ico-pc.log`);
- a `.png` picture of the whole frame the game drew at that moment;
- a picture ending in `-present.png`: that moment as your window showed it.

Attach the `.zip` to your report on the issues page. The pictures are
optional, and can be very large when the Resolution is high.

The files contain pictures from your disc, so use them only for a bug
report, and do not share them anywhere else.

## Error messages

When the game finds a problem it cannot get past, it shows a message box
and closes, and the same text is in the log. Here is what the common ones
mean.

**"No ICO disc image was found or chosen."** The game needs your disc
image the first time. Start it again and choose the file, or put the image
beside the program named `Ico_PAL.iso` (on Windows inside the `x64`
folder; on a Mac, add a line `iso=` with the image's full path to
`ico-pc.ini` in the user folder instead,
[where that is](PORTABLE_MODE.md#where-your-saves-are)).

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

**"The game's data just written to ... cannot be read back."** The copy of
the game's files that the first start made is damaged. Delete the file the
message names and start the game again: it copies the files out of the
disc image once more.

**"Could not open the game window."** (on a computer) or **"Could not start the
game's graphics."** (on a phone) Your graphics driver or graphics chip does
not support Vulkan 1.2, the way the game talks to the graphics. Updating
the graphics driver usually fixes it. On a phone, install the latest
system update. The lines in the log that mention Vulkan say what is
missing. If you added `backend=d3d12` to `ico-pc.ini` on Windows, delete
that line and try again.

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

**The game closed and started again at the title screen.** The graphics
card stopped responding while you played. On Windows, Linux and Mac the
game then starts itself again with simpler blending, which some cards
handle better when pushed hard; if that card stops again, on Windows it
tries the other graphics driver type. It starts at the title screen, so
anything you had not saved is lost, as when you quit. Nothing is changed
in your settings: the next start is normal again. If it keeps happening,
set an overclocked card back to its stock clocks (see below).

**"The graphics card stopped responding ..."** The graphics driver was
reset, updated or crashed while you played, and the game has to close.
When the message also says the game restarted itself with simpler blending
and the card still stopped, the game already tried that. Your last save is
kept, and the log says why. The message stays until you close it. Start
the game again. This is usually a driver reset: if it keeps happening,
update the graphics driver and send the log. If the graphics card is
overclocked (its speed or memory raised above the maker's settings, in a
tool such as MSI Afterburner), set it back to its stock clocks first and
try again: a card pushed past what it can do steadily makes the driver
reset, even in a game that asks little of it. The log also lists what the
game was drawing when the card stopped.

If you report it, the developers may ask you to try a few lines in
`ico-pc.ini` (on Windows in the `x64` folder; open it with Notepad, add the
line at the end, start the game, and send the new log). Each one changes
one thing, so use only the ones you are asked for, and delete them again
afterwards:

- `blend=fixed` draws see-through effects the simpler way.
- `fog_path=copy` (or `inplace`, or `buffer`) picks another way of drawing
  the fog.
- `gpu_checkpoints=true` makes the log say more exactly what the graphics
  card was doing when it stopped. The game may run a little slower with it.
- `gpu_robust=true` makes the graphics card check every read and write the
  game asks for. The game may run a little slower with it.

## The game closes by itself

If the game closes with an error, Windows and Android show a message that
says so; on Linux, the Steam Deck, a Mac and an iPhone the game just
closes. Either way the log says why: near the end of the log there is a
line of `=` signs, and the line after it has `CRASH:` in it. Send the log.
The developers can find where it happened from the numbers in that block.

**The game closes or restarts itself right after it starts.** Some
graphics drivers crash while the game gets its graphics ready. On Windows,
Linux and Mac the game notices and starts itself again, up to three times,
each time trying a gentler way: first without the graphics it saved last
time, then (on Windows) with Direct3D 12, then getting the graphics ready
as you play instead of all at once. You need do nothing, and your settings
are left as they are. If it still cannot start, Windows shows a message
that the graphics driver crashed; on Linux and Mac the game closes and the
log says why. On an Android phone a message says the graphics driver
crashed and the game closes; on an iPhone the game closes. On every
system, when the game closed while it was getting its graphics ready (with
a message or without one), the next start gets the graphics ready as you
play instead of all at once, and the log of that start says where the one
before stopped. It keeps doing that until you install a new version of the
game, which tries getting the graphics ready at the start again. Updating
the graphics driver usually fixes this. On Windows you can also open
`ico-pc.ini` in the `x64` folder with Notepad and add the line
`backend=d3d12` at the end to always use Direct3D 12
([how](OPTIONS.md#choosing-the-renderer-on-windows)). If you report it,
attach the log: it shows every attempt.

If the game stops responding, it closes itself after a while: 30 seconds
while it is starting, 60 seconds once it is running. Windows and Android
show a message that says so. The log then has a line of `=` signs, and the
line after it has `WATCHDOG:` in it and says what the game was doing.
With [ReShade](RESHADE.md) loaded, the game gives itself 60 seconds to
start as well, as ReShade takes a while to get ready.

## Texture packs and model packs

If a texture or model from a pack does not show up, look in the log for
lines that start with `textures:` or `models:`. They say how many files
were found and why a file was not used.
[Texture packs](TEXTURE_PACKS.md#check-that-the-game-found-it),
[model packs](MODEL_PACKS.md#when-a-replacement-is-not-used).

## Linux

The Linux version needs a fairly new system: Debian 13, Ubuntu 24.04 or
newer. Keep `ico_pc` and `libSDL3.so.0` in the same folder.

The window that asks for your disc image comes from your desktop's own
file window, or from the small program `zenity`. If no window shows up,
put the image beside `ico_pc` named `Ico_PAL.iso` (or `Ico_PAL.chd`,
`Ico_PAL.bin` or `Ico_PAL.cue`) instead.

While the game runs, it asks your desktop not to lock the screen or put the
computer to sleep. If the computer still goes to sleep while you play with a
controller, send the log with your report (it says which display system the
game uses).

## Steam Deck

1. In Desktop Mode, unpack the Linux download into a folder in your home
   folder.
2. Start `ico_pc` once in Desktop Mode, so it can ask for your disc image
   and set itself up. Or put the image beside `ico_pc` named
   `Ico_PAL.iso` and skip the question.
3. Close Steam, start the game, and choose Options > Extras > Add to Steam on the
   title screen
   ([what it does](OPTIONS.md#extras)). Or add it by hand: in Steam, choose
   **Add a Non-Steam Game**, browse to `ico_pc`, and add it. It needs no
   launch options.
4. Play it in Game Mode with the normal controller layout. The Deck's
   buttons work with no setup.

In Game Mode, the Deck shows the game on the whole screen, whatever the
Window mode row in Options says.

With Vertical sync off, the picture only tears if the Deck's "Allow
Tearing" setting is on (in the Deck's Quick Access menu, under
Performance).

## Android

If the fog looks wrong on your phone, for example if it covers the whole
picture, attach the log to your report: the log says which way the game
drew the fog and what it found.
