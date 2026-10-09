# Playing on Android

[Back to the front page](../README.md)

> [!NOTE]
> Android support is experimental in this version. It plays, but it can be
> slower than on a PC, and the touch layout is a first draft. See
> [Known limits](#known-limits).

## What you need

- **A 64-bit Android phone or tablet with Android 10 or later.** Its
  graphics must support Vulkan 1.2, which most phones and tablets from
  2022 or newer do. If yours does not, the game tells you when it starts.
- **Your own disc image of ICO, PAL version (SCES-50760)**, as a `.iso` or
  `.chd` file, copied onto the phone or tablet.
- **Free space** for the first start: the size of the disc image plus
  about 1.2 GB. Once the game is set up, it needs about 1 GB.

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

That holds from one release to the next. A test build you were given
(not from the releases page) is signed differently, and Android will not
install a release over it. You would have to uninstall the test build
first, and uninstalling deletes the game's whole folder, saves included
(see [Where your files are](#where-your-files-are)). Before you uninstall,
copy the `memcard` folder out to a computer.

## The first start

1. The first time the game opens, and again after each update, the screen
   says **Starting ICO** and **Preparing graphics**, with a count and a bar.
   This takes a few seconds, and up to about ten on some phones. Later starts
   skip it or show it only for a moment.
2. The game opens your phone's file picker. Find your disc image and tap
   it. The picker shows every file, so make sure you pick the `.iso` or
   `.chd`.
3. The game copies the image into its own folder. The screen says **Copying
   the disc image into the app**, with a bar. With a big image this can take
   a few minutes. At the end it says **Saving the copy** for a few seconds
   while the phone finishes writing the file.
4. Then it says **Opening the disc image** and **Checking the disc image**
   (a bar), then **Preparing the game's data** (a bar) and **Finishing the
   game's data**.
5. **Starting the game** shows on every start. A few seconds of black can
   follow it, until the game's own logo appears.

The screen always says what the game is doing. Where it shows no bar, the
step is short and its length cannot be told in advance. Leave the game open
and do not switch away.

If there is not enough free space, the game tells you how much it needs
and how much is free. Free some space and start it again.

To stop while a bar with "Press Back to stop" is showing, press **Back**.
The game deletes the copy it made, so nothing is left behind, and the next
start asks again.

Once the game is set up, it deletes its copy of the disc image to give the
space back. Your own file, where you picked it from, is not touched.

**Skipping the picker.** You can also copy the disc image into the game's
folder yourself, named `Ico_PAL.iso` (or `Ico_PAL.chd`). The game then
uses it without asking. It deletes that file too once it is set up.

**Keeping the copy.** Most people do not need this. If you want the disc
image to stay in the game's folder, put a text file named `ico-pc.ini` in
that folder ([where it is](#where-your-files-are)) with this line in it
(putting `keep_image = true` in `config.toml` under `[paths]` works the
same):

```
keep_image=1
```

The folder appears the first time you open the game, so this works best
together with copying `Ico_PAL.iso` there yourself.

## Where your files are

Everything the game keeps is in one folder on your phone:

```
Android/data/com.defnf.icopc/files
```

You can reach it with a USB cable from a computer: open the phone's
storage, then `Android`, `data`, `com.defnf.icopc`, `files`. Some phones
also let you open it in their Files app.

In that folder:

- `memcard` holds your saves.
- `logs` holds the log file, `ico-pc.log`, which helps with problem
  reports.
- `textures` is where a [texture pack](TEXTURE_PACKS.md) goes:
  `textures/SCES-50760/replacements`. The game makes these folders when
  it starts.
- `models` is where a [model pack](MODEL_PACKS.md) goes:
  `models/SCES-50760/replacements`.
- `screenshots` holds the pictures you take in photo mode.
- `config.toml` holds your settings.

The game makes the `textures` and `models` folders for you. Inside them,
make the `SCES-50760/replacements` folders yourself.

> [!WARNING]
> Uninstalling the game deletes this whole folder, saves included. Copy
> the `memcard` folder to a computer or another safe place first.

If your phone backs up its apps, the backup keeps your saves, your
settings and your achievements. It leaves out the game data, the disc
image, the logs and the packs. Whether the backup comes back after you
uninstall and install the game again depends on the phone, so keep your
own copy of `memcard` too.

## Controls

**Touch.** Buttons are drawn on the screen over the game: a stick on the
left, a look pad at the top right, the four face buttons at the bottom
right, a D-pad, the shoulder buttons in the top corners, and Start and
Select at the top. They fade away when you stop touching the screen and
come back when you touch it. [The full description](CONTROLS.md#touch-controls-phones-and-tablets).
If Ico does not run in every direction you push the on-screen stick, turn
on **Options > Gameplay > Analogue stick fix**.

**Back.** The phone's Back button or gesture opens the pause menu, like
Start.

**Gamepads.** A Bluetooth or USB gamepad works, with rumble if it has it.
While one is connected, the touch buttons go away. Change that in
**Options > Controls > Touch controls**.

**Keyboards.** A keyboard works too, with the same keys as on a PC
([the table](CONTROLS.md#play-with-a-gamepad-a-keyboard-or-a-mouse)),
except that Escape does not close the game.

**Closing the game.** Choose **Quit game** on the title screen.
Save first. You can also close it from the phone's recent apps screen,
like any app.

## If the game stutters

First set **Options > Display > Frame rate** to **Original**. The game
then draws each picture once, as the PS2 did, which leaves the phone the
most time for the game itself. The game starts at 60 frames a second on
Android.

If it still stutters, lower **Options > Display > Resolution**: 1x is the
lightest. **Auto** does this for you: it starts at the size of your
screen and steps down (3x, then 2x, then 1x) when the phone cannot draw
the pictures in time. It never steps back up while the game runs; the
row shows where it ended up, for example **Auto (2x)**. The Enhanced
preset uses Auto on Android.

## Send a problem report

1. Copy `logs/ico-pc.log` from the game's folder (see above) to a
   computer.
2. Open a report on the
   [issues page](https://github.com/nathanialf/ico-pc/issues), say which
   phone or tablet you have, and attach the file.

**Sending us a log about speed.** If the whole file is too big to send,
the parts that matter are the first lines of `logs/ico-pc.log` (they say
which phone, graphics and settings the game found) and, from a moment of
play that stutters, the four lines starting with `window:` that the game
writes every 10 seconds. Copy them as they are.

**Sending us a log about the picture's size.** If the opening scene after
the logo (the forest) is drawn too small, shifted or stretched, send the
first lines of `logs/ico-pc.log` from that start, down to where the scene
looks right, including every line that starts with `window:` and every
line that contains the word `swapchain`. They say what size the game was
told the screen is, and when that changed. Copy them as they are.

**Sending us a log about the opening scene.** If the forest after the logo
looks as if it were seen from another place (bare thin trunks, flat dark
bushes, a pale empty ground where the trees and grass should be), send
every line of `logs/ico-pc.log` that starts with `rd: camera`, and say
which phone you have. Those lines say where the game put its camera; we
compare them with a PC's to tell whether the game itself or the drawing
went another way.

**Sending us a log when the game closes by itself.** When the game has
to close, it first shows a message that says so and where its log is.
Send `logs/ico-pc.log` from the start where it happened. If you have
started the game again since, the log of the start before is
`logs/ico-pc-previous.log`. If you cannot send the whole file, copy
everything from the line of `=====` signs near the end (the next line
says `CRASH` or `WATCHDOG`) down to the last line, or the last 60 lines
if there is no such line. They say where the game stopped. Also tell us
what you pressed just before, and whether your saves in `memcard` came
from this phone or were copied from somewhere else.

**Sending us the buttons you pressed.** Each start also writes a file in
`logs` whose name starts with `input-` and goes on with the date and
time of that start. It lists the buttons you pressed. With it and your
`memcard` folder as it was when you started, we can play the same
session on a computer, step for step, and see whether the problem
happens there too. Send it with the log for any problem you can make
happen again: closing by itself, a pose, a place.

**Sending us a save that shows a problem.** If something looks wrong in
the game (a pose, a place, a character), save near it if you can, then
send the `memcard` folder with the log. We load the same save on a
computer to see whether it happens there too.

## Screens of every shape

The picture reaches into the notch or camera hole, and it fills a folding
phone's screen when you open it. The game keeps running as you fold and
unfold.

Options > Display > Aspect ratio decides how the picture fits:

- **Auto** fills a wide phone screen. The game shows more of the scene to
  the sides instead of leaving bars.
- The normal 4:3 setting shows bars at the sides, as on the PS2.
- On a folding screen that is nearly square, the picture keeps its 4:3
  shape with bars above and below. It is not cropped.

## Using a different graphics driver

This is optional, and it only works on phones with an Adreno graphics chip
(most Qualcomm Snapdragon phones); on other phones the Graphics driver
page is not shown at all. If the game looks wrong or runs badly on
your phone, a newer graphics driver from the community can sometimes fix
it. If you do not have a problem, you can skip this.

1. On your phone, download a driver package. The community shares them on
   GitHub as zip files, in the "AdrenoToolsDrivers" releases by K11MCH1.
   Leave the file as a zip. Do not unpack it.
2. In the game, open **Options > Graphics driver** and choose **Add a
   driver**. Pick the zip file you downloaded. The game says when the
   driver has been added, or that the file is not a driver package.
3. In the same page, set the **Driver** row to the driver you added.
4. Close the game completely and start it again. The new driver is used from
   that start.

To go back to the phone's own driver, set **Driver** to **Built-in**, or
choose **Remove this driver** to delete it. Restart the game after either.

If a driver does not start, the game falls back to the phone's own driver
and tells you so. You can still play. The driver is not removed, so you can
try another one or remove it yourself.

## Known limits

- The game needs Vulkan 1.2 graphics. Older or cheaper devices may not
  have it, and a system update does not always add it.
- The file picker cannot hide files that are not disc images. If you pick
  the wrong file, the game says so and asks again at the next start.
- The game runs slower than on a PC, especially at a high Resolution in
  Options > Display. See [If the game stutters](#if-the-game-stutters).
- The on-screen buttons are a first layout. Their size and placement will
  change with feedback; Options > Controls lets you change the size now.

Android is **experimental** in this version: it works, but expect rough
edges, and tell us what you find on the
[issues page](https://github.com/nathanialf/ico-pc/issues).
