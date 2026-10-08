# Playing on Android

[Back to the front page](../README.md)

> [!NOTE]
> Android support is experimental in this version. It plays, but the
> picture can be slow, the opening movie may glitch, and the touch layout
> is a first draft. See
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

1. The game opens your phone's file picker. Find your disc image and tap
   it. The picker shows every file, so make sure you pick the `.iso` or
   `.chd`.
2. The game copies the image into its own folder. A bar shows how far it
   has got. With a big image this can take a few minutes.
3. Then it copies the game's files out of the image, with another bar.
4. The game starts.

The first start has long quiet stretches: before the picker opens, between
the two bars, and before the game appears, the screen may not change for a
while. The game is still working. Leave it open and do not switch away;
the next version will show what it is doing during those stretches.

If there is not enough free space, the game tells you how much it needs
and how much is free. Free some space and start it again.

To stop at any point, during either bar, press **Back**. The game deletes
the copy it made, so nothing is left behind, and the next start asks again.

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

The game makes the `textures` and `models` folders for you. Inside them,
make the `SCES-50760/replacements` folders yourself.
- `screenshots` holds the pictures you take in photo mode.
- `config.toml` holds your settings.

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

**Back.** The phone's Back button or gesture opens the pause menu, like
Start.

**Gamepads.** A Bluetooth or USB gamepad works, with rumble if it has it.
While one is connected, the touch buttons go away. Change that in
**Options > Controls > Touch controls**.

**Keyboards.** A keyboard works too, with the same keys as on a PC
([the table](CONTROLS.md#play-with-a-gamepad-a-keyboard-or-a-mouse)),
except that Escape does not close the game.

**Closing the game.** Choose **Quit to desktop** on the title screen.
Save first. You can also close it from the phone's recent apps screen,
like any app.

## Send a problem report

1. Copy `logs/ico-pc.log` from the game's folder (see above) to a
   computer.
2. Open a report on the
   [issues page](https://github.com/nathanialf/ico-pc/issues), say which
   phone or tablet you have, and attach the file.

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

## Known limits

- The game needs Vulkan 1.2 graphics. Older or cheaper devices may not
  have it, and a system update does not always add it.
- The file picker cannot hide files that are not disc images. If you pick
  the wrong file, the game says so and asks again at the next start.
- The game runs slower than on a PC, especially at a high Resolution in
  Options > Display. Try 1x if it stutters. Making it faster is on the list
  for the next version.
- The opening movie shows picture glitches on some phones. The game itself
  is not affected.
- The on-screen buttons are a first layout. Their size and placement will
  change with feedback; Options > Controls lets you change the size now.

Android is **experimental** in this version: it works, but expect rough
edges, and tell us what you find on the
[issues page](https://github.com/nathanialf/ico-pc/issues).
