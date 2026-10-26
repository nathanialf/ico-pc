# Texture packs

[Back to the front page](../README.md)

A texture pack is a set of pictures that replace the game's own textures,
usually sharper ones made by fans. The game works fine without one.

Packs made for PCSX2 work as they are. PCSX2 is a program that plays
PS2 games on a PC, and most ICO packs are made for it. You do not need to
rename or convert anything.
[Sad Origami's ICO PAL HD pack](https://ko-fi.com/s/7c0e3e044f) works with it.

## Which disc a pack is for

A pack is made for one disc. Each disc has its own folder, named after the
number printed on the disc:

- `SCES-50760` for the European disc;
- `SCPS-11003` for the Japanese disc;
- `SCPS-56001` for the Korean disc;
- `SCUS-97113` for the North American disc, and also for the August 2001
  preview disc, which has no number of its own and uses this folder;
- `SCUS-97158` for the June 2001 magazine demo;
- `SCPS-55001` for the Asian disc.

Put a pack in the folder of the disc it was made for; the pack's own
folder name usually says which. The North American disc and the preview read the same folder, so a
pack covers whichever pictures have the same names on the disc you play. Of these six folders, the game reads only
the one of the disc you play, so you can keep a pack in each. The other
places a pack can go (`textures/replacements`, and picture files loose in
`textures`, below) are read for every disc: a pack there shows on any
disc, so keep it in its disc's folder instead.

## Put a texture pack in

The game comes with a folder for a pack for each disc:

- Windows: `x64\textures\SCES-50760\replacements`,
  `x64\textures\SCPS-11003\replacements`,
  `x64\textures\SCPS-56001\replacements`,
  `x64\textures\SCUS-97113\replacements`,
  `x64\textures\SCUS-97158\replacements` and
  `x64\textures\SCPS-55001\replacements`
- Linux and Steam Deck: `textures/SCES-50760/replacements`,
  `textures/SCPS-11003/replacements`, `textures/SCPS-56001/replacements`,
  `textures/SCUS-97113/replacements`, `textures/SCUS-97158/replacements` and
  `textures/SCPS-55001/replacements`, next to `ico_pc`
- Android: see [the Android guide](ANDROID.md#where-your-files-are)
- Mac and iPhone: the download has no such folder; make one as shown in
  [Other places a pack can go](#other-places-a-pack-can-go)

A note named `README.txt` sits inside each to show you the right place.
The examples below use the European disc's `SCES-50760`; for a pack for
another disc, read that disc's number from the list above instead.

A pack you download usually has a folder named after its disc's number (`SCES-50760`, `SCPS-11003`, ...) of
its own, with a `replacements` folder inside. Copy the pack so that its
files end up inside the game's `replacements` folder for the same disc.
For example, on Windows a file from a pack for the European disc should
end up at a place like this:

```
x64\textures\SCES-50760\replacements\some-picture.dds
```

The pack's own folders inside `replacements` can stay as they are. If the
pack's `replacements` folder ends up straight inside `textures`, or its
picture files sit loose in `textures`, the game finds them too, for both
discs. Other
folders of the pack put straight into `textures` are not searched: move
them into `textures/SCES-50760/replacements`.

Then:

1. Start the game.
2. Open **Options > Picture** and check that **Texture pack** says **On**.

That row turns the pack on and off at once, so you can compare. It says
**None installed** if the game found no pack. Anything the pack does not
cover stays as the game draws it.

## Other places a pack can go

The game also looks in a `textures` folder in the same folder as your
saves ([where that is](PORTABLE_MODE.md#where-your-saves-are)). For
example, on Windows (type the start of it, `%APPDATA%\ico-pc\ico-pc`,
into the address bar of a File Explorer window and press Enter):

```
%APPDATA%\ico-pc\ico-pc\textures\SCES-50760\replacements\
```

On a Mac, make the folders here:

```
~/Library/Application Support/ico-pc/ico-pc/textures/SCES-50760/replacements/
```

On an iPhone, make them in the game's Documents folder, the same folder
the disc image goes in: `textures/SCES-50760/replacements`.

In portable mode, that folder is `userdata` beside the program, so a pack
can also go in `userdata\textures\SCES-50760\replacements\`.
[About portable mode](PORTABLE_MODE.md#keep-everything-in-one-folder).

## Check that the game found it

The game's log file, `ico-pc.log` ([where to find it](TROUBLESHOOTING.md#the-log-file)),
has a line for each folder it read, such as:

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
graphics card unpacks. If your graphics card cannot do this, those
pictures are skipped and the log says so:

```
textures: the graphics card has no compressed (BC) textures; the pack's DDS files of that kind are skipped
```

## If the pack uses a lot of memory

Big packs can use a lot of memory. Three lines in `config.toml`, under
`[video]`, control how much. Close the game before you edit the file.
[How to edit config.toml](OPTIONS.md#settings-you-can-only-change-in-a-file).

These lines start with `#`, which switches them off. Delete the `#` to
change one. If the file does not have these lines, add them under
`[video]`.

- `texture_pack_budget_mb = 2048` is how much of the graphics card's
  memory, in MB, the pack may use. Textures past that stay the game's own,
  and the log says when that happens. If your card has more memory, raise
  it.
- `texture_pack_precache = true` reads the whole pack into the computer's
  memory in the background, starting with the subtitles and menus. Without
  it, the game's own subtitle can flash up for a moment before the pack's.
  Set it to `false` to read each texture only when the game first needs
  it, which uses less memory.
- `texture_pack_cache_mb = 0` is how much of the computer's memory, in MB,
  that reading ahead may use. `0` means up to half of it (on Android, an
  eighth of the phone's memory and never more than 512 MB). On a computer
  with 16 GB that is up to 8 GB. Whatever does not fit is read when the
  game first needs it.

## Make a texture pack

To get the pictures to work from:

1. On the title screen, open **Options > Extras** and choose **Export
   textures**. You do not need Developer mode for this.
2. Wait. The row changes to **Stop exporting** and the line under it counts
   the pictures it has looked at so far (many share a name, so fewer files
   are saved than pictures looked at); choose it again to stop early. When
   it ends, a message tells you how many pictures were saved. The message also comes
   if you left the menu or started playing meanwhile.

The game saves each picture of your disc as a PNG file, under the same name
PCSX2 uses, in `textures/SCES-50760/dumps` (European disc) or
`textures/SCPS-11003/dumps` (Japanese disc),
`textures/SCPS-56001/dumps` (Korean disc), `textures/SCUS-97113/dumps` (North
American disc and August 2001 preview), `textures/SCUS-97158/dumps` (June 2001
magazine demo) or `textures/SCPS-55001/dumps` (Asian disc) in the folder with your saves. For
the European disc that is about 1700 files and about 160 MB, so make sure
there is room. A file already there is kept, so running it again only adds
what is missing. The folder also gets a file named `manifest.txt` that
lists every picture of the disc, what was saved and what was left out.

Give your picture the name of the file you replace, put it in the
`replacements` folder for the same disc, and the game uses it the next time
you start it.

Many pictures on the disc share one name, so one replacement covers all of
them. A spreadsheet per disc lists every file a pack can hold, where the
picture is used, how many others share its name, and (for every disc but the
European one) whether a European pack already covers it:
[European](https://github.com/nathanialf/ico-pc/blob/main/docs/textures/europe.csv),
[Japanese](https://github.com/nathanialf/ico-pc/blob/main/docs/textures/japan.csv),
[Korean](https://github.com/nathanialf/ico-pc/blob/main/docs/textures/korea.csv),
and one for each of the other four discs (`usa.csv`, `asia.csv`, `preview-2001-08.csv` and `demo-2001-06.csv`, in the same folder)
(the columns are explained in
[docs/textures/README.md](https://github.com/nathanialf/ico-pc/blob/main/docs/textures/README.md)).

### Pictures the export cannot make

The export saves what the disc holds as pictures. It cannot save:

- the fog, glow, blur and depth-of-field pictures the game makes while it
  runs;
- the movies;
- the later colours of the textures whose colours cycle (only the first
  colours are on the disc);
- names that combine several sizes of one texture.

For those, play with the game saving them as they appear:

1. Open **Options > Developer** and turn on **Developer mode**.
2. Turn on **Dump textures**, the row that appears under it.
3. Play to the place where the texture appears.

The game saves them in the same folder, under the same kind of name. The
`manifest.txt` repeats this list.

You cannot earn achievements while Developer mode is on, so turn it off
again when you are done. Achievements stay off until you go back to the
title screen.
