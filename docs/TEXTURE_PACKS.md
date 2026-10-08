# Texture packs

[Back to the front page](../README.md)

A texture pack is a set of pictures that replace the game's own textures,
usually sharper ones made by fans. The game works fine without one.

Packs made for PCSX2 work as they are. PCSX2 is a program that plays
PS2 games on a PC, and most ICO packs are made for it. You do not need to
rename or convert anything. The pack this was tested with is Sad Origami's
ICO PAL HD pack.

## Put a texture pack in

The game comes with a folder for a pack:

- Windows: `x64\textures\SCES-50760\replacements`
- Linux and Steam Deck: `textures/SCES-50760/replacements`, next to
  `ico_pc`
- Android: see [the Android guide](ANDROID.md#where-your-files-are)

A note named `README.txt` sits inside it to show you the right place.
SCES-50760 is the number of the PAL disc.

A pack you download usually has a `SCES-50760` folder of its own, with a
`replacements` folder inside. Copy the pack so that its files end up
inside the game's `replacements` folder. For example, on Windows a file
from the pack should end up at a place like this:

```
x64\textures\SCES-50760\replacements\some-picture.dds
```

The pack's own folders inside `replacements` can stay as they are. If you
copy the pack one level too high, straight into `textures`, the game finds
it anyway.

Then:

1. Start the game.
2. Open **Options > Display** and check that **Texture pack** says **On**.

That row turns the pack on and off at once, so you can compare. It says
**None installed** if the game found no pack. Anything the pack does not
cover stays as the game draws it.

## Other places a pack can go

The game also looks in a `textures` folder in the same folder as your
saves ([where that is](PORTABLE_MODE.md#where-your-saves-are)). For
example, on Windows:

```
%APPDATA%\ico-pc\ico-pc\textures\SCES-50760\replacements\
```

In portable mode, that folder is `userdata` beside the program, so a pack
can also go in `userdata\textures\SCES-50760\replacements\`.
[About portable mode](PORTABLE_MODE.md#keep-everything-in-one-folder).

## Check that the game found it

The game's log file, `logs/ico-pc.log` beside the program, has a line for
each folder it read, such as:

```
textures: 883 replacements from ...
```

If it found nothing, the line says where it looked:

```
textures: no texture pack (looked in ...)
```

If one file in a pack cannot be used, a line starting with `textures:`
names the file, and the game shows its own texture there.

## Graphics cards

Most packs keep their pictures squeezed into a special format that the
graphics card unpacks. Every desktop graphics card and the Steam Deck can
do this. If yours cannot, the log says so and those pictures are skipped.

## If the pack uses a lot of memory

Big packs can use a lot of memory. Three lines in `config.toml`, under
`[video]`, control how much. Close the game before you edit the file.
[How to edit config.toml](OPTIONS.md#settings-you-can-only-change-in-a-file).

These lines start with `#`, which switches them off. Delete the `#` to
change one. If your `config.toml` is from an older version and does not
have them, add the line under `[video]` yourself.

- `texture_pack_budget_mb = 2048` is how much of the graphics card's
  memory, in MB, the pack may use. Textures past that stay the game's own,
  and the log says when that happens. If your card has more memory, raise
  it.
- `texture_pack_precache = true` reads the whole pack into the computer's
  memory in the background, starting with the subtitles and menus. The
  pack's author recommends this. Without it, the game's own subtitle can
  flash up for a moment before the pack's. Set it to `false` to read each
  texture only when the game first needs it, which uses less memory.
- `texture_pack_cache_mb = 0` is how much of the computer's memory, in MB,
  that reading ahead may use. `0` means up to half of it. On a computer
  with 16 GB that holds all of Sad Origami's pack (about 4 GB). On a
  smaller one it still holds the subtitles and menus, and the rest is read
  when the game needs it.

## Make a texture pack

To see which file name the game expects for a texture:

1. Turn on **Developer mode** in Options.
2. Turn on **Dump textures**, the row that appears under it.
3. Play to the place where the texture appears.

The game saves each texture as a PNG picture, under the same name PCSX2
uses, in `textures/SCES-50760/dumps` in the folder with your saves. Give
your picture that name, put it in `replacements`, and the game uses it
the next time you start it.

You cannot earn achievements while Developer mode is on, so turn it off
again when you are done.
