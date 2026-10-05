# Legal notes

What this repository may contain, what it must never contain, and which
references its source was written from.

This is a community decompilation project. It is not affiliated with,
endorsed by, or sponsored by Sony Interactive Entertainment Inc. (formerly
Sony Computer Entertainment Inc.), Sony Group, Team Ico, or any of their
affiliates. *ICO*, *PlayStation 2* and *Team Ico* are trademarks of their
respective owners.

## What is in this repository

- C source written for this project: the game's source under `ico2/`, which
  the host compiler builds into the PC port, and the port's own code under
  `port/`. (The retired PS2 build compiled the same game source with the
  period toolchain into a boot ELF; `docs/BUILDING.md`, appendix.)
- Build scripts and tooling written for this project.
- Documentation written for this project.
- Configuration that describes how the user's own disc image is checked and
  how the data tables are read: the link order (kept as the source list), the
  linker script, the data-table index, the data-table schema (record types
  and counts, no values) and the SHA-1s of the expected files.
- `config/ico.pal.yaml`, `config/symbol_addrs.pal.txt` and
  `config/symbol_addrs.pal.data.txt`, kept as a record of the old split
  layout; nothing in the build reads them. The two symbol lists hold the
  names this tree defines and the addresses the link places them at, the
  same facts `nm` prints over `build/ico.elf`; they are a record of the
  rebuilt ELF, not a copy of a disc file.

## What is not in this repository, and must never be added

- The ICO disc image (`.bin`, `.iso`, `.cue` or any other form), in whole or
  in part.
- The boot ELF of any revision (`SCES_507.60`, `SCUS_971.13`, the
  prototypes), or any part of it.
- Audio, images, textures, models, text or other assets from the disc.
- The game's data tables (next section).
- Symbol tables, map files or other identifier tables, and any debug string,
  asset or code blob, copied verbatim or in bulk out of the binary, the disc
  or any leaked development material. Using an individual fact (a name, an
  offset, a `__FILE__` path) as a reference is covered under "Identifiers as
  references" below; this item forbids copying a table of them into the
  repository.
- Any code believed to derive from leaked source.

`.gitignore` and `tools/check_no_rom.sh` (run by the pre-commit hook) make it
harder to commit such material by accident. They do not replace care. If you
suspect a file in this repository breaks these rules, open an issue tagged
`legal`.

## The data tables are loaded at run time

The game's archive `ico2000.a` has 70 members that have data sections and no
code (MAIN.MAP), plus three data runs MAIN.MAP does not list;
`config/data_members.pal.txt` lists them all. They are the game's content:
stage object layouts, model and motion file tables, sound definitions, way
points, the staff roll. There is no program in them to re-derive, and their
values are never committed, as C, as assembly or in any other form.

