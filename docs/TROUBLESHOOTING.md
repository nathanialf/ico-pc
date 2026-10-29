# When something goes wrong

[Back to the front page](../README.md)

## The log file

Every time the game runs, it writes what it is doing into a text file
named `ico-pc.log`, in a `logs` folder. Where that folder is:

- Windows: beside the program, so the file is `x64\logs\ico-pc.log`.
- Linux and Steam Deck: beside `ico_pc`, or for the AppImage, beside the
  AppImage file.
- Mac: in the game's folder, usually
  `~/Library/Application Support/ico-pc/ico-pc/logs`.
- iPhone: in ICO's Documents folder.
- Android: in the game's folder
  ([where that is](ANDROID.md#where-your-files-are)).

If the game cannot make the `logs` folder there (for example, an AppImage
kept in a folder you cannot write to), it uses the `logs` folder in your
user folder instead
([where that is](PORTABLE_MODE.md#where-your-saves-are)), and the log's
first lines say so.

Each run starts the file again and keeps the one from the run before as
`ico-pc-previous.log` in the same folder, so the log of a run that went
wrong is still there after you start the game once more.

When the game closes with an error, stops responding and closes itself,
loses the graphics card, or cannot start its graphics, it also writes a
report file in the same `logs` folder: its name starts with `report-` and
goes on with the date and time, ending in `.zip` (for example
`report-20261016-142301.zip`). It holds the log, the log of the run
before, your settings and a short summary, so it is the one file a
problem report needs. The message the game shows names it. The five
newest are kept. Whenever you report a problem, also say which disc you
play (the European, Japanese, Korean, North American or Asian disc, the August 2001 preview or the June 2001 magazine demo).

The first lines of the log say which version of the game you have and
which disc it is playing (SCES-50760 for the European disc, SCPS-11003 for
the Japanese disc, SCPS-56001 for the Korean disc, SCUS-97113 for the North
American disc, SCPS-55001 for the Asian disc, PROTO-20010806 for the August
2001 preview, SCUS-97158 for the June 2001 magazine demo). The log also records your display settings, every
change you make to them, and
every 10 seconds how fast the game is running (every second for 30 seconds
after you press F11). When something goes wrong, it says what.

## The first start takes a long time

The first start after you install the game or a new version of it says
**Starting ICO** and **Preparing graphics**, with a count and a bar: the
game is getting its graphics ready for your graphics card or phone. On a
computer this usually takes a few seconds. On a phone or a handheld it can
take a few minutes. After a few seconds the screen also says about how
much time is left, worked out from how fast the count has gone so far, so
it is only a rough guess. The game keeps working meanwhile: leave it open
until the count is done. On a phone the screen also says that this happens
once after an update: the first start after an update prepares the
graphics from nothing, because what was prepared before the update is
thrown away. The game keeps what it prepares now, and later starts skip this
screen or show it only for a moment. If you close the game while
it says this, the next start shows it again. If the game closes by itself
on this screen, see [The game closes by itself](#the-game-closes-by-itself).

If the game stops while it says **Preparing graphics** (it closes, or the
phone takes you back to its home screen), start it again. The game sees
that the last start stopped there and, for that one start, prepares the
graphics more carefully: in small steps, one at a time, writing down what
it has finished after each step, so nothing finished is prepared twice. If
the device runs short of memory meanwhile, the game stops preparing, goes
on to the title and prepares the rest the first time the game needs it.
The start after that tries the usual way again, because the stop may not
have been the graphics' fault (closing the game with Task Manager on a
computer looks the same to it). If the careful start stops there too, the
game skips this screen from then on and prepares each piece of graphics
the first time the game needs it, and writes that into `config.toml`. The
title then comes at once; the first minutes of play can pause for a moment
here and there while graphics are prepared, and the game keeps what it
prepared, so later starts pause less. On a phone, a stop while the game was
in the background (you switched to another app, or closed the game from
the recent apps) does not count. On the AYN Thor and the AYN Odin 2 Portal
the game prepares the graphics the careful way on every start; if a start
stops there, the next one skips this screen for that start, and a second
stop in a row makes that last. You can also tell the game yourself: close
it, open `config.toml` from the folder with your saves
([where that is](PORTABLE_MODE.md#where-your-saves-are)) in a text editor
and add these two lines at the end (if the file already has a `[dev]`
line, add only the second line under it):

```toml
[dev]
pipeline_set = "lazy"
```

With `pipeline_set = "careful"` instead the game keeps this screen but
always prepares the graphics the careful way.
To have the game prepare everything at the start again, delete that line.
If the game still stops, send the log of the start that stopped and the
log of the start after it: they say what was being prepared and how much
memory the game was using.

On a phone or a handheld the system tells the game when the device itself
runs low on memory. If it says so while the game prepares its graphics,
the game stops preparing, goes on to the title and prepares the rest the
first time the game needs it. The next start then skips **Preparing
graphics** and goes straight to the title; the first minutes of play can
pause for a moment here and there while graphics are prepared. Apps left
open in the background hold memory too: close them before you start the
game. A new version of the game tries preparing everything at the start
again. The log says how much memory the device had left each time the
system asked the game to free some.

If the whole phone or handheld freezes on **Preparing graphics** and you
have to restart it, restart the device and start the game again: the game
sees where the last start stopped and, for that start, prepares the
graphics the careful way, or, when it stopped on the 3D graphics, draws 3D
scenes another way that looks the same. If a later
start freezes at the same place again, the game keeps to that from then on
and writes it into `config.toml`. If it keeps freezing, send the log from
the start after the freeze: it lists how long each group of graphics took
to prepare before the freeze and how much memory the game was using.

If your `config.toml` has a line `vu_plain_only = true` or
`vu_extended = true`, the game ignores it: every device now prepares the
same 3D graphics. You can delete the line.

## The screen stays black

If the screen stays black but you can hear the game, the game is running
and only the picture is missing. See
[The screen stays black but I hear the game](FAQ.md#the-screen-stays-black-but-i-hear-the-game):
updating the graphics driver usually fixes it.

**A black screen that never changes.** If the picture freezes or stays
black and nothing changes, the graphics driver may have stopped
responding without saying so. Wait: the game closes by itself, after
about 15 seconds when it draws with Vulkan (always on a phone) and after
about a minute with the other Graphics API choices. Windows, Linux and Android show a message ("The graphics card stopped
responding while the game was drawing", on a phone "The graphics driver
stopped responding while the game was drawing"). Your last save is kept. Start the game
again: the game treats this like any other time the graphics stop (see
[The game closes by itself](#the-game-closes-by-itself) and the messages
under [Error messages](#error-messages)): it keeps a copy of the picture
it was drawing for the report, and on a phone the next start has the
graphics driver check every read for that start. If it happens again, report it and attach the
newest file in `logs` that starts with `report-` (the message names it),
and say which disc you play (the European, Japanese, Korean, North American or Asian disc, the August 2001 preview or the June 2001 magazine demo).

## The music stops

If a piece of music goes quiet while the sounds around it still play, or
never starts in a scene where it should, the log of that run has what is
needed to find out why: it writes down when each piece of music starts and
ends, and how loud it is playing at its start, half way and at its end.
Send `ico-pc.log` from that run (if you have started the game again since,
it is `ico-pc-previous.log`) with a [problem report](#send-a-problem-report),
and say roughly where in the game it happened and for how long the music
was missing. The log is worth sending even from a run where the music
played well: its last lines count how often a piece of music started while
the game had a lot of sound to send at once, which is when music can go
missing.

## Sound that crackles or cuts out

The game keeps its sound playing on its own while a scene loads or the
picture takes a moment, so the sound carries on through short pauses. When
the game itself stops for longer (a long load on a slow phone), the sound
pauses with it, so it never runs ahead of the game. If you still hear
crackling or gaps, send `ico-pc.log` from that run (if you
have started the game again since, it is `ico-pc-previous.log`) with a
[problem report](#send-a-problem-report): every ten seconds the log counts
the moments the sound could not keep up. A short click when you go from one
area to the next is the game stopping its sounds all at once, as it does on
the PlayStation 2; the log writes down how loud each one was.

## No sound on Linux

Open `ico-pc.log` (see [The log file](#the-log-file)) and look for the
lines that start with `audio:`. The first says which sound systems this
copy of the game can talk to and which one it picked, for example
`audio: SDL audio drivers built in: pipewire pulseaudio alsa disk dummy;
using pipewire`. If it says `using pipewire` or `using pulseaudio`, the game
is playing through your desktop's sound system, and the speakers it uses are
the ones your desktop has chosen. If the next line says `no sound server
found`, the game did not find PipeWire or PulseAudio running and is using
the sound card directly, which can be a different output than the one you
listen on.

Open **Options > Audio** and look at the **Output device** row: it lists
the outputs the game can see. Pick the one you listen on. The game talks to
PipeWire and PulseAudio directly. If the log has no line saying which sound
systems the game can use, you have a copy of the game that talks to the sound
card only, which gives silence on some desktops: download the newest version
from the front page. If you have no sound with the newest version, send the
`audio:` lines of `ico-pc.log` with a [problem report](#send-a-problem-report).

## The game is slow on an older graphics card

When the graphics card cannot draw every picture in time, the game keeps
its normal speed and leaves some pictures out, so it looks less smooth
instead of slowing down. This works at every frame rate except Original.
If the game stutters, or your graphics card is always at full load, these
settings lower the load the most (Options):

- **Frame rate** Original draws one picture per game step, as on the PS2,
  instead of also drawing the pictures in between: about half the work.
  It never leaves a picture out, so if the card still cannot keep up there,
  the game slows down a little instead.
- **Resolution** 1x is the PS2's own sharpness and the lightest of all.

On a Mac the game runs fast with anti-aliasing on or off: it draws with
the faster method by default, whatever the anti-aliasing setting.

If it is still slow, play for a minute or two with your usual settings, in
a place where it is slow, and send `ico-pc.log` with a
[problem report](#send-a-problem-report) before you start the game again
(the log of the run before is kept as `ico-pc-previous.log`; the
[first section](#the-log-file) says where the file is on each system).
Every 10 seconds the log writes a few lines with how long the graphics
card spent on the scene, the glow, the fog and the other effects, how many
drawing steps the game took for each picture and which steps took longest.
Those lines are what is needed, and the first lines of the log say which
graphics card and driver you have. If you changed the Resolution or the
other graphics settings to find what helps, say which ones: the log
records each change you make to them. If it is slow only at one place or
after one change, say where or which, and send one run that is slow and, if
you can, one at Resolution 1x for comparison.

## White lines along walls while the camera moves

While the camera turns or moves, white or light wedges and lines can show
along the edges where two pieces of a wall or a floor meet. The light
colour is the room's mist showing through a gap between the pieces. The
game draws extra pictures between its own steps to look smooth, which only
happens at a Frame rate of 60 or more. In those pictures every piece is
placed from the same viewpoint, so pieces that have just come into view or
crossed the edge of the screen line up with their neighbours. If you still
see such lines:

1. Set **Options > Display > Frame rate** to **Original** and turn the
   camera in the same place. If the lines are gone, they come from the
   pictures the game draws between its own steps; if they stay, they are in
   the game's own pictures too.
2. Set **Frame rate** back and play for a few minutes where the lines show.
   The log (`ico-pc.log`) then holds lines that say how many pictures had
   pieces placed from different moments.
3. On Windows, while the lines show, press **F12**
   ([Save a picture of a problem](#save-a-picture-of-a-problem)). The
   message the game writes in the log for it says which moment between two
   steps the picture shows, so the picture can be drawn again exactly.
4. Send a [problem report](#send-a-problem-report) with the newest `report-`
   file (or `ico-pc.log`), and on Windows the `.zip` F12 saved (it holds that
   moment and the moment before it) and the picture ending in
   `-present.png` (the lines as your screen showed them). Say which Frame
   rate showed the lines and where in the game you were. On a phone, a short
   screen recording of the lines helps.

## A program that adds frames or scales the game (Lossless Scaling) does not work

Programs that scale the game's window, add frames to it or draw over it,
such as Lossless Scaling and overlays, need the game in a window that
other programs can draw over:

1. Set **Options > Display > Window mode** to **Borderless**. The game
   still fills the screen, without a frame.
2. Start the program and pick the game's window.

**Windowed** works too. **Fullscreen** does not: it takes the whole screen
for the game, and those programs cannot work on that. If the program still
does not work, send a [problem report](#send-a-problem-report) with
`ico-pc.log` and say which program and which settings you use.

## Send a problem report

1. Open a report on the
   [issues page](https://github.com/nathanialf/ico-pc/issues).
2. Say what happened and what you were doing.
3. Say which version of the game you have, and whether you play the
   the European, Japanese, Korean, North American or Asian disc, the August 2001 preview or the June 2001 magazine demo. A downloaded release shows its version
   in the top-right corner of the Options menu's first page, and the log
   says both at the top.
4. Attach the newest file in `logs` that starts with `report-`, when the
   game wrote one for the problem (it already holds the log and your
   settings). Otherwise attach `ico-pc.log`. If the problem is in a
   picture, also attach the `.zip` that **F12** saves
   ([below](#save-a-picture-of-a-problem)).
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

- a `.zip` file with files that let the developers see exactly how that
  moment and the moment before it were drawn (and so the pictures the game
  draws between them), and a copy of the log (`ico-pc.log`);
- a `.png` picture of the whole frame the game drew at that moment;
- a picture ending in `-present.png`: that moment as your window showed it.

Without a keyboard (a phone, a handheld), choose **Save a picture for a
bug report** on the Options menu's first page (the row just above
Developer; you do not need Developer mode for it) and leave the menu
at once: 3 seconds later the game saves the same files. On Android the
`dumps` folder is in the game's folder
([where it is](ANDROID.md#where-your-files-are)).

Attach the `.zip` to your report on the issues page. The pictures are
optional, and can be very large when the Resolution is high; for something
that shows only while the picture moves (lines or flicker), attach the
`-present.png` too.

The files contain pictures from your disc, so use them only for a bug
report, and do not share them anywhere else.

**Puddles, ripples or water look broken.** First, two things are the
game's own and look the same on a PlayStation 2: the rings in the drained
Waterfall room have eight sides, and the puddles' edges follow the small
stencil picture the game paints them with, so at a high resolution they
show small steps. Sad Origami's texture pack carries a smoother version of
that stencil, so with **Texture pack** on the puddles' edges are smooth
([Texture packs](TEXTURE_PACKS.md)). For anything else, press **F12** while
it shows and attach the `.zip`. Then try these one at a time, each with the others as
they were, and say in the report which one changed the water: set
**Anti-aliasing** to **Off**, then (with your anti-aliasing back) set
**Texture pack** to **Off** ([Options](OPTIONS.md)). The log line the game
writes when you press F12 says what the water was drawn with, so press F12
once for each try.

## Error messages

When the game finds a problem it cannot get past, it shows a message box
and closes, and the same text is in the log. Here is what the common ones
mean.

**"No ICO disc image was found or chosen."** The game needs your disc
image the first time. Start it again and choose the file, or put the image
beside the program named `Ico_PAL.iso` for the European disc,
`Ico_JPN.iso` for the Japanese disc, `Ico_KOR.iso` for the Korean disc,
`Ico_USA.iso` for the North American disc, `Ico_CN.iso` for the Asian disc,
`Ico_AUG6.iso` for the August 2001 preview or `Ico_JUN26.iso` for the June 2001
magazine demo (on Windows inside the `x64`
folder; on a Mac, add a line `iso=` with the image's full path to
`ico-pc.ini` in the user folder instead,
[where that is](PORTABLE_MODE.md#where-your-saves-are)).

**"... is not a disc image this port plays."** The file is not one of the
seven discs (the European release of ICO, SCES-50760, the Japanese release
SCPS-11003, the Korean release SCPS-56001, the North American release
SCUS-97113, the Asian release SCPS-55001, the August 2001 preview disc and the
June 2001 magazine demo SCUS-97158), or it is damaged or changed. Other
editions do not work. For the magazine demo the file must be the **whole**
magazine disc image: a copy of only ICO's files is not recognised.

**"Cannot read the disc image ..."** The file is incomplete or not a disc
image. A `.chd` must be complete on its own: one made as a difference from
another `.chd` does not work, so use the full `.chd`, the `.iso` or the
`.bin`. A `.cue` file with several files listed in it does not work either:
choose the `.bin` of the data track. With a `.cue`, its `.bin` must be in
the same folder, under the name the `.cue` gives it.

**"Could not extract the game's data ..."** There is not enough free space
(about 1 GB is needed for each disc you set up; on an Android phone the
June 2001 magazine demo needs about 5.6 GB, because its 4.4 GB image is copied
into the app first), or the disc image is incomplete. Free some space and try again.

**"The game's data just written to ... cannot be read back."** The copy of
the game's files that the first start made is damaged. Delete the file the
message names and start the game again: it copies the files out of the
disc image once more.

**"Could not open the game window."** (on a computer) or **"Could not start the
game's graphics."** (on a phone) Your graphics driver or graphics chip does
not support Vulkan 1.1 or later, the way the game talks to the graphics. Updating
the graphics driver usually fixes it. On a phone, install the latest
system update. The lines in the log that mention Vulkan say what is
missing. If you added `backend=d3d12` to `ico-pc.ini` on Windows, delete
that line and try again.

**The game does not start: no Vulkan.** On a computer whose graphics card or driver has no
Vulkan, Automatic goes on with Direct3D 12 (Windows) and then OpenGL by itself. If it still
does not start, choose OpenGL yourself: on Windows add the line `backend=opengl` to `ico-pc.ini`
in the `x64` folder, beside the program's `.exe`; on any computer, if the game opens, set
Options > Graphics > Graphics API to OpenGL and start it again, or write `backend = "opengl"`
under `[video]` in `config.toml` ([more](OPTIONS.md#choosing-the-renderer)). OpenGL has only been checked on a software
renderer, so if it fails or looks wrong, send `logs/ico-pc.log` (the lines starting `window:`
and `rhi_gl:`) with your graphics card and driver.

**I chose Direct3D 11 and the game does not start.** Direct3D 11 has not been tried on any
Windows computer, so this can happen. Open `ico-pc.ini` in the `x64` folder with Notepad and
delete the line `backend=d3d11` (or change it to `backend=` with nothing after it, which is
Automatic), then start the game. If you can still reach Options > Graphics, pick Automatic
there. Please send `logs/ico-pc.log` (the lines starting `window:` and `rhi_d3d11:`) so we can
see what stopped it.

**"ICO could not start the game's graphics with this phone's driver."**
(on a phone) The phone's own graphics driver cannot run the game (the log
may say "the Vulkan loader is 1.0", or "device 0 (...) unsuitable" and the missing part). A driver package is loaded by the game
itself, without the phone's own Vulkan loader, so it can work where the
phone's driver does not. The box names the folder to copy the zip into if
the file picker does not open (`Android/data/com.defnf.icopc/files/drivers`).
The box
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

**"The file picker could not be opened."** (on a phone, after **Add a
driver**) The phone would not open its file picker. Copy the driver's zip
file into `Android/data/com.defnf.icopc/files/drivers` (the `drivers` folder
in the game's folder; the message names the path) with the USB cable, then
choose **Add from the drivers folder** in Options > Graphics driver (see
[Using a different graphics driver](ANDROID.md#using-a-different-graphics-driver)).
If you report it, send `ico-pc.log` from right after you pressed **Add a
driver**: the line that starts `gpu driver: the file picker` says what the
phone answered.

**"No app on this device can choose a file."** (on a phone) The phone has no
app that can offer a file to pick. Use the same way: copy the zip into the
`drivers` folder and choose **Add from the drivers folder**. The
`gpu driver: the file picker` line in `ico-pc.log` says what the phone
answered.

**"No file was chosen."** (on a phone, after **Add a driver**, or in the box
at the start) The file picker closed without a file: either you backed out,
or the phone's picker closes at once. The game names the folder to use
instead, `Android/data/com.defnf.icopc/files/drivers`: copy the zip into it
and choose **Add from the drivers folder** (at the start, the box adds the
first zip from that folder by itself). If the picker never answered at all,
the game stops waiting after the picker's screen has closed, or after two
minutes. The `gpu driver:` and `file picker:` lines in `ico-pc.log` say what
the phone answered.

**Driver note after the graphics stopped (Options > Graphics driver).** On a
phone with a Qualcomm Snapdragon chip, when the graphics stopped responding
on the phone's own driver, the note under the page names the Turnip driver
package route and the folder. A package is loaded by the game itself and may
not stop where the phone's own driver does. On a tablet, also set
**Vertical sync** on and **Frame rate** to 60 instead of Uncapped, and do not
use the highest Resolution.

**"No disc image was chosen."** (on a phone) You left the file picker
without choosing a file. Start the game again and choose your ICO disc
image.

**The game closed and started again at the title screen.** The graphics
card stopped responding while you played. On Windows, Linux and Mac the
game then starts itself again with safer graphics settings, one more each
time the card stops: first a simpler way of drawing 3D scenes that looks
the same, then extra checks on every read the graphics card makes, which
some cards need when pushed hard; if that card stops again, on Windows it
tries the other graphics driver type. It starts at the title screen, so
anything you had not saved is lost, as when you quit. Nothing is changed
in your settings: the next start is normal again. If it keeps happening,
set an overclocked card back to its stock clocks (see below).

**What the game keeps when the graphics stop.** Whenever the graphics card
or the phone's graphics driver stops responding, the game saves a copy of
the picture it was drawing in the `logs` folder, next to the log: a file
whose name starts with `loss-` and goes on with the date and time, ending
in `.zip`. The three newest are kept. Before the title, the game checks
the picture step by step at the next start and writes what it finds into
the log: first which part of the picture makes the graphics stop, then
which single drawing in it, a few tries at a time. While it does, the
screen says "Checking the graphics problem" and may stay dark a little
longer. If the graphics stop during the check, the next start
carries on where it left off (it gives up after a few starts). Then the
game starts as usual, whatever it found. If you report the problem, attach
the newest file in `logs` that starts with `report-` (it holds the `loss-`
file) together with the log of the start after it, and say which disc
you play (the European, Japanese, Korean, North American or Asian disc, the August 2001 preview or the June 2001 magazine demo).

**"The game checked why the graphics stopped last time, and the graphics
driver needs a fresh start now."** The check above stopped the graphics
driver again, as it was meant to find out, and the driver could not carry
on in the same start. Start the game again: the check carries on where it
left off if it was not finished, and the game then starts as usual.

**"The graphics driver stopped responding while the game was drawing ..."**
(on a phone) The phone's graphics driver failed while drawing a picture,
and the game has to close; your last save is kept. When the driver stops
without saying so, the game notices within about 15 seconds of a still
picture and shows this message. On a phone with a Qualcomm Snapdragon chip
that uses its own graphics driver, the message ends with the other route:
a Turnip driver package, chosen under **Options > Graphics driver > Add a
driver** and used from the next start (see the driver note above). Start the game again.
That start has the graphics driver check every read it makes, for that
start only, which can keep it from stopping (the game may run a little
slower). If the graphics driver stops again in that start or in the one
after it, the game keeps the checks on from then on and writes
`gpu_robust = true` into `config.toml`; delete that line to turn them off
again. If it happens every time at the same place, report it and attach
the newest file in `logs` that starts with `report-` (the message names
it; it shows what was being drawn) and say which disc you play, the
European or the Japanese.

**"The graphics card stopped responding ..."** The graphics driver was
reset, updated or crashed while you played, and the game has to close.
When the message also says the game restarted itself with safer graphics
settings and the card still stopped, the game already tried those. Your last save is
kept, and the log says why. The message stays until you close it. Start
the game again. This is usually a driver reset: if it keeps happening,
update the graphics driver and send the newest file in `logs` that starts
with `report-` (the message names it), saying which disc you play, the
European or the Japanese. If the graphics card is
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
  game asks for. The game may run a little slower with it. It is off unless
  you turn it on; on a phone the game turns it on by itself for one start
  after the graphics driver stopped responding (see above), and
  `gpu_robust=false` keeps it off.

## The game closes by itself

If the game closes with an error, Windows and Android show a message that
says so; on Linux, the Steam Deck, a Mac and an iPhone the game just
closes. Either way the log says why: near the end of the log there is a
line of `=` signs, and the line after it has `CRASH:` in it; a few lines
further on, the place in the game where it happened is named (on
Windows, Linux, the Steam Deck and Android). Send the
newest file in `logs` that starts with `report-`: it holds the log and
everything else a report needs. Say which disc you play, the European or
the Japanese.

**The game closes or restarts itself right after it starts.** Some graphics
drivers crash while the game gets its graphics ready. On Windows, Linux and
Mac the game notices and starts itself again, up to four times, each time
trying a gentler way: first without the graphics it saved last time, then
with simpler blending (when the crash was in the way the game draws
see-through effects), then (on Windows) with Direct3D 12, then getting the
graphics ready as you play instead of all at once. You need do nothing, and
your settings are left as they are. If it still cannot start, Windows shows
a message that the graphics driver crashed; on Linux and Mac the game
closes and the log says why. On an Android phone a message says the
graphics driver crashed and the game closes; on an iPhone the game closes.
On every system, when the game closed while it was getting its graphics
ready (with a message or without one), the next start gets the graphics
ready as you play instead of all at once, and the log of that start says
where the one before stopped. It keeps doing that until you install a new
version of the game, which tries getting the graphics ready at the start
again. To make the game try that again sooner, delete the file
`startup-activity.txt` in the `logs` folder. Updating the graphics driver usually fixes this. On Windows you can
also open `ico-pc.ini` in the `x64` folder with Notepad and add the line
`backend=d3d12` at the end to always use Direct3D 12
([how](OPTIONS.md#choosing-the-renderer)). If you report it,
attach the log: it shows every attempt.

If the game stops responding, it closes itself after a while: 30 seconds
while it is starting, 60 seconds once it is running. Windows, Linux and
Android show a message that says so. The log then has a line of `=` signs, and the
line after it has `WATCHDOG:` in it and says what the game was doing.
When the game was waiting for the graphics card, the next line says `the
graphics driver did not finish the picture`, and the message says the
graphics card (or the graphics driver) stopped responding: the game then
keeps a copy of the picture as it does whenever the graphics stop (see
[Error messages](#error-messages)).
Send the newest file in `logs` that starts with `report-`, and say which
disc you play (the European, Japanese, Korean, North American or Asian disc, the August 2001 preview or the June 2001 magazine demo).
With [ReShade](RESHADE.md) loaded, the game gives itself 60 seconds to
start as well, as ReShade takes a while to get ready.

**The game stopped after being left alone.** Sometimes Windows holds the
game's window for a while, for example when a controller turns itself off
or the computer would go idle. The game counts that time as Windows' and
waits; it only gives up when Windows has held the window for 10 minutes.
The game also keeps the screen and the computer awake while it runs, as a
console does: quit the game to let them sleep. If the game still stops
after being left alone, the log says which message from Windows held the
window: look for lines with `has not returned` or `the event pump's` in
them, and send the log.

If such a line says `the controllers' scan`, Windows held the game while
it looked for controllers, which can happen right after a controller is
plugged in, unplugged or turns itself off. To check whether that is the
cause, close the game, open `config.toml` from the folder with your saves
([where that is](PORTABLE_MODE.md#where-your-saves-are)) in a text editor
and add these two lines at the end (if the file already has a `[dev]`
line, add only the second line under it):

```toml
[dev]
joystick_directinput = false
```

The game then skips Windows' older way of finding controllers. Xbox and
PlayStation controllers and most others still work; some older wheels,
flight sticks and plain USB pads are found only that way and stop working.
Play as before and send the log either way: it says whether the long waits
stopped. If a controller of yours stops working, delete the line.

## Texture packs and model packs

If a texture or model from a pack does not show up, look in the log for
lines that start with `textures:` or `models:`. They say how many files
were found and why a file was not used.
[Texture packs](TEXTURE_PACKS.md#check-that-the-game-found-it),
[model packs](MODEL_PACKS.md#when-a-replacement-is-not-used).

## Linux

The Linux version needs a fairly new system: Debian 13, Ubuntu 24.04 or
newer. Keep `ico_pc` and `libSDL3.so.0` in the same folder.

If the AppImage does nothing when you double-click it, first check that it
may run as a program (right-click, **Properties**; see
[Install](../README.md#linux-and-steam-deck)). If it still does not start,
run it from a terminal with `--appimage-extract-and-run` after the file's
name: some systems lack a part the AppImage normally uses, and this way
works without it. Keep the AppImage in a folder you can write to, because
`ico-pc.ini` and the `logs` folder go next to it.

The window that asks for your disc image comes from your desktop's own
file window, or from the small program `zenity`. If no window shows up,
put the image beside `ico_pc` (or beside the AppImage), named `Ico_PAL`
(European disc), `Ico_JPN` (Japanese), `Ico_KOR` (Korean), `Ico_USA` (North
American), `Ico_CN` (Asian), `Ico_AUG6` (August 2001 preview) or `Ico_JUN26`
(June 2001 magazine demo) and ending in `.iso`, `.chd`, `.bin` or `.cue`,
instead.

While the game runs, it asks your desktop not to lock the screen or put the
computer to sleep. If the computer still goes to sleep while you play with a
controller, send the log with your report (it says which display system the
game uses).

## Steam Deck

1. In Desktop Mode, put the AppImage in a folder of its own in your home
   folder and let it run as a program
   ([how](../README.md#linux-and-steam-deck)), or unpack the
   `-linux.tar.gz` download into a folder there.
2. Start it (the AppImage, or `ico_pc`) once in Desktop Mode, so it can ask
   for your disc image and set itself up. Or put the image beside it named
   `Ico_PAL.iso` (European disc), `Ico_JPN.iso` (Japanese disc),
   `Ico_KOR.iso` (Korean disc), `Ico_USA.iso` (North American disc),
   `Ico_CN.iso` (Asian disc), `Ico_AUG6.iso` (August 2001 preview) or
   `Ico_JUN26.iso` (June 2001 magazine demo) and skip
   the question.
3. Close Steam, start the game, and choose Options > Extras > Add to Steam on the
   title screen
   ([what it does](OPTIONS.md#extras)). Or add it by hand: in Steam, choose
   **Add a Non-Steam Game**, browse to the AppImage (or `ico_pc`), and add
   it. It needs no launch options.
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

If see-through things look wrong, for example white boxes around leaves,
a washed-out title screen or menus you cannot read, attach the log to your
report. The game checks at start that it draws see-through things right
on your phone and uses a simpler way when it does not; the log says what
it found.

**The game stutters on the phone's own graphics driver.** On some Adreno
phones and tablets (a Xiaomi Pad 6S Pro, for one) the phone's own graphics
driver makes the game stutter, with long stops every few seconds, while a
newer driver from the community does not. Try a Turnip driver package:
**Options > Graphics driver**, **Add a driver**, then set **Driver** to it
and restart ([the steps](ANDROID.md#using-a-different-graphics-driver)).
On a tablet, a high **Resolution** with Vsync off and Frame rate
Uncapped asks the most of the driver: set **Vsync** On, or **Frame rate**
60, and lower the **Resolution** a step. If it still stutters, play for a
minute and send the newest `report-` zip from `logs` (and `ico-pc.log`)
with a [problem report](#send-a-problem-report).
