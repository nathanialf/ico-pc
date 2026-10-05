# Saves

The game's memory card code (`ico2/fumi/ios/mcard.c`, unchanged) calls
`sceMc*`. `port/save/mc_host.c` implements that subset over a host folder:
one formatted 8 MB card in slot 0 of port 0, whose files hold exactly the
bytes the game wrote. Port 1 is empty.

## Where the card is

The card folder is `<pref folder>/memcard` by default (`ico_host_saves_dir`
in `port/platform/host_config.c`; the pref folder is described in
docs/port/CONFIG.md). `[paths] saves` in `config.toml`, or `saves=` in
`ico-pc.ini`, moves it; a relative path is taken from the executable's
folder. The folder is resolved once, at the first `sceMcInit`, and is not
created by looking at the card: the game's first save (`sceMcMkdir`) makes
it and any missing parent, so a boot with no save writes nothing.

Names the host cannot hold (`\ : * ? " < > |`, control characters, longer
than 31 bytes) are refused as "no entry". Dot files (`.DS_Store`, the
port's own temporary files) are not shown to the game. Host paths are
UTF-8 through `port/platform/host_fs.h` (docs/port/DATA.md, "Paths").

## Layout

```
<saves>/
  BESCES-50760ico/
    icon.sys          copied from the disc as the game writes it (iosMcIconWriteIconsys)
    boy_blk.ico       the icon, likewise (the three names in mcard.c are the same file)
    BESCES-50760ico   the product (system) file: McProductFile + 4-byte sum
    game.000 ...      one file per save slot (up to 10): 25588-byte save block
                      + optionScreenMode + girlControlMode + 4-byte sum
```

`McProductFile` is 0x1F0 bytes (`typedef.h`; class `save` in
`config/struct_classes.txt`): twenty save previews (`McFileInfo`, 20 bytes
each), then the sound, output, vibration and control options, the 64 bytes
of the custom pad map, `fileNo`, `serial`, camera mode and PAL mode. The sum
is the byte sum of what precedes it, as a little-endian int. A game save is
a block of the save buffer, `25588 + 4 + 4` bytes plus the sum, so
`game.NNN` is 25600 bytes. The icon files have no sum.

`sizeof(McProductFile)` is frozen. The port adds no fields to card files:
port settings (mirror mode, key bindings, audio, video) live in
`config.toml`, so a card folder stays interchangeable with a PS2's.

Files are plain host files. `sceMcOpen` with `0x203` (read/write, create)
does not truncate, as on the card, so re-saving the same data rewrites the
same bytes; `SCE_TRUNC` is honoured.

Writes are atomic per file. A handle opened for writing works on a copy,
`<dir>/.<name>.tmp` (the file's bytes copied in unless `SCE_TRUNC`; hidden
from `sceMcGetDir` like every dot file), and `sceMcClose` moves it over the
file (`ico_rename_replace`: `rename(2)`, or `MoveFileExW` with
`MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`). A crash, a full disk
or a failed write during a save leaves the previous file whole; in that
case `sceMcClose` returns `sceMcResFullDevice` and logs it. A file created
by the open exists, empty, from the open, as on the card.

### PCSX2 folder cards

PCSX2's folder memory cards keep each save as a directory of plain files in
the same layout, so a save can be copied between
`<card>/BESCES-50760ico/` and `<saves>/BESCES-50760ico/` in either
direction. The extra index files a folder card keeps are harmless: the game
never asks for them, they count towards the free space, and `sceMcGetDir`
with `*` lists them. (Their names, `_pcsx2_superblock` and `_pcsx2_index`,
are from PCSX2's format and are not checked anywhere in this tree.)

Raw card images (`.ps2`, `.bin`) and `.psu`/`.max`/`.cbs` save archives are
not read. Convert them to a folder card first (PCSX2 can export a raw card
to a folder card, or a card tool can extract a save) and copy the
`BESCES-50760ico` directory in. Only the PAL title ID (SCES-50760) is used.

## Asynchrony

Like `port/data/cdvd_host.c`, a request does its work at the call and
completes at the next simulated vsync (`ico_host_on_vsync_register`).
`sceMcSync(1)` returns 0 until then, then 1 with `*cmd` set to the function
number (`sceMcFuncNo*`, `libmc.h`) and `*result`; it returns -1 when nothing
is pending. `sceMcSync(0)` returns the result at once. A new request
replaces an unclaimed one (the game always syncs first).

The vsync also signals the game's `IosMcLock` semaphore. `iosMcMgrSync`
creates it with count 0 and loops `WaitSema; sceMcSync(1)`; with nothing
signalling it, the manager thread would wait forever. The signal makes each
poll one vsync apart, and the same loop with nothing pending (`mcdata.c`'s
icon copy) becomes a once-per-vsync sleep.

Each request takes at least one vsync, and a save is a few dozen requests,
so a save completes in well under a second. The PS2 card's real pacing (a
save takes seconds) is not modelled; nothing in the game depends on it.

## Result codes

These are `libmc.h`'s `sceMcRes*`, as the PS2 libmc documents them. "Game"
is what `mcard.c` or `layout_action.c` does with the code.

| result | name | returned by | game |
|---|---|---|---|
| 0 | Succeed | all | ok |
| -1 | ChangedCard | `GetInfo`, the first call after `sceMcInit` | treated as 0 (`iosMcMgrChdirProduct`); stored in `cardState` |
| -2 | NoFormat | every call after `sceMcUnformat`, until `sceMcFormat` | `iosMcMgrChdirProduct` returns it; `_la_mcard_error_check` "unformatted" |
| -3 | FullDevice | `Write`, `Mkdir`, `Open` (create), `Close` when the host write fails | "memory card another err" (-2 from the check) |
| -4 | NoEntry | `Chdir`/`Open`/`Delete`/`GetDir` of a missing path or a bad name; `Mkdir` of an existing one | `Chdir`: mapped to -14; `Mkdir`: tolerated; load of a missing slot: "file not found" |
| -5 | DeniedPermit | bad handle, write on a read-only handle, open of a directory | "another err" |
| -6 | NotEmpty | `Delete` of a directory with entries | "another err" |
| -7 | UpLimitHandle | a fourth open file | "another err" |
| -9 | FailDetect | any call but `GetInfo` on port 1 | "not insert memory card" |
| -10 | FailDetect2 | `GetInfo` on port 1 | `iosMcMgrGetInfo` stores it; `iosMcMgrChdirProduct` maps it to -9; type 0 takes the no-card exit |

Calls that return counts: `Open` returns the handle (0 to 2), `Read` and
`Write` the bytes, `Seek` the new position, `GetDir` the entries filled.
The game adds its own values on top: -14 (no product directory), -15
(handler error or end of file), -16 (checksum mismatch).

`sceMcGetInfo` reports type 2 (a PS2 card), format 1, and the free
clusters: `ICO_MC_HOST_CLUSTERS` (8000) less the 1 KB clusters the card
folder holds (a file's size rounded up, one per directory). The game wants
360 free to save. Port 1 reports type 0, format 0, free 0.

`sceMcGetDir` takes `*` and `?` wildcards, lists a subdirectory with `.`
and `..` first, sorts by name, and reports `AttrFile` 0x8497 (file) or
0x8427 (directory), the file size and the host modification time (UTC).
The game asks for `game.*` in the product directory and reads the slot
number from the last three characters of each name. `Delete` and `Open`
are relative to the current directory the last `Chdir` set, as in libmc;
`iosMcDelete` relies on that. `Format` never deletes anything (the folder
may hold other games' saves); `Unformat` only changes what `GetInfo`
reports.

## First run and later runs

On the first run the card is formatted and has no save directory.
`kanbanBootMcCheck` (`kanbanBoot.c`) gets type 2, formatted, 8000 free, and
result -14 from `iosMcChdirProduct`, and goes on to the language and
50/60 Hz steps, which the port answers from the config without showing the
screens (docs/port/CONFIG.md, "Language").

After a save, `iosMcChdirProduct` returns 0, the boot loads the product
file (`iosMcLoadProductBlock`) and applies its camera and PAL mode, unless
the config names them. The load and save menus list `game.NNN` through
`iosMcGetBlockSaveInfo`.

The save UI's order of calls (`layout_action.c`) is `GetBlockSaveInfo`,
`SaveIconBlock` (`icon.sys` and one `boy_blk.ico`, with the directory
made), `SaveProductBlock`, `SaveGameBlock`.

## Mirror mode

Mirror mode (docs/port/OPTIONS.md) is chosen at New Game and belongs to the
run. It is kept per save slot in the port config, never in the card files,
which stay the bytes the game writes:

```
[mirror]
slot_3 = true
slot_3_sum = 1834213
```

- The run's value is a session value (`ico_opt_mirror`) until a save
  happens. After New Game it is what the player picked; at the title it is
  `[gameplay] mirror` (normally Off).
- Saving to slot N (`la_save_processing` in `common/src/layout_action.c`,
  after the game block was written and checked) calls
  `ico_mirror_slot_saved(mc.fileNo, mc.sum)`, which writes `slot_N` as the
  run's value and `slot_N_sum` as the checksum `mcard.c` computed over the
  block (the 4 bytes after it in `game.00N`), and saves `config.toml`.
- Loading from slot N (`la_load_processing`, after the block was read and
  its checksum matched) calls `ico_mirror_slot_loaded(mc.fileNo, mc.sum)`,
  which sets the run's value from `slot_N` when `slot_N_sum` equals the
  loaded block's checksum, and Off otherwise.
- N is the save file's number (`mc.fileNo`, 0 to 9, `game.000` to
  `game.009`). The port has one card, so the port number is not part of the
  key.

The checksum is what ties an entry to a particular save:

- Saving over a slot that held another run's save replaces the entry.
- A save made on a PS2, copied in from a PCSX2 folder card, or written by
  the card's initialisation (`la_system_save_processing` fills empty slots)
  has no entry, or one with another sum, and loads as Off.
- A slot deleted in the game's menu keeps its stale entry; the next save
  there rewrites it, and a load could only match it if the same block came
  back.
- Another `saves=` folder shares the entries; the checksum makes a
  mismatch read Off.
- A cleared save's "New Game" (`la_load_processing` returns to the
  vibration screen when gflag 395 is set) first sets the slot's value, then
  the Mirror mode screen resets it and asks again.

## Tests

`port/save/test/save_test.c` (ctest `save`):

- The `sceMc` calls by hand: first boot (info, chdir -4, getdir 0, nothing
  on disk), the first save, reload, re-save (byte-identical, no
  truncation), truncation, the handle limit, bad names, `..` stopping at
  the root, delete, unformat and format, and port 1.
- `mcard.c`, `thread.c`, `message.c` and `memory.c` compiled unchanged with
  the game's options on the scheduler, `iosMcManager` running as a thread,
  driven through `iosMcChdirProduct`, `iosMcGetBlockSaveInfo`, `iosMcSave*`,
  `iosMcLoad*` and `iosMcDelete`. The bytes written for an all-zero game
  (500 bytes for the product file, 25600 for the game block) and for a game
  with every option set are checked, with the sum, against a layout written
  out by hand. Reload restores the options and the block, the re-save is
  byte-identical, a flipped byte gives -16, a missing slot -4, port 1 -9.
  `mcdata.c` (the icon copy from the disc) is replaced by a pattern writer.

The mirror slot entries are tested in `port/game/test/options_test.c`
(ctest `options`): the round trip through the file, a missing entry and a
wrong sum reading Off, saving over with another value, and the listener.

Open items for saves are in docs/TODO.md.
