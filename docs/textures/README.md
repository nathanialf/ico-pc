# Texture sheets

One spreadsheet per disc, for people who make texture packs: one row per
picture file a pack can hold.

- `europe.csv` for the European disc (SCES-50760)
- `japan.csv` for the Japanese disc (SCPS-11003)
- `korea.csv` for the Korean disc (SCPS-56001)
- `usa.csv` for the North American disc (SCUS-97113)
- `asia.csv` for the Asian disc (SCPS-55001)
- `preview-2001-08.csv` for the August 2001 preview disc (no product number)
- `demo-2001-06.csv` for the June 2001 magazine demo (SCUS-97158)

Columns:

- **file**: the name of the replacement file, as it goes into
  `textures/<disc>/replacements`. Options > Extras > Export textures on the
  title screen saves the game's own picture under this name in
  `textures/<disc>/dumps`.
- **size** and **format**: the picture's size in pixels and how many colours
  it holds.
- **first used by**: where the picture sits on the disc (a stage or character
  folder, a menu sheet, a subtitle file).
- **where**: every place that uses the picture.
- **also used by**: how many other pictures on the disc carry the same name.
  One file replaces all of them.
- **in the European pack** (every sheet but the European one): `yes` when the same
  file is in a pack made for the European disc, so that pack already covers
  it; `no` when the picture is only on this disc.
- **notes**: a smaller copy of a bigger picture (the game uses several sizes
  of some textures, each a file of its own); a picture whose colours cycle
  (only its first colours are on the disc); a picture with no name, which
  cannot be replaced.

What the sheets and the export cannot cover: the fog, glow, blur and
depth-of-field pictures the game makes while it runs, the movies, and the
later colours of the pictures whose colours cycle. Dump textures under
Developer mode saves those while you play.

The sheets are generated from the disc by `tools/texture_catalog.py`
(see docs/port/PACKS.md).
