# Headers

Which headers under `ico2/` the disc attests, which source files are compiled
as part of another, and where every other declaration lives.

The evidence is the PAL disc's January 2002 listing (`SRCFILE.TXT`, an
`objdump -dl` of the game with a source path and line on every instruction)
and its `MAIN.MAP`; both are copied into `baserom/pal/` by
`tools/extract_elf.sh` and are never committed (`docs/LEGAL.md`). The listing
contains no source text. A header shows up in it only when code from the
header was compiled into a caller: a header that only declares leaves no
rows at all.

## Headers the disc attests

Eleven headers under `ico2/` are known from the disc. Each sits at the path
the listing records for it, under the programmer directory that owns it.

| header | evidence | what it holds |
| --- | --- | --- |
| `ico2/sugipon/include/sugiCommon.h` | listing rows in 84 caller functions in six directories (`sugipon` 63, `fumi` 11, `ito` 5, `common` 3, `omori` 1, `script` 1) | nine `static` inline helpers: random numbers, plane distance, squared distances, a byte checksum |
| `ico2/ito/include/itou_common.h` | listing rows in five `ito` functions (`DrawLightning2`, `GatherEffect_Proc`, `QueenBarrierGeo`, `subBirdBrainMain`, `vector_angle_degree`) | degree and radian conversion |
| `ico2/ito/include/mv_defs.h` | inlined expansions in the movie player, and three out-of-line copies of `Free` | address masks and a zeroing allocator; `Free`, its release, is defined in each of `mv_vibuf.c`, `mv_videodec.c` and `mv_vobuf.c` |
| `ico2/common/include/typedef.h` | one helper at line 74, inlined twice into `avoid_obstacle2` | the engine's shared object records (`GObj`, `Sub15C`, `Act`, ...); in the decompilation also the game's VU0 asm templates, which the port replaces with `port/math` |
| `ico2/omori/include/{b50,b100,b200}climb.h` | whole functions, emitted into `fumi/src/boyact.c`'s object | the boy's climb handlers |
| `ico2/omori/include/{g50,g100,g200}climb.h` | whole functions, emitted into `fumi/src/girl_act.c`'s object | the girl's climb handlers |
| `ico2/common/include/charFileName.h` | no listing rows; the ROM's message at `0x00619370` names `commmon/include/charFileName.h` (the typo is the message's) | `MAX_CHARS` (1637), the size of `charFileManager.c`'s character-file table and the bound its four id checks compare against |

Things a reader of these files should know:

- **Helper names.** An inlined helper leaves no symbol, so its name is the
  developer's only when the listing shows it emitted out of line somewhere.
  That holds for `Free` and for the eighteen climb functions
  (`after*Hand*`, `act*Hand*`, `mot*Hand*`). Every other helper name is a
  descriptive one, marked as described under "Derived names" below.
- **Two pairs of identical helpers.** The listing cites line 55 and line 65
  of the developer's `sugiCommon.h` for the same instructions (the
  `* 2.0f - 1.0f` of a -1..+1 random number), and lines 85 and 87 and line 97
  for the same VU0 squared distance. Both of each pair are written out:
  `random_signed` (line 55) and `random_signed_b` (line 65), and
  `distance_squared` (lines 85 and 87) and `distance_squared_b` (line 97).
  The tracked file's line numbers are not the listing's.
- **Line numbers that are part of the bytes.** `mv_defs.h`'s allocator bakes
  `__FILE__` and `__LINE__` into the ROM (`"../ito/include/mv_defs.h"`, lines
  43 and 44), and `typedef.h`'s helper sits on line 74. That is a constraint
  of the decompilation's byte-matched build; the port's copies record it but
  do not depend on it.
- **The climb headers hold no bodies.** ee-gcc 2.9 emits ordinary functions
  in parse order and `inline` ones at the end of the file. In each includer
  the `mot*` function sits in the parse-order run and the `act*` and
  `after*` functions in the end-of-file run, an order one `#include` cannot
  give. The eighteen functions are written in `boyact.c` and `girl_act.c`
  at the point the headers would be included, under a comment naming the
  three headers, and each header names the functions written in its place.

## Code includes (`.c.inc`)

The listing also records source files compiled as part of another file.
Each is a `.c.inc` beside its includer:

| file | included by |
| --- | --- |
| `ico2/common/src/debug_exception_screen.c.inc` | `debug_exception.c` |
| `ico2/fumi/src/girl_act_hand.c.inc` | `girl_act.c` (inside `actGirlHand`) |
| `ico2/fumi/src/girl_brain_attract.c.inc` | `girl_act.c` |
| `ico2/sugipon/src/motMan_getFinalMatrix.c.inc` | `motionManager.c` |
| `ico2/sugipon/src/motMan_rootUpdate.c.inc` | `motionManager.c` |
| `ico2/sugipon/src/switch.c.inc` | `box.c` |

The listing names one more, `girl_brain_main.c.inc`, which includes
`girl_brain_attract.c.inc`. Its functions fall into three separate stretches
of `girl_act.o`'s emission order, so its text stays inside `girl_act.c` at
the point the listing puts it, and its assert strings still name it
(`"src/girl_brain_main.c.inc"`).

## Headers placed by this project

**One header per game file.** The other 217 headers under
`ico2/<programmer>/include/` are each named after the source file whose
definitions they declare (`gobj.h` for `isys/gobj.c`, `switch.h` for
`switch.c.inc`); the disc records none of them. They hold one prototype
per function and one `extern` per object, with the types the definitions
and their callers use. Shared records are
defined in the owner's header or, for the engine's records (`GObj`,
`Sub15C`, `Act`, ...), in `typedef.h`. A few files declare their own view of
a shared record under a comment saying so (`st04a.c`'s `Act` and `GObj`,
`item.c`'s `ClipWork`). Records only one file uses stay in that file.

**Sony's and newlib's headers.** The headers under `sce/<archive>/` carry the
SDK's public header names (`eekernel.h`, `libgraph.h`, `libdma.h`, ...) and
newlib's (`stdio.h`, `math.h`, ...). Each declaration is the signature of the
member under `sce/` that defines the symbol; `eekernel.h`, whose calls the
kernel defines, gives the public signatures of ps2sdk's
`ee/kernel/include/kernel.h`. Each header's opening comment says which.
Files named `*_internal.h` hold declarations that are not public API, and their names
are this project's, as is `sce/libsndn2/sound.h`, named after its member
`sound.o`; the movie player's audio decoder takes the stream PCM calls from
it. The listing attributes no rows to `/usr/local/sce/ee/include`, so no SDK
header compiled code into the game.

## Derived names

The disc records no field names and no names for inlined helpers. A name
derived from context instead of read from the disc's maps is marked with a
`/* derived name */` token on its definition line. A record of the game
whose field names are derived carries one `/* field names derived */` token
on its opening line rather than one per field. Sony's public types keep
their public names.

## Search order

`tools/compile_c.sh` compiles each game file from inside its programmer
directory, with the programmer's own `include/` first and then
`-I../sugipon/include -I../omori/include -I../common/include
-I../ito/include -I../fumi/include -I../seki/include -I../script/include`,
then the `sce/` archive directories. The relative spellings are the ones the
ROM's `__FILE__` strings record. Sony's members compile from inside their
own directory by bare file name, for the same reason.

The CMake build (Ninja) tracks header dependencies through compiler
depfiles, so after editing a header `cmake --build` recompiles what includes
it; no clean step is needed. (`tools/compile_c.sh`, the period-compiler
spelling of the search order, belongs to the optional EE check in
docs/BUILDING.md's appendix.)
