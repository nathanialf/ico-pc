# Saves: memory cards

How the port keeps the game's saves: [libmc](ARCHITECTURE.md#terms) answered from a folder on the
host in the layout PCSX2's folder memory cards use, a second card in port 1 when one is configured,
where those folders are on each platform, each disc's save directory and record, and the importer
that copies an existing PS2 save into a card folder. The game's own card code
(`ico2/fumi/ios/mcard.c`, `mcdata.c`) runs unchanged but for its region forms.

## Files

| path | what it holds |
| --- | --- |
| `port/save/mc_host.c` | libmc over a host folder: the `sceMc*` functions the game calls |
| `port/save/mc_host.h` | the card folders of the two ports (each holding every disc's directory), the pending-request query, the card's size |
| `port/compat/libmc.h` | the host's libmc header: the directory entry, the result codes, the function numbers, the calls |
| `port/save/mc_import.h`, `mc_import.c` | the importer: a raw card image or a `.psu` archive into a card folder |
| `tools/mc_import.c` | `mc_import`, the command-line importer |
| `port/platform/host_config.c` | `ico_host_saves_dir`, `ico_host_saves2_dir`: where the card folders are |
| `port/save/CMakeLists.txt` | `ico_save` (the card), `ico_save_import` (the importer), the `mc_import` tool, the `save`, `save_jpn`, `save_kor`, `save_cn`, `save_ntsc`, `save_aug6` and `save_jun26` tests |
| `port/save/test/save_test.c` | the same tests |
| `port/core/region_<slug>.c` (seven rows) | each disc's save directory (`mc_save_dir`) and product record size (`mc_product_size`) |
| `ico2/fumi/ios/mcard.c`, `mcdata.c` | the game's side: the card manager thread, the save and load steps, the icon files |

## The card

Port 0 always holds a formatted 8 MB card whose root is the card folder; port 1 holds a second card
only when a folder is configured for it, and is empty otherwise, as on a PS2 with one card
(`port/save/mc_host.c`, header comment, `slot_ok`). Slot 0 is the only slot. `sceMcGetInfo`
reports a PS2 card (type 2), formatted, with its free space in 1 KB clusters: 8000 less the
clusters the folder's tree takes, each file rounded up to whole clusters and each directory one
cluster (`port/save/mc_host.c` (`sceMcGetInfo`, `used_clusters`); `port/save/mc_host.h`
(`ICO_MC_HOST_CLUSTERS`)). The first `sceMcGetInfo` after `sceMcInit` answers
`sceMcResChangedCard` once, a card freshly inserted, as the game's card check expects
(`port/save/mc_host.c` (`sceMcInit`)).

The files are stored byte for byte as the card holds them, in PCSX2's folder-card layout:
`<card>/<save directory>/` holding `icon.sys`, `boy_blk.ico`, the product file named after the
directory and the save files `game.000` onward, the names the game itself uses
(`port/save/mc_host.c`, header comment; `ico2/fumi/ios/mcard.c` (`iOSMcSaveSeg`)). The directory
is the disc's (see [The seven discs](#the-seven-discs)): `BESCES-50760ico` for the European disc,
`BISCPS-11003ico` for the Japanese one, `BISCPS-56001ico` for the Korean one, and one for each of
the other four discs (the table below). The host names no directory and checks no file's size; it
stores what the game writes. Nothing else is written, and nothing at
all before the game's first `sceMcMkdir`: until then a card folder that does not exist is an
empty card, and `sceMcMkdir` creates the card folder and any missing parent
(`port/save/mc_host.c` (`sceMcGetDir`, `sceMcMkdir`, `make_root`)). The PC-only facts about a
save, the mirror-mode flag of each slot and the achievements' run state, live outside the card,
in `config.toml`'s `[mirror]` and `achievements.toml`, keyed by the slot and the save block's
checksum, one set of keys per disc ([The seven discs](#the-seven-discs), [CONFIG.md](CONFIG.md);
`port/game/options.h`, `ico_mirror_slot_saved`; `port/game/achievements.h`,
`ico_ach_slot_saved`).

Names follow the card's rules: at most 31 bytes, none of `\ : * ? " < > |` or a control
character, at most 8 directories deep; `.` and `..` resolve against each port's current directory
(`port/save/mc_host.c` (`valid_name`, `resolve`)). A subdirectory's `sceMcGetDir` lists `.` and
`..` first, as a card does, and the entries are sorted by name; a pattern takes `*` and `?`
(`port/save/mc_host.c` (`sceMcGetDir`, `list_dir`, `wild`)). Dates in the directory entries are
the host file's modification time in UTC (`port/save/mc_host.c` (`fill_entry`, `fill_date`)).

On Linux and on Android's app folder, where the file system tells case apart, a path component
that does not exist as spelled takes the name of the one entry in its directory that differs only
in ASCII case, so a card folder copied in as `besces-50760ICO` (or `biscps-11003ICO`) is still
the game's; Windows and
macOS ignore case themselves (`port/save/mc_host.c` (`fold_case`)).

`sceMcFormat` never wipes the folder, which may hold other games' saves; `sceMcUnformat` only
marks the port unformatted until the next `sceMcFormat` (`port/save/mc_host.c` (`sceMcFormat`,
`sceMcUnformat`)).

## The seven discs

Each disc saves into its own directory on the card, as on a PS2, so one card folder holds the
discs' saves side by side and no disc sees another's (`port/core/region.h`
(`mc_save_dir`); `ico2/fumi/ios/mcard.c` (`iOSMcSaveSeg`, the product path)):

| disc | directory | product file | `game.00N` |
| --- | --- | --- | --- |
| European, SCES-50760 | `BESCES-50760ico` | 0x1F0 bytes and a 4-byte sum, 500 bytes | 25,600 bytes |
| Japanese, SCPS-11003 | `BISCPS-11003ico` | 0x1E8 bytes and a 4-byte sum, 492 bytes | 25,600 bytes |
| Korean, SCPS-56001 | `BISCPS-56001ico` | 0x1E8 bytes and a 4-byte sum, 492 bytes | 25,600 bytes |
| Asian, SCPS-55001 | `BISCPS-55001ico` | 0x1E8 bytes and a 4-byte sum, 492 bytes | 25,600 bytes |
| North American, SCUS-97113 | `BASCUS-97113ico` | 0x18C bytes and a 4-byte sum, 400 bytes | 25,600 bytes (25,596-byte block) |
| August 2001 preview | `BASCUS-97113ico` (the North American disc's name, in the card folder `memcard-preview`) | 0x18C bytes and a 4-byte sum, 400 bytes | 25,600 bytes (25,596-byte block) |
| June 2001 demo, SCUS-97158 | `BISCUS-97113ico` | 0x18C bytes and a 4-byte sum, 400 bytes | 20,484 bytes (20,480-byte block) |

The product file is the game's `McProductFile` record (`ico2/common/include/typedef.h`): twenty
file summaries, the sound, output, vibration and control options, the pad configuration, the
current file and the serial, then on the European disc `cameraMove` (`NonLinearCameraMove`, the
language) at 0x1E8 and `palMode` (`systemStatus[0]`, 50 or 60 Hz) at 0x1EC. `controlType` is written from `optionControlType` but not read back into it: `[gameplay] hold_type`
decides (`ico2/fumi/ios/mcard.c` (`gameblock_read`), and on the 2001 discs, whose record has no
`controlType`, `gameblock_read_ntscaug6` and `gameblock_read_jun26`). The Japanese disc's
record is the same fields up to 0x1E8 without those two words (upstream's
`ico2/common/include/typedef.h`, the `VERSION_JPN` arm of `McProductFile`, aligned 8, against
the European arm's 16). The one record in memory is the European layout on every disc; the
Japanese disc reads and writes its first 0x1E8 bytes (`port/core/region_jpn.c`
(`mc_product_size`); `ico2/fumi/ios/mcard.c` (`product_write`, `product_read`)), so the port's
`BISCPS-11003ico` folder is a real Japanese save. The save file is the same on all discs: the
25,588-byte game block, then `optionScreenMode`, `girlControlMode` and the sum (upstream's
`ico2/fumi/ios/mcard.c` `gameblock_write` and `gameblock_read` carry no region block).

The Korean disc (SCPS-56001) is the Japanese program with Korean text textures, so its card is the
Japanese one under another name: the directory `BISCPS-56001ico` (`iOSMcSaveSeg_kor`), the same
0x1E8-byte product file (`McProductFile` without `cameraMove` and `palMode`), the same
25,600-byte `game.00N`, and the Japanese stage numbers and game flags
(`port/core/region_kor.c` (`mc_save_dir`, `mc_product_size`, `key_prefix` `kor_`, `key_suffix`
`_kor`)). The importer, the card host and the tests take all of this from the region's row; the
save layout is told apart by `mc_product_size`, never by the disc's slug. A Japanese save is not a
Korean one: the two directories are separate, and a save copied from one folder to the other is not
converted or checked for the other disc.

The Asian disc (SCPS-55001) is the Korean program: its card is the Korean one under another name
(`BISCPS-55001ico`, the same 0x1E8-byte record; `port/core/region_cn.c`, `key_prefix` `cn_`, `key_suffix`
`_cn`; the directory is mcard.c's `VERSION_CN` literal).

The other three discs (the North American disc, the August 2001 preview and the June 2001 demo) use a
shorter product file, 0x18C bytes (`mc_product_size`): upstream's `McProductFile` for them is twenty
16-byte file summaries (0x140), then the sound, output and vibration options and the 64-byte pad
configuration, with no `controlType`, current file, serial, `cameraMove` or `palMode` (upstream's
`ico2/common/include/typedef.h`, its `VERSION_NTSC`/`AUG6`/`JUN26` arm). Each file summary is four
words on all three discs: the stage, the play time in frames, the sofa and a word no code reads.
That is how their code uses the record (upstream's 2001 `McPreview` in `layout_action.c`:
`la_playtime_count` and `la_game_loop` count `IosMcPreviewInfo[1]`, `la_save_processing` puts the
sofa in `[2]`, and the record is copied whole between `IosMcPreviewInfo` and the product file);
upstream's `McFileInfo` names for the preview and the demo (`cleared`, `playTime`, `sofa`) have no
reader. The host keeps the European `McFileInfo` on every disc and `mc_product_from_2001` /
`mc_product_to_2001` (`ico2/fumi/ios/mcard.c`) move the four words in the disc's order into its
first four, so on these discs the words named `cleared` and `playTime` hold the play time and the
sofa: `layout_action.c` reads them through `la_preview_play` / `la_preview_sofa`, and the game-state
view through the region row's `mc_preview_play` (1) and `mc_preview_sofa` (2) (the European
program's 2 and 3). Before v0.9.5 the North American disc's summaries were converted to the European
places while the game kept writing the disc's, so every save put the sofa where the play time goes
and a load restarted the count (issue 160). The North American disc and the preview write the
same directory, `BASCUS-97113ico`, but not the same records: the preview's story flags from 300 on
are one lower (New Game 348 against 349, the queen 306 against
307: `region_ntsc.c`, `region_aug6.c` `gflag_*`; upstream `layout_action.c` keepFlagNo). On one
card each would misread and rewrite the other's saves, so the port gives the preview a card folder
of its own beside the configured one (a PC port divergence): `IcoRegionInfo.mc_card_suffix`
`"-preview"` (`port/core/region_aug6.c`), applied by `sceMcInit` to port 0's and port 1's folders
through `ico_host_card_dir_beside` (`port/platform/host_config.c`), so the default is
`memcard-preview/` beside `memcard/` and `saves2 = "cards2"` gives `cards2-preview`; the game still
writes its own `BASCUS-97113ico` inside it. `sceMcInit` logs it once at boot: `saves: the preview
disc's card folder is memcard-preview (its saves are not the USA disc's): <folder>`. Their
`[mirror]` and `achievements.toml` keys stay apart too (`ntsc_`, `aug6_`). The importer never picks
the preview's row (`mc_import.c` `save_dir_region` skips a row with `mc_card_suffix`), so a card
holding that directory reports the North American disc, which is the only one a retail card can
hold; a preview save is imported by naming `memcard-preview` as the folder. save_test's rows pin the
suffix (the preview's only) and `ico_host_card_dir_beside`. The demo's directory carries the North
American game's serial, `BISCUS-97113ico` (mcard.c's literal), not the demo disc's `SCUS-97158`.

On the card these three discs write the record through `mc_product_to_2001` (`mcard.c`): the
twenty previews at 16-byte steps (`stage`, `playTime`, `sofa`, the unread word on the North American
disc; `stage`, `cleared`, `playTime`, `sofa` on the preview and the demo), the sound mode at 0x140,
the output mode at 0x144, the vibration at 0x148 and the pad bytes at 0x14C; the memory record
keeps the European layout. Their game block has no option words after it: 25,596 bytes on the
North American disc and the preview, 20,480 bytes on the demo (`gameblock_write`), then the sum.
The Players count the port offers there is kept beside the save instead, in `config.toml`'s
`[players]` ([CONFIG.md](CONFIG.md#players)).
All three ship `icon/icon.sys` (964 bytes) and `icon/boy_blk.ico` (95,624 bytes; the symbol tables'
`iconFile` entries) and save them as `icon.sys` and `boy_blk.ico`. The demo writes each file
with the 4-byte sum after it, writes its second and third `boy_blk.ico` segments as no-ops (a
listing finds the file), and never rewrites an icon file that is already on the card
(`iosMcMgrSaveSeg_jun26`, `iosMcMgrSaveIcon_jun26`). The demo's disc holds a second icon record,
`mcIconSys` (CRC 0x32533E32), that nothing reads; the tests do not touch it.
`region_select_test` holds this rule. How the merged `mcard.c` lays this record out in memory is the
game-code merge's; `save_test`'s part B, which lays the product file out byte by byte, has no run
for these three discs until that is known.

A real card's saves need no conversion: a card or archive with any of the directories imports into the
card folder as it is ([Importing a PS2 save](#importing-a-ps2-save)). There is no exporter. The
card folder is the export format: PCSX2 reads it as a folder memory card, and tools such as mymc
copy a directory from it to a card image or a `.psu`.

The PC-only facts kept beside a save are per disc too, since slot N of one disc is not slot N of
the other: `config.toml`'s `[mirror] slot_N` and `slot_N_sum` for the European disc and
`jpn_slot_N` and `jpn_slot_N_sum` for the Japanese one, `kor_slot_N` and `kor_slot_N_sum` for the Korean one (and `ntsc_`, `aug6_`, `jun26_`, `cn_` for the others), and `achievements.toml`'s `run.slot_N_*`,
`run.jpn_slot_N_*` and `run.kor_slot_N_*` (and the others) (`port/core/region.h` (`key_prefix`, `ico_region_key_prefixed`);
`port/game/options.c` (`slot_keys`); `port/game/achievements.c` (`run_key`)). The visited stages
are `stats.stages`, `stats.stages_jpn` and `stats.stages_kor` (the discs number their stages differently,
`key_suffix`, `ico_region_key_suffixed`); the unlocks and the other counters are one set for the
player ([CONFIG.md](CONFIG.md#mirror), [GAME_FEATURES.md](GAME_FEATURES.md)).

## The calls

`port/compat/libmc.h` declares the calls the game makes and nothing else: `sceMcInit`,
`sceMcSync`, `sceMcGetInfo`, `sceMcFormat`, `sceMcUnformat`, `sceMcMkdir`, `sceMcChdir`,
`sceMcGetDir`, `sceMcDelete`, `sceMcOpen`, `sceMcClose`, `sceMcSeek`, `sceMcRead`, `sceMcWrite`
and `sceMcFlush`, with the 64-byte `sceMcTblGetDir` entry, the `sceMcRes*` result codes the game
compares against and the `sceMcFuncNo*` numbers `sceMcSync` reports
(`port/compat/libmc.h`, header comment). The header is written from this project's clean-room
`sce/libmc/libmc.h`, with pointer-sized types made host-correct.

The result codes the port returns, and when:

| code | value | returned for |
| --- | --- | --- |
| `sceMcResSucceed` | 0 | success (a read or write returns the byte count, an open the handle) |
| `sceMcResChangedCard` | -1 | the first `sceMcGetInfo` after `sceMcInit` |
| `sceMcResNoFormat` | -2 | a port after `sceMcUnformat` |
| `sceMcResFullDevice` | -3 | a directory or file that cannot be made, a short write, a failed close |
| `sceMcResNoEntry` | -4 | a missing file or directory; `sceMcMkdir` of one that exists |
| `sceMcResDeniedPermit` | -5 | a bad handle, or an open mode that forbids the call |
| `sceMcResNotEmpty` | -6 | `sceMcDelete` of a directory that has entries |
| `sceMcResUpLimitHandle` | -7 | a fourth open file (libmc's limit is three) |
| `sceMcResFailDetect`, `sceMcResFailDetect2` | -9, -10 | a call on an empty port (`-10` from `sceMcGetInfo`) |

(`port/compat/libmc.h`, the `sceMcRes*` enum; `port/save/mc_host.c`, the `begin` call of each
function, `MC_HANDLES`, `unusable`.)

A file opened for writing is written to a hidden copy, `<dir>/.<name>.tmp`, which `sceMcClose`
syncs to the disk and moves over the file; a crash, a power cut or a full disk during a save leaves
the previous file whole. A new file is on the card, empty, from the open, as on the PS2
(`port/save/mc_host.c` (`McHandle`, `close_handle`, `copy_to`, `sceMcOpen`)). Opening or writing
a save file (`game.*`) logs one `mc: reading ...` or `mc: writing ...` line, so a log that stops
there says so (`port/save/mc_host.c` (`game_file`, `sceMcOpen`)).

## Completion at the next vsync

As the disc's calls do ([DATA.md](DATA.md)), a card request does all its work when it is issued and
completes at the next simulated [vsync](ARCHITECTURE.md#terms): an on-vsync callback marks it done,
and from then `sceMcSync(1, ...)` returns 1 with the function number and the result; before that
it returns 0, and with no request it returns -1. `sceMcSync(0, ...)` waits for the request, which
is done at once (`port/save/mc_host.c`, header comment, `begin`, `vsync`, `sceMcSync`).

The same callback signals the game's card lock semaphore, `IosMcLock`, when it exists: the game's
`iosMcMgrSync` waits on it between its `sceMcSync` polls, so each poll happens once per vsync, the
rate it has on the PS2 (`port/save/mc_host.c` (`vsync`); `ico2/fumi/ios/mcard.c` (`IosMcLock`)).
`ico_mc_host_pending` reports whether a request is waiting for its `sceMcSync`
(`port/save/mc_host.h`). The callback is registered by the first `sceMcInit`
(`ico_host_on_vsync_register`; [PLATFORM.md](PLATFORM.md) has the host loop).

## Where the card folders are

The first `sceMcInit` reads the folders unless a caller set them first with
`ico_mc_host_set_root` or `ico_mc_host_set_port_root` (the tests do); setting either port stops
`sceMcInit` reading the configuration for both (`port/save/mc_host.h`;
`port/save/mc_host.c` (`sceMcInit`, `ico_mc_host_set_port_root`)).

- Port 0: `saves=` in `ico-pc.ini`, else `[paths] saves` in `config.toml`, a relative path taken
  from the executable's folder; with neither, `memcard/` in the
  [user folder](ARCHITECTURE.md#terms) (`port/platform/host_config.c` (`ico_host_saves_dir`,
  `card_dir_key`)). A path too long for the buffer falls back to `memcard` in the working folder,
  with a log line (`port/save/mc_host.c` (`sceMcInit`)).
- Port 1: `saves2=` in `ico-pc.ini`, else `[paths] saves2`, relative paths likewise; absent or
  empty, port 1 has no card. With two cards the log says `mc: two cards: port 0 ..., port 1 ...`
  (`port/platform/host_config.c` (`ico_host_saves2_dir`); `port/save/mc_host.c` (`sceMcInit`)).
- On the August 2001 preview both folders get `-preview` appended (`memcard-preview`; see the
  North American disc and the preview above; `port/save/mc_host.c` (`sceMcInit`)).

Both functions read the game's own `ico-pc.ini` (`ico_host_ini_path`: the executable's folder, or
the user folder in a macOS app bundle) over `config.toml` (`ico_ini_load_layered`), set no
environment variable and make no folder, so they can run on the game's
[fiber](ARCHITECTURE.md#terms) inside `sceMcInit` (`port/platform/host_config.c`, the comment
above `card_dir_key`). A relative `saves` or `saves2` path is taken from the executable's folder on
every platform (`card_dir_key`).

The default is `memcard/` in the user folder (`port/platform/host_config.c`
(`ico_host_saves_dir`)); [PLATFORM.md](PLATFORM.md#folders-ico-pcini-and-the-user-folder) has where
the user folder is on each platform and in portable mode, [CONFIG.md](CONFIG.md#paths) the keys.

## Importing a PS2 save

`ico_mc_import` copies the game's save directories, the European disc's `BESCES-50760ico` and the
Japanese disc's `BISCPS-11003ico` and the Korean disc's `BISCPS-56001ico`, whichever the source holds, out of a PS2 memory card image or
a save archive into a card folder, each into its own directory there (`port/save/mc_import.h`
(`ico_mc_import`); `port/save/mc_import.c` (`save_dir_region`)). A card image may hold several; a
`.psu` holds one directory. `IcoMcImport.regions` has bit r set when files of region r's
directory (the row of `config/regions.txt`) were written. The
format is told by the file's contents, not its name (`port/save/mc_import.c` (`detect`)):

| format | recognised by | read |
| --- | --- | --- |
| raw card image (`.ps2`, `.bin`) | the superblock's `Sony PS2 Memory Card Format ` at page 0 | yes |
| `.psu` (uLaunchELF, mymc) | a directory entry, then `.` | yes |
| `.max` (Action Replay MAX) | `Ps2PowerSave` | refused, with a message |
| `.cbs` (CodeBreaker) | `CFU` | refused, with a message |

A raw image is read through the card's own file system: the superblock, the FAT reached through
the superblock's indirect FAT cluster list, and directories of 512-byte entries. Pages are 512
bytes on the 8 MB card, followed by 16 bytes of ECC in images that keep them (PCSX2's and mymc's
`.ps2`) and by nothing in plain dumps; the file's size tells which, and the ECC is not checked.
A `.psu` is the save directory's entry, `.` and `..`, then each file's entry followed by its data
padded to 1024 bytes (`port/save/mc_import.c`, header comment, `read_raw`, `read_psu`). Images up
to 80 MB and archives up to 16 MB are read (`IMAGE_MAX`, `ARCHIVE_MAX`).

Only the game's directories are imported; every other entry is counted and the first 32 are named
in the result (`IcoMcImport.skipped`, `skippedName`). Every file is read before any is written, an
existing file of the same name is refused unless `ICO_MC_IMPORT_OVERWRITE` is passed, and each
file is written to a dot-file copy that is synced and moved over its name, as the card writes
(`port/save/mc_import.c` (`write_files`)). The result is 0 imported, 1 when the source holds no
ICO save (`no ICO save (BESCES-50760ico or BISCPS-11003ico or BISCPS-56001ico) in FILE`) or an empty one, -1 on an
error with the reason in `IcoMcImport.why` (`port/save/mc_import.h` (`ico_mc_import`)).

The command-line tool runs it on each file it is given:

```
mc_import [--overwrite] --to SAVES_FOLDER FILE...
```

`SAVES_FOLDER` is the card folder (`[paths] saves` or `saves2`); each file prints its format and
either the files imported and the directories they went to
(`imported 12 file(s) into SAVES_FOLDER: BESCES-50760ico BISCPS-11003ico`) or the reason, then
the skipped entries. The exit status is 0 when every
file gave a save, 1 when one held none, 2 on an error (`tools/mc_import.c` (`main`, `usage`)). The
tool is built by `port/save/CMakeLists.txt` and kept in every build (`cmake/IcoTests.cmake`
(`ICO_KEEP_IN_ALL`)). The program itself does not call the importer.

## Tests

`port/save/test/save_test.c` is the ctests `save` (the European disc), `save_jpn` (the
Japanese disc), `save_kor` (the Korean disc), `save_cn` (the Asian disc), `save_ntsc` (the North
American disc), `save_aug6` (the August 2001 preview) and `save_jun26` (the June 2001 demo): its first argument is the region's slug, which it selects
(`ico_region_select`), so every part runs with that disc's directory and product file size. Each
runs on temporary card folders in its own working directory, on simulated vsyncs, and needs no
game data (`port/save/test/save_test.c`, header comment; `port/save/CMakeLists.txt`):

- `test_libmc`: the `sceMc*` calls the game's card code issues, scripted by hand: the first boot on
  a card with no save, the first save to slot 1, a reload, a re-save that leaves the file
  byte-identical, a delete, and the result codes the game compares.
- `test_mcard`: `ico2/fumi/ios/mcard.c` and `mcdata.c` themselves, compiled unchanged with the
  game's options on the scheduler, driven through the `iosMc*` entry points as `kanbanBoot.c` and
  `layout_action.c` drive them; the bytes left in the card folder are checked against a
  hand-laid-out expectation: the product file is `mc_product_size` bytes and its sum (0x1F0, 0x1E8
  or 0x18C), the same bytes up to 0x1E8 on the Japanese, Korean and Asian discs, `cameraMove` and
  `palMode` written and read back on the European disc only, the 16-byte-preview record of the
  three 2001 discs (with the `cleared` word on the preview and the demo only, and `fileNo`,
  `serial` and the options of the European record not coming back), and `game.00N` 25,600 bytes
  (20,484 on the demo). A stand-in for the game's background disc read supplies the icon files; the
  demo's carry the sum and a second save leaves them alone. The test also pins each row's
  `mc_save_dir` to mcard.c's `iOSMcSaveSeg_*` literal and 0x18C to 20 × 16 + 3 × 4 + 64.
- `test_import`: the importer on synthetic card images (plain and with ECC spares) and a synthetic
  `.psu`, the game's files read back through the folder card byte for byte, other games' entries
  skipped and named; the other disc's `.psu` imported beside the selected disc's save; a card image
  holding the selected disc's and another's directories imported side by side (`regions` is those two bits); the `mc_import` tool (its path
  is the test's second argument) on the same files, and the directories it prints; and the second
  card in port 1.

Run them with `ctest --preset linux-x64 -R '^save(_jpn|_kor|_cn|_ntsc|_aug6|_jun26)?$'`; it needs the `mc_import` target, which every
build keeps (`cmake/IcoTests.cmake` (`ICO_KEEP_IN_ALL`)).
