# Saves and portable mode

[Back to the front page](../README.md)

## Where your saves are

The game keeps your saves and settings in a folder of its own, called the
user folder here. It is not the folder the program is in:

- Windows: `%APPDATA%\ico-pc\ico-pc\`
  (type that into the address bar of a File Explorer window and press
  Enter)
- Linux and Steam Deck: `~/.local/share/ico-pc/ico-pc/`
  (`.local` is a hidden folder in your home folder)
- Mac: usually `~/Library/Application Support/ico-pc/ico-pc/`
  (in Finder, choose **Go > Go to Folder** and type that path). On a Mac,
  `ico-pc.ini` and the `logs` folder are in this folder too.
- iPhone: ICO's Documents folder, which you reach through LiveContainer.
  The settings, the saves, `ico-pc.ini` and the `logs` folder are all
  there.
- Android: see [the Android guide](ANDROID.md#where-your-files-are)

In the user folder:

- `states` holds your save states, one folder for each disc, with each
  slot's file and picture. They are separate from your memory card: copy this
  folder too when you move to another computer. A state only loads in the
  version of the game that saved it.
- `memcard` is your memory card. It holds your saves, as normal files.
  PCSX2 can keep a memory card as a folder in the same way, so saves can
  move between the two. The European disc's saves are in the folder
  `BESCES-50760ico` inside it, the Japanese disc's in `BISCPS-11003ico`,
  the Korean disc's in `BISCPS-56001ico`, the Asian disc's in
  `BISCPS-55001ico`, the June 2001 magazine demo's in `BISCUS-97113ico` and the
  North American disc's in `BASCUS-97113ico`, as on a PS2 memory card, so
  they do not mix.
- `memcard-preview` is the August 2001 preview disc's own memory card. The
  preview writes its saves under the same name as the North American disc
  (`BASCUS-97113ico`), but they are not the same kind of save, so the game
  keeps them in this folder of their own.
- `config.toml` holds your settings.
- The game's own data, copied from your disc image at the first start: one
  large file for each disc you set up.
- `screenshots` holds the pictures you take in photo mode.
- Your achievements, and any texture or model pack you put there.

**Back up your saves** by copying the `memcard` folder (and `memcard-preview`,
if you play the August 2001 preview) somewhere safe.

**To uninstall,** delete the program's folder (or the AppImage file) and
the user folder. On a
Mac, also drag ICO from Applications to the Bin; on an iPhone, remove it
in LiveContainer; on Android, uninstall the app
([copy your saves first](ANDROID.md#where-your-files-are)).

## Keep everything in one folder

Portable mode keeps your saves, settings and the game's files beside the
program instead, in a folder named `userdata`. That is handy on a USB
stick, or to keep two separate copies of the game. Portable mode is for
Windows and Linux:

- On Android and on an iPhone the game always keeps everything in its own
  folder, so there is nothing to turn on. A `portable=` line in
  `ico-pc.ini` is ignored there.
- On a Mac, a `userdata` folder placed next to the ICO app is not used.
  The game keeps everything in the folder above. Portable mode is not
  available on a Mac: do not add `portable=1` to `ico-pc.ini` there.

There are three ways to have it:

- **Download the portable version.** The release has a portable download
  for Windows and one for Linux (`...-win-portable.zip`,
  `...-linux-portable.tar.gz`). It is the same game with portable mode
  already on: unpack it where you want it and the game makes `userdata` at
  the first start. Its `ico-pc.ini` has the line `portable=1`.
- **Run the make-portable script** that comes in the normal download:
  `make_portable.cmd` in the `x64` folder on Windows (double-click it) or
  `make_portable.sh` on Linux. It makes the `userdata` folder for you. If
  you already have saves in your user folder, it offers to move them in. It
  stops without changing anything if the game is running or if `userdata`
  already holds files.
- **Make the folder yourself.** Make an empty folder named `userdata`
  beside the program. On Windows that is inside the `x64` folder. That is
  all.

**With the AppImage** (Linux), the program is the AppImage file itself:
make an empty folder named `userdata` next to the file, and the game keeps
your saves, settings and its own files there. `ico-pc.ini` and the `logs`
folder are next to the file as well. That is the only step: there is no
portable AppImage download and no make-portable script for it. This suits
an external drive or a USB stick, as long as you can write to it; to move
saves you already have, see [below](#move-an-existing-game-into-portable-mode).

You can also turn it on in `ico-pc.ini`, the file beside the program. Open
it in a text editor and find this line:

```
# portable=1
```

Delete the `#` at the start so it reads `portable=1`, and save the file.