The host binary holds none of their bytes. It defines each of the 73 tables
as an empty array (`port/data/gen/table_defs.c`), and at start-up the
runtime loader (`port/data/tables.c`, docs/port/DATA.md, "The data tables")
fills them from the boot ELF on the user's own disc, checked against the
committed manifest of CRCs (`config/tables_manifest.txt`). The committed
descriptors under `port/data/gen/` (`tools/gen_data_desc.py`), the schema
(`config/data_schema.pal.txt`) and the record types in the owners' headers
hold only types, element counts, offsets and symbol names. The configuration
files hold member names, address ranges, record types and the names MAIN.MAP
gives the symbols, and nothing of the tables' content. The retired PS2 build
instead generated each member as `build/data/<member>.c` from the user's own
`baserom/pal/baseelf.elf` (`tools/gen_data_c.py`, still the reference the
loader's test compares against); that output is gitignored and never
committed.

The line drawn is between content and facts about its shape. The names,
tables and strings are content and are never committed. A count or size
derived from a table is an ordinary fact about it, as its element count is:
the staff roll's line count, `staffRollNameDataNum`, is the table's element
count less its two null entries, recorded in the schema and written by the
generator as `sizeof` over the table, not read from the disc.

## Public reverse-engineering material (allowed as references)

Published reverse-engineering material may be consulted as a reference for
naming and structure: conference talks, blog posts and articles, public
Ghidra projects with clear non-leaked provenance, and academic papers on the
PS2 EE / R5900. Treat them like academic papers: read, understand, then
re-derive from the disassembly. Never paste their code, comments or symbol
names into this repository.

Public source code under an open licence is used the same way. Sony's
libraries in `sce/` were written from the shipped instructions; their header
names follow the SDK's public naming, and each declaration is the signature
of the member under `sce/` that defines it; the kernel calls in `eekernel.h`
follow the public ps2sdk `kernel.h` (each header's opening comment says
which). The newlib and libgcc members follow newlib's and GCC's published
source, whose licences permit it.

## Identifiers as references: facts, not expression

Facts observable in a copy you legally own (function and symbol names, struct
field offsets and sizes, array strides, `__FILE__` paths in `.rodata`) are
not copyrightable expression. They may inform original code: read,
understand, re-derive. Never commit the source artifact, and never paste a
table of identifiers in bulk.

- **Allowed:** reading the legally owned binary or disc, or a public decomp of
  clean provenance, and re-deriving names and shapes into your own code.
- **Forbidden:** committing the disc image, the extracted ELF, asset bytes, or
  a symbol, map or debug table in whole or in bulk; and any use of leaked
  source, leaked SDKs, or leaked prototype or debug builds (below). A retail
  disc you own is never in that category.

## Forbidden inputs

These may never be used as inputs to this project, even indirectly:

- Leaked SDKs (Sony Pro-DG, internal Sony tools, debug PS2 firmware).
- Leaked source code from any party, including any of ICO or other Team Ico
  titles, however obtained.
- Paid asset extractors that bundle game data.

If you have ever read any of the above, say so on your first pull request so
your contributions can be reviewed for provenance.

## Retail PAL disc symbol maps (`main`)

The PAL retail disc (`SCES-50760`, boot ELF `SCES_507.60`, `SYSTEM.CNF`
`VER = 1.00`) ships three build artifacts next to the boot ELF, which the USA
disc does not:

- `MAIN.MAP`: a GNU ld linker map (member load order and symbol table).
- `SRCFILE.TXT`: an `objdump -dl` listing of a slightly earlier link of the
  same program (`20020116MasterVer1.00EU`): every instruction with its
  function label and the source path and line number it came from. It
  contains no source statements.
- `TRFILE.TXT` / `TRTABLE.BIN`: an address-to-function/file/line table.
  `TRFILE.TXT` has one text line per address (111515 lines);
  `TRTABLE.BIN` is a binary index of 111515 eight-byte entries, each an
  address from `TRFILE.TXT` and a 32-bit offset.

These are officially distributed retail media that every buyer received.
Symbol names, file boundaries and file paths read out of them are used as
references in the source tree, the same way as any other fact in the disc.
The listing's line data was used to recover names, file boundaries and the
order of functions in each file. It is not a licence to reconstruct source
statements, and it contains none. `tools/extract_elf.sh` copies `MAIN.MAP`,
`SRCFILE.TXT`, `TRFILE.TXT` and `SYSTEM.CNF` into gitignored `baserom/pal/`;
it does not copy `TRTABLE.BIN`, which nothing in the build reads. None of
them is ever committed or bulk-copied. Because the listing comes from a
different link than the shipped ELF, names were attached to shipped
functions by instruction-stream correlation, never by copying an address.

## Prototype symbol maps (`aug6`)

The `aug6` branch builds the August 6, 2001 ICO prototype (`SCUS_971.13`).
That prototype disc shipped a `MAIN.MAP` and a `SRCFILE.TXT` of its own. The
project uses the factual metadata in them (names, addresses, file boundaries,
`__FILE__` paths) on the same terms as the retail maps above. This rests on
common decompilation practice for symbol information present in a
distributed compiled binary: the Ocarina of Time decomp builds the
symbol-bearing GameCube debug ROM, the Paper Mario TTYD decomp seeds names
from a demo-disc symbol map. A review prototype that circulated through
collectors is closer to a grey area than retail media; using its metadata is
a deliberate choice of this project, not settled community consensus. Leaked
source and leaked SDKs stay forbidden on every branch.

## What you need to build

You must legally own a copy of *ICO* for the PlayStation 2 and supply the
disc image yourself. The build checks the extracted ELF's SHA-1 before it
proceeds and refuses any other file (`config/sha1sums.txt`,
`docs/BUILDING.md`). A SHA-1 is a fingerprint, not a copy of the work; the
disc image and the ELF remain the property of their rightsholders.

This project gives no instructions for obtaining a disc image. The only
legitimate source is a personal dump of a disc you own, made in compliance
with the law where you live.

## Why decompilation projects can exist

Projects of this kind have a long history (Super Mario 64, Ocarina of Time,
Majora's Mask, Paper Mario, Sly Cooper, Jak and Daxter, Kingdom Hearts and
many more). They produce original source code by reverse engineering,
distribute no copyrighted assets, and require users to bring their own image.
This project follows the same model.

If you represent a rightsholder and have a concern, please open a GitHub
issue.
