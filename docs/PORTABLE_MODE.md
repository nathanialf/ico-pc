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
- Android: see [the Android guide](ANDROID.md#where-your-files-are)

In the user folder:

- `memcard` is your memory card. It holds your saves, as normal files.
  PCSX2 can keep a memory card as a folder in the same way, so saves can
  move between the two.
- `config.toml` holds your settings.
- The game's own data, copied from your disc image at the first start.
- `screenshots` holds the pictures you take in photo mode.
- Your achievements, and any texture or model pack you put there.

**Back up your saves** by copying the `memcard` folder somewhere safe.

**To uninstall,** delete the program's folder and the user folder.

## Keep everything in one folder

Portable mode keeps your saves, settings and the game's files beside the
program instead, in a folder named `userdata`. That is handy on a USB
stick, or to keep two separate copies of the game.

To turn it on, make an empty folder named `userdata` beside the program.
On Windows that is inside the `x64` folder. That is all.

You can also turn it on in `ico-pc.ini`, the file beside the program. Open
it in a text editor and find this line:

```
# portable=1
```

Delete the `#` at the start so it reads `portable=1`, and save the file.

To check, look in `logs/ico-pc.log`: one of the first lines ends in
"(portable)".

To turn it off again even with a `userdata` folder there, put this line in
`ico-pc.ini`:

```
portable=0
```

## Move an existing game into portable mode

The game does not move your files for you.

1. Close the game.
2. Copy everything from the user folder ([where it is](#where-your-saves-are))
   into the `userdata` folder.
3. Start the game.

## Bring a PS2 save over

The game comes with a small tool, `mc_import`, that copies ICO's save out
of a PS2 memory card file. It is in the `tools` folder of the download:
`x64\tools\mc_import.exe` on Windows, `tools/mc_import` on Linux.

It reads memory card files from emulators and save tools: `.ps2`, `.bin`
and `.psu` files. It cannot read `.max` or `.cbs` files, so convert those
to `.psu` first with a PS2 save tool.

You run it from a command prompt (Windows) or a terminal (Linux). Tell it
where your `memcard` folder is and which file to read. For example, on
Windows:

```
x64\tools\mc_import.exe --to "%APPDATA%\ico-pc\ico-pc\memcard" Mcd001.ps2
```

and on Linux:

```
tools/mc_import --to ~/.local/share/ico-pc/ico-pc/memcard Mcd001.ps2
```

It only takes ICO's save and leaves other games alone. It will not replace
a save that is already there unless you add `--overwrite`.

From a PCSX2 memory card kept as a folder, you do not need the tool. Copy
the `BESCES-50760ico` folder into `memcard`.

After importing, check that the game lists the save.

## A second memory card

The PS2 had two memory card slots. To add a second card, close the game,
open `config.toml` in the user folder, and add a line under `[paths]`
naming a folder for it. For example, this line makes a second card in a
folder named `memcard2` beside the program:

```
saves2 = "memcard2"
```
