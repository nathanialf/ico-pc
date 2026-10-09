# ico-pc

This is **ICO** (Sony Computer Entertainment, 2001) made to run natively on
a Windows or Linux PC, the Steam Deck, and Android phones and tablets. It
runs the game's own code, so it plays, sounds and feels like the PAL
PlayStation 2 game. There are extras, such as a sharper picture,
widescreen and smoother motion, but they stay off until you turn them on.

> [!IMPORTANT]
> This project is not connected to Sony Interactive Entertainment or Team
> Ico. *ICO* is a trademark of its owners. The game itself is not included
> here or in the downloads. You need your own copy of the PAL disc
> (SCES-50760), saved as a disc image file, and the program reads the game
> from it. See
> [`docs/LEGAL.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/LEGAL.md).

## What you need

- **Your own disc image of ICO, PAL version (SCES-50760).** A disc image is
  one file that holds the whole disc. It ends in `.iso` or `.chd`. Other
  regions and editions do not work.
- **Something to play on:**
  - a 64-bit Windows 10 or 11 PC, or a Linux PC, with an up-to-date
    graphics driver (any recent NVIDIA, AMD or Intel graphics card works);
  - a Steam Deck;
  - or a 64-bit Android phone or tablet with Android 10 or later, usually
    one from 2022 or newer. Android is experimental in this version.
    [More about Android](docs/ANDROID.md).
- **About 1 GB of free space** for the game's files, which the first start
  copies out of the disc image.

## Install

Download the file for your device from the
[releases page](https://github.com/nathanialf/ico-pc/releases).

**Windows**

1. Download the file whose name ends in `-win.zip`.
2. Unzip it into a folder you can write to, such as Documents or a Games
   folder. Keep all the files together.
3. Open the `x64` folder and double-click `ico_pc_x64.exe`.

**Linux and Steam Deck**

1. Download the file whose name ends in `-linux.tar.gz`.
2. Unpack it into a folder in your home folder.
3. Double-click `ico_pc`, or run it from a terminal.

The Linux version needs a fairly new system: SteamOS 3.5, Debian 13,
Ubuntu 24.04 or newer. On a Steam Deck, unpack it in Desktop Mode and start it
once there so it can ask for the disc image, then
add `ico_pc` to Steam with Steam > Add a Non-Steam Game and play in Game
Mode. [Steam Deck tips](docs/TROUBLESHOOTING.md#steam-deck).

**Android**

1. Download the file whose name ends in `-android.apk` on your phone or
   tablet.
2. Open it. Your phone may ask you to allow installing apps from your
   browser or file manager first. Say yes.
3. Open **ICO** from your apps.

[The Android guide](docs/ANDROID.md) explains the first start, the touch
controls and where your saves are.

## The first start

The first time you start the game, it asks for your disc image. Choose the
file and wait while it copies the game's files out of it. This takes a
minute or two and happens only once. After that, the game no longer needs
the disc image, so you can move or delete it.

You can skip the question. Put the disc image beside the program and name
it `Ico_PAL.iso` (or `Ico_PAL.chd`). On Windows, "beside the program"
means inside the `x64` folder.

## Questions people ask first

- **Ico walks when I push the stick all the way. Why can't he run?** The
  original game only runs in eight directions. Turn on **Options >
  Gameplay > Analogue stick fix** to run in any direction.
  [The full answer](docs/FAQ.md#ico-walks-instead-of-running).
- **How do I use a texture pack?** Copy it into the `textures` folder that
  comes with the game. [Step by step](docs/TEXTURE_PACKS.md).
- **Can I turn off the blur or the glow?** Yes, in **Options > Effects**.
  [What each one does](docs/OPTIONS.md#effects).
- **Where are my saves?** In a folder the game makes for you.
  [Where that is](docs/PORTABLE_MODE.md#where-your-saves-are).

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
`ico-pc.log`, in a `logs` folder beside the program. Open a report on the
[issues page](https://github.com/nathanialf/ico-pc/issues) and attach that
file. [When something goes wrong](docs/TROUBLESHOOTING.md) explains the
error messages and what to try first.

## How it differs from the PS2

- The title screen has an **Options** line and a **Quit to desktop** line (**Quit game** on Android).
  Options is the same menu you get from the pause menu.
- The language and 50/60 Hz questions the PS2 asks at first start are
  skipped. Both are in Options.
- Starting a New Game shows one more screen, with **Mirror mode** and
  **New Game+**. [More about them](docs/OPTIONS.md#starting-a-new-game).
- You can change the language in the middle of a game.
- The ending credits run about four seconds longer, for a line of credit
  for this project.
- The pause menu shows your journey's numbers on the right.

Everything else is meant to match the PS2. If the picture or the timing is
different from the console, that is a bug worth reporting.

## Building it yourself

See [`docs/BUILDING.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/BUILDING.md).
You do not need the disc image to build it.

## Special thanks

- Sad Origami, for the ICO PAL HD texture pack, and for the support and
  blessing to test the port with it
  ([GBAtemp thread](https://gbatemp.net/threads/ps2-ico-pal-sces-50760-in-progress.671638/)).

## Legal and licence

The code in this repository is MIT licensed ([`LICENSE`](https://github.com/nathanialf/ico-pc/blob/main/LICENSE)). The
licence covers the code written for this project and grants no rights in the
game, its data or anything else owned by Sony Interactive Entertainment or
Team Ico. [`docs/LEGAL.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/LEGAL.md) says what may and may not be in
the repository, and [`docs/THIRD_PARTY.md`](https://github.com/nathanialf/ico-pc/blob/main/docs/THIRD_PARTY.md) lists the
third-party code the program uses. The game code began as a fork of the
[ICO decompilation](https://github.com/nathanialf/ico) project.
