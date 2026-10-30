# Playing on Android

[Back to the front page](../README.md)

> [!NOTE]
> On a phone the game can be slower than on a PC. See
> [Known limits](#known-limits).

## What you need

- **A 64-bit Android phone or tablet with Android 8 or later.** Its
  graphics must support Vulkan 1.0 or later (the way the game talks to the
  graphics); most phones and tablets from 2017 on do. If you are not sure,
  install the game and start it: if your phone cannot run it, the game
  tells you when it starts. On Android 9 or later it also offers to use a
  graphics driver package instead, which only helps on a phone with an
  Adreno chip (most phones with a Qualcomm Snapdragon processor; see
  [Using a different graphics driver](#using-a-different-graphics-driver)).
  Graphics driver packages need Android 9 or later.
- **Android 11 phones and tablets, such as the Logitech G Cloud (Adreno 618).**
  The game has run on the older graphics driver those devices ship (Vulkan
  1.1) since version 0.9.1, but no one has confirmed it on a real device yet.
  When you start the game, look in its log (`logs/ico-pc.log`) for a line like
  `<name of the graphics chip> (Vulkan 1.1.x) ...; render passes legacy; sync fences`.
  That line means the game is using the Vulkan 1.1 route. If you try it, please
  tell us how it went and send that log.
- **Your own disc image of ICO**: the European release (SCES-50760), the
  Japanese release (SCPS-11003), the Korean release (SCPS-56001), the North
  American release (SCUS-97113), the Asian release (SCPS-55001), the August
  2001 preview disc or the June 2001 magazine demo (SCUS-97158), as a `.iso`, `.chd` or `.bin` file,
  copied onto the phone or tablet. The Japanese, North American and preview
  discs are CDs, so their images are usually a `.bin` with a `.cue` beside it:
  copy the `.bin`.
  [What a disc image is](../README.md#what-you-need), and
  [what differs between the seven discs](../README.md#the-seven-discs).
- **Free space** for the first start: the size of the disc image plus
  about 1.2 GB. Once the game is set up, it needs about 1 GB. The June 2001
  magazine demo is the exception: its image is the whole magazine disc, about
  4.4 GB, so the first start needs about 5.6 GB free. The screen says so while
  it copies, and says how much is needed when there is not enough. The demo
  setup keeps only ICO's part of the image (about 280 MB).

## Install it

1. On your phone or tablet, open the
   [releases page](https://github.com/nathanialf/ico-pc/releases) and
   download the file whose name ends in `-android.apk`.
2. Open the downloaded file. Android may ask you to allow installing apps
   from your browser or file manager. Allow it, then go back and tap
   **Install**.
3. Open **ICO** from your apps.

To update, install the new version over the old one in the same way. Your
saves stay.
Close other apps first: the game prepares its graphics in full once after
every update. If the phone froze and you had to restart it during an update
and the game then shows "bad ELF magic", see
[the fix](TROUBLESHOOTING.md#the-first-start-takes-a-long-time).

A test build someone gave you (not from the releases page) may block
installing a release over it; then you must uninstall the test build
first, which deletes your saves: copy the `memcard` folder to a computer
first ([where it is](#where-your-files-are)).

## The first start

1. The first time the game opens, and the first time after you install a
   new version, the screen says **Starting ICO** and **Preparing
   graphics**, with a count and a bar. On some phones this takes a few
   minutes. After a few seconds the screen also says about how much time
   is left (a rough guess from how fast the count has gone so far) and
   that this happens once after an update. Leave the game open until the
   count is done. After an update the game starts this from nothing:
   what was prepared before the update is thrown away. Later starts skip it or
   show it only for a moment. If you
   close the game during it, the next start shows it again.
2. The game asks you to choose your ICO disc image (any of the seven discs
   above), and opens your
   phone's file picker. Find your disc image and tap it. The picker shows
   every file, so make sure you pick the `.iso`, `.chd` or `.bin` of the
   disc. If you have a `.bin` and a `.cue` (the usual form of the
   Japanese disc), pick the `.bin`: a `.cue` on its own is not the disc. For
   the magazine demo, pick the whole magazine image.
   On a phone without the system's own file picker, the game opens
   another file manager app instead, if one is installed.
3. The game copies the image into its own folder, naming the copy
   `Ico_PAL`, `Ico_JPN`, `Ico_KOR`, `Ico_USA`, `Ico_CN`, `Ico_AUG6` or `Ico_JUN26` and the file's ending, after the disc it is. The screen says **Copying
   the disc image into the app**, with a bar. With a big image this can take
   a few minutes. At the end it says **Saving the copy** for a few seconds
   while the phone finishes writing the file.
4. Then it says **Opening the disc image** and **Checking the disc image**
   (a bar), then **Preparing the game's data** (a bar) and **Finishing the
   game's data**.
5. **Starting the game** shows on every start. A few seconds of black can
   follow it, until the game's own logo appears.

At each step the screen says what the game is doing. Where it shows no
bar, the step is short and its length cannot be told in advance. Leave the
game open and do not switch away.

If there is not enough free space, the game tells you how much it needs
and how much is free. Free some space and start it again.

To stop while a bar with "Press Back to stop" is showing, press **Back**.
The game deletes the copy it made, so nothing is left behind, and the next
start asks again.

Once the game is set up, it deletes its copy of the disc image to give the
space back. Your own file, where you picked it from, is not touched.

**Skipping the picker.** You can also copy the disc image into the game's
folder yourself, named for your disc: `Ico_PAL` (European), `Ico_JPN`
(Japanese), `Ico_KOR` (Korean), `Ico_USA` (North American), `Ico_CN` (Asian),
`Ico_AUG6` (August 2001 preview) or `Ico_JUN26` (June 2001 magazine demo),
with the ending `.iso`, `.chd` or `.bin`. The game then uses it without
asking. It deletes that file too once it is set up.

**Keeping the copy.** Most people do not need this. If you want the disc
image to stay in the game's folder, you need a text file named
`ico-pc.ini` in that folder ([where it is](#where-your-files-are)) with
this line in it:

```
keep_image=1
```

1. Open the game once and close the file picker without choosing a file
   (the game says no disc image was chosen and closes). The game's folder
   now exists.
2. On a computer, make the text file with Notepad or any text editor.
3. With the USB cable, copy `ico-pc.ini` and your disc image, named
   `Ico_PAL.iso` (European disc), `Ico_JPN.iso` (Japanese disc),
   `Ico_KOR.iso` (Korean disc), `Ico_USA.iso` (North American disc),
   `Ico_CN.iso` (Asian disc), `Ico_AUG6.iso` (August 2001 preview) or
   `Ico_JUN26.iso` (June 2001 magazine demo), into
   the game's folder, and start the game again.

## Where your files are

Everything the game keeps is in one folder on your phone:

```
Android/data/com.defnf.icopc/files
```

You can reach it with a USB cable from a computer: open the phone's
storage, then `Android`, `data`, `com.defnf.icopc`, `files`.

In that folder:

- `memcard` holds your saves: the European disc's in `BESCES-50760ico`,
  the Japanese disc's in `BISCPS-11003ico`, the Korean disc's in
  `BISCPS-56001ico`, the Asian disc's in `BISCPS-55001ico`, the North American
  disc's in `BASCUS-97113ico` and the June 2001 magazine demo's in
  `BISCUS-97113ico`.
- `states` holds your save states, one folder for each disc. Saving and
  loading them is on the pause menu's **Save states** row (and under
  **Options > Extras** on the title screen); there are no key shortcuts on a
  phone. Copy it along with `memcard` when you back up.
- `memcard-preview` holds the August 2001 preview disc's saves (in its own
  `BASCUS-97113ico`): they are not the North American disc's kind of save,
  so they are kept apart. Copy it along with `memcard` when you back up.
- `drivers` is where a graphics driver zip goes when the file picker will
  not open ([Using a different graphics
  driver](#using-a-different-graphics-driver)). The game makes the folder
  when it starts.
- `logs` holds the log file, `ico-pc.log`, which helps with problem
  reports. When the graphics driver stops responding, the game also saves
  a copy of the picture it was drawing there, a file whose name starts
  with `loss-` and ends in `.zip` (the three newest are kept).
- `textures/SCES-50760/replacements` is where a
  [texture pack](TEXTURE_PACKS.md) for the European disc goes, and
  `textures/SCPS-11003/replacements` one for the Japanese disc,
  `textures/SCPS-56001/replacements` one for the Korean disc,
  `textures/SCPS-55001/replacements` one for the Asian disc,
  `textures/SCUS-97113/replacements` one for the North American disc and the
  August 2001 preview, and `textures/SCUS-97158/replacements` one for the June
  2001 magazine demo. The game
  makes the folder for the disc you play when it starts.
- `textures/SCES-50760/dumps` (and the other discs' folders named above) holds the pictures saved by **Options > Extras > Export textures**
  and by Dump textures, with a list named `manifest.txt`. You can copy them
  to a computer with the USB cable to draw new ones. The export takes about
  160 MB for the European disc.
- `models/SCES-50760/replacements` is where a
  [model pack](MODEL_PACKS.md) for the European disc goes, and
  `models/SCPS-11003/replacements` one for the Japanese disc and
  `models/SCPS-56001/replacements` one for the Korean disc (the other discs'
  folders have the names listed for textures above). Make these
  folders yourself.
- `screenshots` holds the pictures you take in photo mode.
- `dumps` holds the pictures saved for a bug report
  ([how](#send-a-problem-report)).
- `config.toml` holds your settings.
- A big file of about 1 GB is the game's data, copied out of your disc
  image (one for each disc you set up). Do not delete it, or the next
  start asks for the disc image again.

If the game cannot write to that folder (it is then missing or empty),
it uses a private folder instead, which a computer or the Files app cannot
open.

> [!WARNING]
> Uninstalling the game deletes this whole folder, saves included. Copy
> the `memcard` folder to a computer or another safe place first.

If your phone backs up its apps, the backup keeps your saves, your
settings and your achievements. It leaves out the game data, the disc
image, the logs and the packs. Whether the backup comes back after you
uninstall and install the game again depends on the phone, so keep your
own copy of `memcard` too.

## Controls

**Touch.** Buttons are drawn on the screen over the game, laid out like a
PlayStation controller with the game's own button pictures: a stick in the
lower left, the D-pad above it beside L1 and L2 in the top left corner, the
four face buttons in the lower right with R1 just above them, R2 in the top
right, Start and Select at the top in the middle, and a look pad (the area
you swipe to turn the camera) on the rest of the upper right. They are about
the same real size on every phone and tablet (smaller when the screen is too
short for the chosen Touch size) and never cover each other. They fade away
when you stop touching the screen and come back when you touch it.
**Options > Controls > Touch screen** has **Touch size** and **Touch
opacity** to make them bigger or smaller and more or less see-through, and
**Edit layout** to move, resize, hide or switch them off yourself, for each
way you hold or fold the phone.
[The full description](CONTROLS.md#touch-controls-phones-and-tablets).
If Ico does not run in every direction you push the on-screen stick, turn
on **Options > Gameplay > Analogue stick fix**.

**Foldables.** The buttons keep clear of the fold. When the fold runs from
top to bottom, each hand's buttons stay on their own side of it, flat or
half-folded. When it runs across the screen: half-folded like a laptop,
all the buttons move to the lower half; opened flat, they stay where they
are, unless one would sit on the fold: then they all move below it.

**Back.** The phone's Back button or gesture opens the pause menu while
you play, like Start. In the menus it goes back, like Triangle.

**Vibration.** When no gamepad is connected, the phone itself vibrates
where a gamepad would rumble, for example when Ico is hurt. A connected
gamepad that can rumble takes over. To turn it off, open the pause menu's
**Options > Controls** and set **Vibration** to Off. The row is only in the
pause menu's Options.

**Gamepads.** A Bluetooth or USB gamepad works, with rumble if it has it.
While one is connected, the touch buttons go away. Change that in
**Options > Controls > Touch screen > Touch controls**.

**Keyboards.** A keyboard works too, with the same keys as on a PC
([the table](CONTROLS.md#play-with-a-gamepad-a-keyboard-or-a-mouse)).
Escape works like the Back button.

**Closing the game.** Choose **Quit game** on the title screen.
Save first. You can also close it from the phone's recent apps screen,
like any app.

## If the game stutters

First set **Options > Display > Frame rate** to **Original**. The game
then draws each picture once, as the PS2 did, which leaves the phone the
most time for the game itself. The game starts at 60 frames a second on
Android.

If it still stutters and you changed **Preset** to Enhanced or raised
**Options > Display > Resolution**, lower it again: 1x (the normal setting)
is the lightest. **Auto** does this for you: it starts at the size of your
screen and steps down (3x, then 2x, then 1x) when the phone cannot draw the
pictures in time. It never steps back up while the game runs; the row shows
where it ended up, for example **Auto (2x)**. The Enhanced preset uses Auto
on Android.

## Send a problem report

1. Copy `logs/ico-pc.log` from the game's folder (see above) to a
   computer. On a test build you can send it from the phone instead (see
   "Sending the files from the phone" below).
2. Open a report on the
   [issues page](https://github.com/nathanialf/ico-pc/issues), say which
   phone or tablet you have, and attach the file.

A few problems are easier to track down with a little more. If one of
these is yours, add it to the report.

**The game is slow.** If the whole log is too big to send, send its first
lines (they say which graphics chip, graphics driver and settings the game
found)
and, from a moment of play that stutters, a few of the speed reports the
game writes every 10 seconds. Those lines start with `window:`. Copy them
as they are.

**The picture is the wrong size.** If the opening scene after the logo
is drawn too small, shifted or stretched, send the first
lines of the log from that start, down to where the scene looks right.
They say what size the game was told the screen is, and when that changed.
Copy them as they are.

**The game closes by itself.** When the game has to close, it usually
first shows a message that says so and where its log is. Sometimes the
phone closes it without a message (for example when the phone runs short
of memory); the log then just stops. If that happened while the game was
getting its graphics ready, the next start's log says where it stopped,
and that start gets its graphics ready as you play. If it stops there
two starts in a row, the game does that from then on and writes it into
`config.toml` ([how to undo that](TROUBLESHOOTING.md#the-first-start-takes-a-long-time)).
A stop while the game was in the background does not count. Send `logs/ico-pc.log` from the start where it
happened. The same goes for a message that the graphics driver stopped
responding while the game was drawing: the log says what was being drawn.
Also send the newest `loss-` file from `logs` (the message names it). At the
next start, before the title, the game draws that picture again one step at
a time to find the step that makes the driver stop, and writes the answer in
the log, so send the log of that start too. If you have started the game
again since, the log of the start before is `logs/ico-pc-previous.log`. If you cannot send the whole file,
copy everything from the line of `=====` signs near the end (the next line
says `CRASH` or `WATCHDOG`) down to the last line, or the last 60 lines if
there is no such line. They say where the game stopped. If it closed while
the screen said "Preparing graphics", also say which numbers the screen
showed last. Also tell us what you pressed just before, and whether your
saves in `memcard` came from this phone or were copied from somewhere
else.

**Something looks wrong in the picture** (a line, a bright band or a mark
that stays on the screen). Save a picture of it for the report:

1. In **Options**, choose **Save a picture for a bug report**, the row just
   above Developer on the first page (you do not need Developer mode
   for it).
2. Leave the menu right away. 3 seconds after you chose the row, the game
   saves what is on the screen in the `dumps` folder of the game's folder:
   a `.zip` file whose name starts with `frame-` and the date and time, and
   pictures with the same name. Send the `.zip` (it holds the log too) and
   the picture ending in `-present.png`.

If you cannot get to the menu while it shows, the game can save
recordings of its frames by itself instead. Add this line to the text file
`ico-pc.ini` in the game's folder ([where it is](#where-your-files-are);
see [Keeping the copy](#the-first-start) for how to make that file and copy
it to the phone):

```
dump_every=300
```

The game then saves a recording of the frame on the screen (a file the
game can draw again for us, not a picture) about every ten seconds into
`dumps`. Play to the place, wait there at least 15 seconds, close the
game, put the newest file from `dumps` and `logs/ico-pc.log` in a `.zip`
file (the issues page takes `.zip` files) and send it. Each file is a few
megabytes, so delete the line (and the files) when you are done.

**Sending the files from the phone (test builds only).** The test builds
we post while we look into a problem (their names end in `-rc` and a
number) have one more row on the first page of **Options**: **Share a bug
report**, just below Save a picture for a bug report. The normal releases
do not have it. Choose it after the picture is saved. The phone's share
menu opens with the log, the log of the start before, the newest `.zip`
and the newest picture ending in `-present.png` from `dumps`, and the
newest `report-` file from `logs` if there is one. Pick an email or chat
app, or save them to the phone's files, and attach them to your report.
No computer is needed.

**The buttons you pressed.** Each start also writes a file in `logs`
whose name starts with `input-` and goes on with the date and time of
that start. It lists the buttons you pressed. With it and your `memcard`
folder as it was when you started, we can play the same session again on
a computer, step for step, and see whether the problem happens there too.
Send it with the log for any problem you can make happen again: closing
by itself, a pose, a place.

**A save that shows a problem.** If something looks wrong in the game (a
pose, a place, a character), save near it if you can, then send the
`memcard` folder with the log. We load the same save on a computer to see
whether it happens there too.

## Screens of every shape

The picture reaches into the notch or camera hole (the game's buttons keep
clear of it, and Edit layout can put one there; see CONTROLS.md), and it fills a folding
phone's screen when you open it. The game keeps running as you fold and
unfold.

Options > Display > Aspect ratio decides how the picture fits:

- **Auto** fills a wide phone screen. The game draws the whole width of
  the screen and shows more of the scene to the sides instead of leaving
  bars. Menus, subtitles and movies stay in a 4:3 box in the middle.
- The normal 4:3 setting shows bars at the sides, as on the PS2.
- On a folding screen that is nearly square, the picture keeps its 4:3
  shape with bars above and below. It is not cropped.

## Using a different graphics driver

This is optional, and it only works on phones with an Adreno graphics chip
(most Qualcomm Snapdragon phones) and Android 9 or later; on other phones
the Graphics driver page is not shown at all. If the game looks wrong or runs badly on
your phone, a newer graphics driver from the community can sometimes fix
it. If you do not have a problem, you can skip this.

1. On your phone, download a driver package. The community shares them on
   GitHub as zip files (search for "AdrenoToolsDrivers"). Leave the file
   as a zip. Do not unpack it.
2. In the game, open **Options > Graphics driver** and choose **Add a
   driver**. Pick the zip file you downloaded. The game says when the
   driver has been added, or that the file is not a driver package.
   On a phone without the system's own file picker, the game opens
   another file manager app instead, if one is installed.
   If the file picker does not open, closes at once without letting you
   choose, or the game says that no app on the phone can choose a file,
   use the game's `drivers` folder instead: with the USB cable, copy the
   zip into `Android/data/com.defnf.icopc/files/drivers` (the `drivers`
   folder in the game's folder, [where it is](#where-your-files-are); the
   game writes this path in its messages), open **Options > Graphics
   driver** again and choose **Add from the drivers folder**. That row only
   shows while there is a zip in the folder; Left and Right pick between
   zips if there are several. The game deletes the zip once the driver has
   been added. If it says the chosen file could not be read, copy the zip
   onto the phone first and choose it again.
3. In the same page, set the **Driver** row to the driver you added.
4. Close the game completely and start it again. The new driver is used from
   that start.

To go back to the phone's own driver, set **Driver** to **Built-in**, or
choose **Remove this driver** to delete it. Restart the game after either.

A driver package is loaded by the game itself, not by the phone's system,
so it can work on a phone whose own graphics driver or Vulkan version is
too old for the game. This is also the way to try a different driver after
the phone's own driver stopped responding while you played (the Qualcomm
driver on some Snapdragon tablets does): after such a stop, the note under
**Options > Graphics driver** names this route and the folder.

If a driver does not start, the game falls back to the phone's own driver
and tells you so. You can still play. The driver is not removed, so you can
try another one, or remove it yourself: in **Options > Graphics driver**,
set **Driver** to it, then choose **Remove this driver**.

### When the game cannot start with the phone's driver

On some phones the phone's own graphics driver cannot run the game at all,
so the Options menu is never reached. The game then shows a box that says
it could not start the game's graphics with this phone's driver, with
these buttons:

- **Choose a driver file**: the file picker opens. Pick a driver package
  zip you downloaded (step 1 above). The game adds it and asks you to
  close it and open it again: the driver is used from the next start.
  This only works on phones with an Adreno chip. The box also names the
  folder to copy the zip into if the picker does not open
  (`Android/data/com.defnf.icopc/files/drivers`). A driver package is loaded
  by the game itself, without the phone's own Vulkan loader, so it can work
  where the phone's driver does not.
- **Quit**: closes the game.

On Android 8 there is no driver package to choose (they need Android 9 or
later): the box says the game could not start the graphics on this device
and asks you to send the log file, and the game closes.

If the file is not a driver package, the game says so and the box comes
back, so you can try another one. If the picker cannot open, or closes
without a file (some phones' pickers do), and there is a driver zip in the
`drivers` folder, the game adds the first one from there; with no zip in the
folder, the box comes back and names the folder. If the driver does not start at the next
start either, the game says so and shows the box again. Once the game runs,
the driver stays chosen, and you can change it later in **Options >
Graphics driver**.

## Known limits

- The game needs Android 8 or later and a graphics chip that supports Vulkan 1.0 or later (Vulkan
  is how the game talks to the graphics). Older or cheaper devices may not have it, and a system
  update does not always add it. On a phone with an Adreno chip and Android 9 or later, a graphics
  driver package can help; see
  [When the game cannot start with the phone's driver](#when-the-game-cannot-start-with-the-phones-driver).
  Graphics driver packages need Android 9 or later: on Android 8 the game always uses the phone's
  own driver.
- Phones from the Vulkan 1.0 years (most Android 8 phones) have older graphics drivers. The first
  start can spend several minutes preparing the game's graphics, and some of these phones may not
  manage it. If the game closes during that first start, start it again: the next start prepares
  less at once. If it still does not start, please send the log
  ([Send a problem report](#send-a-problem-report)).
- The file picker cannot hide files that are not disc images. If you pick
  the wrong file, the game says so and asks again at the next start.
- The game runs slower than on a PC, especially at a high Resolution in
  Options > Display. See [If the game stutters](#if-the-game-stutters).
