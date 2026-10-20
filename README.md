# ico-pc

This is **ICO** (Sony Computer Entertainment, 2001) made to run as a normal
program on a Windows or Linux PC, the Steam Deck, 64-bit ARM Linux
handhelds, Android phones and tablets, iPhones and Apple-silicon Macs. It
runs the game's own code, so it plays, sounds and feels like the
PlayStation 2 game, from the European disc or the Japanese one. There are
extras, such as a sharper picture, widescreen
and smoother motion. The sharper picture, widescreen and the other picture
extras stay off until you turn them on, and the frame rate is not held to
the PS2's unless you choose Original. The mouse camera starts on.

> [!IMPORTANT]
> This project is not connected to Sony Interactive Entertainment or Team
> Ico. *ICO* is a trademark of its owners. The game itself is not included
> here or in the downloads. You need your own copy of the European disc
> (SCES-50760), the Japanese disc (SCPS-11003) or the Korean disc
> (SCPS-56001); the number is printed on the disc and on the case. Save it as a disc image file, and the program
> reads the game from it. More in the
> [project's legal notes](https://github.com/nathanialf/ico-pc/blob/main/docs/LEGAL.md)
> (written for developers).

## What you need

- **Your own disc image of ICO: the European release (SCES-50760), the
  Japanese release (SCPS-11003) or the Korean release (SCPS-56001).** The number is printed on the disc and
  on the case. A disc image is one file that holds the whole disc. It ends
  in `.iso`, `.chd` or `.bin` (a `.bin` usually comes with a `.cue` file,
  which also works). The Japanese release is a CD, so its image is most
  often a `.bin` and a `.cue`. The American release and other editions do
  not work. [Which disc to pick](#the-three-discs).
- **Something to play on:**
  - a 64-bit Windows 10 or 11 PC, or a Linux PC, with an up-to-date
    graphics driver (any recent NVIDIA, AMD or Intel graphics card works);
  - a Steam Deck;
  - a 64-bit ARM Linux handheld or board with an up-to-date graphics
    driver;
  - or a 64-bit Android phone or tablet with Android 10 or later, usually
    one from 2022 or newer. [More about Android](docs/ANDROID.md);
  - or an iPhone (iOS 15 or newer) or an Apple-silicon Mac (macOS 12 or
    newer); see [Supported platforms](#supported-platforms).
- **About 1 GB of free space** for the game's files, which the first start
  copies out of the disc image.

## Supported platforms

| Device | Download | Status |
|---|---|---|
| Windows 10 or 11, 64-bit, with a graphics card from NVIDIA, AMD or Intel | `-win.zip` (or `-win-portable.zip`) | The main test machine. Draws with Vulkan, or Direct3D 12 if you choose it ([how](docs/OPTIONS.md#choosing-the-renderer-on-windows)). |
| Linux PC, 64-bit x86: Debian 13, Ubuntu 24.04 or newer | `-linux-x86_64.AppImage`, or `-linux.tar.gz` (or `-linux-portable.tar.gz`) | Built and checked by the automatic tests for every release. |
| Steam Deck | `-linux-x86_64.AppImage` or `-linux.tar.gz` | Supported; [Steam Deck tips](docs/TROUBLESHOOTING.md#steam-deck). |
| Linux handheld or board, 64-bit ARM: Debian 13, Ubuntu 24.04 or newer | `-linux-aarch64.AppImage`, or `-linux-arm64.tar.gz` (or `-linux-arm64-portable.tar.gz`) | Built for every release. |
| Android phone or tablet, 64-bit, Android 10 or newer (usually 2022 or newer) | `-android.apk` | Supported. It can be slower than on a PC. [The Android guide](docs/ANDROID.md). |
| iPhone, iOS 15 or newer, through [LiveContainer](https://github.com/LiveContainer/LiveContainer) | `-ios-unsigned.ipa` | The maintainer has no Apple device, so these builds are not tested here; problem reports are welcome. |
| Apple-silicon Mac, macOS 12 or newer | `-macos-arm64.dmg` (or `-macos-arm64.zip`) | Not tested by the maintainer either. The first time, right-click **ICO** and choose **Open** ([how](#mac)). |

Not supported: 32-bit systems, Windows 7 and 8, Intel Macs, and any disc
other than the European release (SCES-50760), the Japanese release
(SCPS-11003) and the Korean release (SCPS-56001). Every download plays all
three discs.

## Install

The easy way: the [download page](https://nathanialf.github.io/ico-pc/)
picks the right file for your device. Or download the file for your device
from the [releases page](https://github.com/nathanialf/ico-pc/releases).

### Windows

1. Download the file whose name ends in `-win.zip`.
2. Right-click the file and choose **Extract All**, then pick a folder you
   can write to, such as Documents or a Games folder. Keep all the files
   together.
3. Open the `x64` folder and double-click `ico_pc_x64.exe`.

### Linux and Steam Deck

The simplest way is the AppImage, the game in a single file:

1. Download the file whose name ends in `-linux-x86_64.AppImage`. Put it
   in a folder of its own in your home folder, such as `Games/ICO`: the
   game keeps its settings file `ico-pc.ini` and its `logs` folder next to
   it.
2. Let it run as a program: right-click the file, choose **Properties**,
   and turn on the option that lets it run (called **Allow executing file
   as program** or **Is executable**, depending on your desktop; it may be
   on a **Permissions** tab). Or, in a terminal, run `chmod +x` followed by
   the file's name.
3. Double-click it.

Your saves and settings go to your user folder
(`~/.local/share/ico-pc/ico-pc/`), so a newer AppImage finds them. To keep
everything next to the file instead, for example on an external drive,
make an empty folder named `userdata` beside it
([portable mode](docs/PORTABLE_MODE.md#keep-everything-in-one-folder)).
Options > Extras > Add to Steam adds the AppImage to Steam. If it does not
start, see [Linux](docs/TROUBLESHOOTING.md#linux).

Or use the archive:

1. Download the file whose name ends in `-linux.tar.gz`.
2. Right-click the file and choose **Extract** (or **Extract Here**), into
   a folder in your home folder.
3. Double-click `ico_pc`, or run it from a terminal. If your file manager
   asks, choose **Run** or **Execute**.

The Linux version needs a fairly new system: Debian 13, Ubuntu 24.04 or
newer. On a Steam Deck, put it in a folder in Desktop Mode and start it
once there so it can ask for the disc image, then close Steam (right-click
its icon in the taskbar and choose **Exit Steam**) and choose Options >
Extras > Add to Steam on the title screen (or add the AppImage or `ico_pc`
by hand with Steam > Add a Non-Steam Game) and play in Game Mode. [Steam
Deck tips](docs/TROUBLESHOOTING.md#steam-deck).

On a 64-bit ARM Linux handheld or board, download the file whose name ends
in `-linux-aarch64.AppImage` (or `-linux-arm64.tar.gz`) instead and follow
the same steps. It also needs a fairly new system: Debian 13, Ubuntu 24.04
or newer.

### Android

1. Download the file whose name ends in `-android.apk` on your phone or
   tablet.
2. Open it. Your phone may ask you to allow installing apps from your
   browser or file manager first. Say yes.
3. Open **ICO** from your apps.

[The Android guide](docs/ANDROID.md) explains the first start, the touch
controls and where your saves are.

### Mac

1. Download the file whose name ends in `-macos-arm64.dmg` (or the one
   ending in `-macos-arm64.zip`, which holds the same app).
2. Open it. Open a second Finder window on Applications (**Go >
   Applications**) and drag **ICO** into it.
3. The first time, right-click **ICO** and choose **Open**, then **Open**
   again. Apple has not checked this app, so a plain double-click only
   shows a warning the first time. If your Mac still will not open it, go
   to **System Settings > Privacy & Security**, scroll down and choose
   **Open Anyway** next to ICO. After that, open it like any other app.

The Mac keeps your saves, your settings, `ico-pc.ini` and the `logs`
folder in a folder of its own, usually
`~/Library/Application Support/ico-pc/ico-pc/`. To get there, open Finder,
choose **Go > Go to Folder**, type that path and press Return.

### iPhone

The iPhone version is not in the App Store. You run it inside
[LiveContainer](https://github.com/LiveContainer/LiveContainer); set that
up first with its own instructions.

1. Download the file whose name ends in `-ios-unsigned.ipa` on your
   iPhone.
2. Add it to LiveContainer and start **ICO** once. It says the game data
   is missing and closes. (The message names a game data file; you do not
   need it: your disc image does the same job.) That is expected: it has
   not seen your disc image yet.
3. Copy your disc image into ICO's **Documents** folder through
   LiveContainer's data-folder access, and name it `Ico_PAL.iso` (or
   `Ico_PAL.chd`, `Ico_PAL.bin` or `Ico_PAL.cue`) for the European disc,
   `Ico_JPN.iso` (or `Ico_JPN.chd`, `Ico_JPN.bin` or `Ico_JPN.cue`) for
   the Japanese disc, or `Ico_KOR.iso` (or `Ico_KOR.chd`, `Ico_KOR.bin` or
   `Ico_KOR.cue`) for the Korean disc. With a `.bin` and a `.cue`, copy both, rename only
   the `.cue`, and leave the `.bin` with the name the `.cue` gives it. The
   iPhone version does not ask for the disc image, so
   it has to be there with one of those names.
4. Start **ICO** again. It copies the game's files out of the image, as
   described in [The first start](#the-first-start).

Your saves (the `memcard` folder), your settings (`config.toml`) and the
`logs` folder are in that same Documents folder.

## The first start

The first time you start the game, it asks for your disc image (an
iPhone uses the one you copied in, as above). Choose the file and wait
while it copies the game's files out of it. This takes a minute or two and
happens only once. From then on the game runs without
the disc image, so you can move or delete it. On Android, the screen says
what the game is doing the whole time
([the Android guide](docs/ANDROID.md#the-first-start) has the steps).

On Windows and Linux you can skip the question. Put the disc image beside
the program and name it `Ico_PAL.iso` (or `Ico_PAL.chd`, `Ico_PAL.bin` or
`Ico_PAL.cue`) for the European disc, `Ico_JPN.iso` (or `Ico_JPN.chd`,
`Ico_JPN.bin` or `Ico_JPN.cue`) for the Japanese disc, or `Ico_KOR.iso` (or
`Ico_KOR.chd`, `Ico_KOR.bin` or `Ico_KOR.cue`) for the Korean disc. With a
`.bin` and a `.cue`, put both there, rename only the `.cue`, and leave the `.bin` with
the name the `.cue` gives it. On Windows, "beside the program" means inside
the `x64` folder.

## The three discs

The game plays the European release of ICO (SCES-50760), the Japanese
release (SCPS-11003) and the Korean release (SCPS-56001). The disc image you
give it at the first start decides which one you play. All three are the
whole game; the differences are the
original releases' own:

- **The game's text.** The signs, menus and subtitles are the disc's own.
  The European disc has English, French, German, Italian and Spanish; the
  Japanese disc has Japanese, and the Korean disc has Korean.
- **Buttons.** On the Japanese disc and the Korean disc, Circle confirms and
  Cross goes back, as on those PS2 releases, and the menus this port adds do
  the same. [More in Controls](docs/CONTROLS.md#the-japanese-and-korean-discs).
- **Video mode.** The European disc offers 50 Hz or 60 Hz; the Japanese
  disc and the Korean disc are always 60 Hz.
- **The port's own menus** (Options, the extras, photo mode) can be shown in
  English, French, German, Italian, Spanish, Japanese or Korean, with the **Menu
  language** row on the first page of the Options menu
  ([more](docs/OPTIONS.md#language)).

Achievements, the music and models galleries, photo mode and the model
viewer work with all three discs. Saves, texture packs and model packs are
kept apart for each disc, so you can set up more than one on the same
device: see [Can I play more than one disc?](docs/FAQ.md#can-i-play-more-than-one-disc)

## Questions people ask first

- **Ico walks when I push the stick all the way. Why can't he run?** The
  original game only runs in eight directions. Turn on **Options >
  Gameplay > Analogue stick fix** to run in any direction.
  [The full answer](docs/FAQ.md#ico-walks-instead-of-running).
- **How do I use a texture pack?** Copy it into the `textures` folder that
  comes with the game (on a Mac or an iPhone, the guide says where to make
  one). [Step by step](docs/TEXTURE_PACKS.md).
- **Can I turn off the blur or the glow?** Yes, in **Options > Effects**.
  [What each one does](docs/OPTIONS.md#effects).
- **Where are my saves?** In a folder the game makes for you.
  [Where that is](docs/PORTABLE_MODE.md#where-your-saves-are).
- **Do the Japanese and Korean discs work?** Yes, the European disc
  (SCES-50760), the Japanese disc (SCPS-11003) and the Korean disc
  (SCPS-56001) all do. [The three discs](#the-three-discs).

[More questions and answers](docs/FAQ.md).

## Guides

| Guide | What it covers |
| --- | --- |
| [Questions and answers](docs/FAQ.md) | Short answers to the questions people ask most |
| [Controls](docs/CONTROLS.md) | Gamepad, keyboard and mouse, the model viewer, photo mode, touch controls |
| [Options](docs/OPTIONS.md) | Every row of the Options menu in plain words |
| [Texture packs](docs/TEXTURE_PACKS.md) | Sharper textures made by fans |
| [Model packs](docs/MODEL_PACKS.md) | Replacement 3D models, and how to make them |
| [ReShade](docs/RESHADE.md) | Adding your own picture effects |
| [Android](docs/ANDROID.md) | Playing on a phone or tablet |
| [Saves and portable mode](docs/PORTABLE_MODE.md) | Where your saves are, keeping everything in one folder, bringing a PS2 save over |
| [When something goes wrong](docs/TROUBLESHOOTING.md) | The log file, error messages, the Steam Deck |

## Getting help

When something goes wrong, the game writes what happened into a file named
`ico-pc.log`, in a folder named `logs`. Where that folder is:

- Windows and Linux: beside the program (on Windows, inside the `x64`
  folder; for the AppImage, beside the AppImage file). If the game cannot
  make the folder there, it uses the `logs` folder in your user folder
  ([where that is](docs/PORTABLE_MODE.md#where-your-saves-are)).
- Mac: in the game's folder, usually
  `~/Library/Application Support/ico-pc/ico-pc/logs`.
- iPhone: in ICO's Documents folder.
- Android: in the game's folder,
  `Android/data/com.defnf.icopc/files/logs`
  ([how to reach it](docs/ANDROID.md#where-your-files-are)).

When the game closes with an error it also writes a report file in the
same folder, named `report-` and the date, ending in `.zip`. Open a report
on the [issues page](https://github.com/nathanialf/ico-pc/issues), say which
disc you play (the European, the Japanese or the Korean) and attach the newest report
file, or `ico-pc.log` if there is none. [When something goes wrong](docs/TROUBLESHOOTING.md) explains the
error messages and what to try first.

## How it differs from the PS2

The title screen has an **Options** line and a **Quit to desktop** line
(**Quit game** on a phone); starting a New Game shows one more screen, with
**Mirror mode** and **New Game+** ([more about them](docs/OPTIONS.md#starting-a-new-game));
the language and 50/60 Hz questions the European PS2 disc asks at first
start live in Options instead, and the language can change in the middle of
a game; the
ending credits have one more heading and name, for this project; and the
pause menu shows your journey's numbers. Everything else is meant to match
the PS2: if the picture or the timing is different from the console, that
is a bug worth reporting. The full list of differences, written for people
who work on the code, is in
[`docs/port/DIVERGENCES.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/port/DIVERGENCES.md).

## Building it yourself

See [`docs/BUILDING.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/BUILDING.md).
You do not need the disc image to build it. How the port works, for
contributors: [`docs/port/ARCHITECTURE.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/port/ARCHITECTURE.md).

## Special thanks

- Sad Origami, for the [ICO PAL HD texture pack](https://ko-fi.com/s/7c0e3e044f),
  for the support and blessing to test the port with it, and for
  suggesting the download page.
- katsifolis, for the iOS support (through LiveContainer).
- braito4, for the Apple-silicon macOS app.
- neetgon, for the second player's gamepad.

## Legal and licence

The code in this repository is MIT licensed
([`LICENSE`](https://github.com/nathanialf/ico-pc/blob/main/LICENSE)). The licence covers the code
written for this project and grants no rights in the game, its data or anything else owned by Sony
Interactive Entertainment or Team Ico.
[`docs/LEGAL.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/LEGAL.md) says what may and
may not be in the repository, and
[`docs/THIRD_PARTY.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/THIRD_PARTY.md) lists
the third-party code the program uses. The game code began as a fork of the
[ICO](https://github.com/nathanialf/ico) project, which rebuilds the game's source code from the PS2
release.