To check, look in `logs/ico-pc.log`: one of the first lines ends in
"(portable)".

To turn it off again even with a `userdata` folder there (the portable
download too), put this line in `ico-pc.ini`:

```
portable=0
```

## Move an existing game into portable mode

The make-portable script above can move them for you. To do it by hand:

1. Close the game.
2. Copy everything from the user folder ([where it is](#where-your-saves-are))
   into the `userdata` folder.
3. Start the game.

## Bring a PS2 save over

The game comes with a small tool, `mc_import`, that copies ICO's save out
of a PS2 memory card file. It is in the `tools` folder of the download:
`x64\tools\mc_import.exe` on Windows, `tools/mc_import` on Linux. If you
play the AppImage, take `tools/mc_import` from the `-linux.tar.gz` download
(`-linux-arm64.tar.gz` for the ARM AppImage) of the same version.

It reads memory card files from emulators and save tools: `.ps2` and `.bin`
memory card files (not the disc image), and `.psu` files. It cannot read
`.max` or `.cbs` files, so convert those to `.psu` first with a PS2 save
tool.

On Windows:

1. Copy your memory card file (in PCSX2 it is in the `memcards` folder,
   for example `Mcd001.ps2`) into the `x64\tools` folder.
2. In File Explorer, open that folder, type `cmd` in the address bar and
   press Enter. A command prompt opens in that folder.
3. Type this and press Enter:

```
mc_import.exe --to "%APPDATA%\ico-pc\ico-pc\memcard" Mcd001.ps2
```

On Linux, open a terminal in the game's folder and tell it where your
`memcard` folder is and which file to read, for example:

```
tools/mc_import --to ~/.local/share/ico-pc/ico-pc/memcard Mcd001.ps2
```

It only takes ICO's save and leaves other games alone. It works for a save
of any of the seven discs, and puts each in the
folder for its disc. It will not replace a save that is already there unless you add
`--overwrite`.

From a PCSX2 memory card kept as a folder, you do not need the tool. Copy
your disc's folder into `memcard`: `BESCES-50760ico` (European disc),
`BISCPS-11003ico` (Japanese disc), `BISCPS-56001ico` (Korean disc),
`BISCPS-55001ico` (Asian disc), `BASCUS-97113ico` (North American disc) or
`BISCUS-97113ico` (June 2001 magazine demo). A save of the August 2001
preview goes in a `BASCUS-97113ico` folder inside `memcard-preview` instead.
The tool files a `BASCUS-97113ico` folder from a memory card as the North
American disc's; to import a preview save with it, give `memcard-preview` as
the folder.

After importing, check that the game lists the save.

## A second memory card

The PS2 had two memory card slots. To add a second card, close the game,
open `config.toml` in the user folder, and add a line under `[paths]`
naming a folder for it. For example, this line makes a second card in a
folder named `memcard2` beside the program:

```
saves2 = "memcard2"
```

On a Mac, give the full path instead, for example
`saves2 = "/Users/you/Library/Application Support/ico-pc/ico-pc/memcard2"`.

The August 2001 preview disc uses folders beside the ones you name, with
`-preview` added (`memcard2-preview` for the example above), as it does with
`memcard-preview` for the first card.
