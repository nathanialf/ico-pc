# Game data: the disc layer

How the port reads the player's disc: the [VFS](ARCHITECTURE.md#terms) and its two backends (the
disc image read in place, and the extracted archive [`ico.o2r`](ARCHITECTURE.md#terms)), the
first-run extractor that writes the archive, the [libcdvd](ARCHITECTURE.md#terms) and
[SIF](ARCHITECTURE.md#terms) host layers under the game's own cdvd manager, the
[IOP](ARCHITECTURE.md#terms) RAM stand-in, the loader that fills the game's data tables from the
disc's boot ELF, and the port's own reads of DATA.DF. The repository holds no disc data
([LEGAL.md](../LEGAL.md)): what is here is structure, sector numbers and hashes.

## Files

| path | what it holds |
| --- | --- |
| `port/data/vfs.h`, `vfs.c` | the VFS: the backend table, path spellings, byte reads over sector reads, the disc slot |
| `port/data/iso9660.c` | the ISO9660 backend and `IcoDiscImage`, the logical bytes of an `.iso`, `.chd`, `.bin` or `.cue` |
| `port/data/archive.h`, `archive.c` | the archives (`ico.o2r`, `ico-jpn.o2r`): their layout, the disc table's type (`IcoDiscInfo`), the archive backend |
| `port/data/discs.c` | the disc table `ico_discs` (one row per disc, in `config/regions.txt`'s order) and each disc's DATA.DF manifest |
| `port/data/extract.h`, `extract.c` | the first-run extractor, the disc's identification by its boot file, the wrong-disc and unreadable texts |
| `port/data/cdvd_host.h`, `cdvd_host.c` | the `sceCd*` functions the game calls, over the VFS disc |
| `port/data/sif_host.h`, `sif_host.c` | `sceSif*` with no IOP: the IOP heap, SIF DMA, RPC to host servers |
| `port/data/sifdev_host.c` | `sceOpen` and the other sifdev file calls, for the development kit's `host0:` device |
| `port/data/iop_ram.h`, `iop_ram.c` | `ico_iop_ram`, the 2 MB of IOP RAM, and the IOP heap allocator |
| `port/data/tables.h`, `tables.c` | the runtime loader of the 73 data tables |
| `port/data/gen/` | the generated table definitions (`table_defs.c`), descriptor types (`table_desc.h`), the sets' list (`table_sets.c`, `tables.cmake`) and one descriptor set per disc (`pal/`, `jpn/`, `kor/`, and `ntsc/` and `aug6/` ahead of their rows: `table_desc.c`, `ee_symbols.c`, `tables.cmake`) |
| `port/data/df_pack.h`, `df_pack.c` | reads of DATA.DF's loose files and pack members for the port's own screens |
| `port/data/json.h`, `json.c` | the JSON reader (`ico_json`) |
| `port/data/chdr_miniz_names.h` | prefixes the second miniz copy inside libchdr |
| `port/data/CMakeLists.txt` | the libraries (`ico_port_data`, `ico_miniz`, `ico_libchdr`, `ico_json`) and the tests |
| `port/platform/android/iso_import.h`, `iso_import.c` | the Android first start's copy of the chosen image |
| `ico2/fumi/ios/cdvd.c`, `ico2/seki/src/FileManager.c` | the game's side: the cdvd manager and the boot loader |
| `config/tables_manifest.txt`, `tables_manifest.<slug>.txt` | per disc, the CRC-32 of every table row and the addresses the tables' pointers hold |
| `config/ee_layout.<slug>.txt`, `motion_bits.<slug>.txt` | the other discs' record layouts and MotionDef's shift-read bits (`tools/gen_ee_layout.py`; jpn, kor, ntsc, aug6, jun26, cn) |
| `config/symbol_addrs.<slug>{,.data}.txt` | the names at the other ELFs' addresses (`tools/gen_symbol_addrs.py`; jpn, kor, ntsc, aug6, jun26, cn) |

## Layers

```
game  fumi/ios/cdvd.c          the cdvd manager thread, its request queue, the background
                               request table, the stream manager, the directory cache,
                               DATA.DF's directory (unifile_read_func)
      seki/src/FileManager.c   boot: file_Init, file_LoadCDFile
      fumi/sound/adpcm_init.c  ADPCM streams into IOP memory
---------------------------- the SDK's function names (port/compat/libcdvd.h, sifrpc.h, sifdev.h)
port  port/data/cdvd_host.c    sceCd* over the VFS disc
      port/data/sif_host.c     sceSif*: IOP heap, SIF DMA, RPC to host servers
      port/data/iop_ram.c      ico_iop_ram and the IOP heap
      port/data/vfs.c          paths, byte reads, the disc slot
      port/data/iso9660.c      backend: the disc image in place
      port/data/archive.c      backend: ico.o2r
      port/data/extract.c      writes ico.o2r on the first run
      port/data/tables.c       the data tables, from the boot ELF on the disc
```

The seam is the SDK's own function names: the game calls `sceCdRead`, and the host build links
`port/data`'s definition. Everything above it in `cdvd.c` (the queue, the threads, the cache, the
retry and drive-recovery paths) runs as written (`port/data/cdvd_host.c` (header comment)).

## The VFS

The game addresses the disc as libcdvd does: it looks a file up by its ISO9660 path, gets its first
logical sector number (LSN) and byte size, and reads whole 2048-byte sectors by LSN. It also does
LSN arithmetic of its own: DATA.DF's directory gives byte offsets inside DATA.DF, and
`unifile_read_func` (`ico2/fumi/ios/cdvd.c`) records each member as DATA.DF's LSN + offset / 2048.
So every backend serves one flat LSN space with the disc's own numbering (`port/data/vfs.h`).

| call | what |
| --- | --- |
| `ico_vfs_mount(backend, location)`, `ico_vfs_unmount` | open a volume through a backend table; the reason for a failure goes to stderr |
| `ico_vfs_reopen(vfs)`, `ico_vfs_location` | a second mount of the same volume: same backend, same location, state of its own (the image's file handle and CHD hunk buffers, the archive's `FILE*`; the Korean DATA.DF shift found again), so another thread can read it while the game reads the first |
| `ico_vfs_set_disc`, `ico_vfs_disc` | the volume the libcdvd layer reads (not owned; `NULL` is "no disc") |
| `ico_vfs_normalize` | any accepted spelling to `DIR/FILE` |
| `ico_vfs_stat`, `ico_vfs_open`, `ico_vfs_size` | a look-up: LSN, size, ISO date, directory flag, name as the volume spells it |
| `ico_vfs_read(file, offset, dst, len)` | bytes of a file, short at its end |
| `ico_vfs_read_sectors(vfs, lsn, count, dst)` | whole sectors anywhere on the volume |
| `ico_vfs_lsn_to_offset`, `ico_vfs_offset_to_lsn`, `ico_vfs_size_to_sectors` | sector arithmetic (`ICO_VFS_SECTOR`, 2048) |

A backend (`IcoVfsBackend`) has `mount`, `unmount`, `lookup` (a normalized path), `read_sectors`
(-1 when any sector lies outside the volume) and `volume_sectors`. Paths are case-insensitive, and
`ico_vfs_normalize` accepts `DFDATAS/DATA.DF`, `DFDATAS\DATA.DF;1`, `\DFDATAS\DATA.DF;1`,
`cdrom0:\SYSTEM.CNF;1` and `/SCES_507.60`: it strips a `cdrom0:` or `cdrom:` prefix, leading
separators and the `;<version>` suffix, maps `\` to `/` and upper-cases (`port/data/vfs.h`
(`IcoVfsBackend`, `ico_vfs_normalize`)).

## The three discs

The port plays three discs, one row each of `config/regions.txt` (the region module,
[GAME_CODE.md](GAME_CODE.md#regions)); `ico_discs` (`port/data/discs.c`) holds what the disc
layer needs of each, in the same order (the `region_select` test checks the slug, disc id, boot
file and product name of every row against the table):

| | European disc | Japanese disc | Korean disc |
| --- | --- | --- | --- |
| slug, disc id, boot file | `pal`, `SCES-50760`, `SCES_507.60` | `jpn`, `SCPS-11003`, `SCPS_110.03` | `kor`, `SCPS-56001`, `SCPS_560.01` |
| image SHA-1, size | `1017b53f6e80f41f823369b0be1d8c69f7e16dc6`, 907,706,368 bytes | `95ee72becbc7fba85e9c0f6aa6fc932c705b37b3`, 559,693,824 bytes | `156b63e5824b03e60e0d45c4e86fc424d9e1dd1b`, 873,332,736 bytes |
| boot ELF SHA-1 | `da3644c54c26fe760f3b6a591a5fc2eab396ed2b` | `580da1dcc4c239a1608a10300e7e220fe5d35525` | `f9b00596d133c698df20c8ffe4ca2d7107e236bb` |
| DATA.DF | 867,184,640 bytes, 193 members, directory CRC-32 `0xfb82eea7` | 553,443,328 bytes, 183 members, directory CRC-32 `0xfc60bbaf` | 552,286,208 bytes (the span from the archive's header on, see below), 183 members, directory CRC-32 `0xa8ab2e74` |
| archive, image name | `ico.o2r`, `Ico_PAL.<ext>` | `ico-jpn.o2r`, `Ico_JPN.<ext>` | `ico-kor.o2r`, `Ico_KOR.<ext>` |
| media, `sceCdGetDiskType` | DVD, `0x14` (`SCECdPS2DVD`) | CD, `0x12` (`SCECdPS2CD`) | DVD, `0x14` |
| DATA.DF shift (`df_lsn_shift`) | 0 | 0 | 153,600 sectors |

Which disc an image is comes from its `SYSTEM.CNF`: the `BOOT2` line's file, normalized
(`SCPS_110.03`), is looked up by `ico_disc_identify` (`extract.h`); `ico_extract_identify` does
this for an image path. A boot file one row has is answered from the directory alone, so the
program knows the disc before it hashes anything. When two rows boot with one file (`SCUS_971.13`: the North
American disc and the August 2001 preview, once their rows exist): there the boot ELF (`elf_path`, 4 MB) is hashed and
the row with that `elf_sha1` is the disc (`ico_disc_by_boot_elf`), logged once. The magazine demo
boots its menu `SCUS_971.58` and its game lives in the `CUS97113` folder (`game_dir`, `df_path`);
its row has no image hash (`iso_sha1` is NULL) and is accepted by the ELF and DATA.DF rule only.
The row's values then verify the image. An image whose boot file is none of the rows', or that is not an
unmodified copy of its row's disc, gets one box text everywhere (`ICO_WRONG_DISC_TEXT`,
`extract.h`: the extractor, `main_host.c`, the Android import); one that cannot be read gets
`ICO_UNREADABLE_DISC_TEXT`. `LIBSD.IRX` and `SNDN2DRV.IRX` are byte-identical on all three discs
([AUDIO.md](AUDIO.md#values-read-from-the-disc)).

### The Korean disc's DATA.DF shift

The Korean disc's directory lists `\DFDATAS\DATA.DF` 153,600 sectors (300 MB) before the archive's
header: the first 300 MB of the file are not the game's data, and the Korean build adds 153,600 to
the sector `sceCdSearchFile` returns for it (upstream's `VERSION_KOR` block in
`iosCdvdMgrSearchFile`, [DIVERGENCES.md](DIVERGENCES.md) GL-12). The port applies that shift once,
in the disc layer, so the game code reads the archive from the same sectors without adding it
again. The row's `IcoDiscInfo.df_lsn_shift` (`port/data/archive.h`; 153,600 for the Korean disc, 0
for the others) is read by the ISO9660 backend when it mounts: it reads `SYSTEM.CNF`, looks the
`BOOT2` file up with `ico_disc_by_boot` and keeps the row's shift (`iso_find_df_shift`). A look-up
of the normalized path `DFDATAS/DATA.DF` then returns the directory's sector plus the shift and
the directory's size minus the shift in bytes, and fails if the directory's size is not larger
than the shift. Every consumer goes through that look-up (the extractor, the archive's `meta.json`,
`sceCdSearchFile`, the movie and gallery readers, the texture and model readers), so no other site
changed. The extractor's raw directory walk reads directory sectors only, which are never shifted.
The Korean row's manifest (`DF_KOR_SIZE` 552,286,208 bytes, 183 members, offsets relative to the
archive's header) is therefore of the shifted span, not of the 866,859,008 bytes the directory
lists. `ico_vfs_iso9660_force_df_shift` (`vfs.h`) overrides the table in a test. The synthetic
`archive_shift` test (`archive_test shift`, no disc needed) covers the look-up, and `archive_disc_kor`
and `vfs_disc_kor` check a real image (the entry's size equals the row's `df_size`, the header's
count the row's `df_count`, the raw directory's sector plus the shift the entry's). The Korean
disc has been booted headless to the title once in the container (identified as SCPS-56001, its
tables loaded, the boot signs, the opening movie, the title); it has not been played yet.

### A disc whose game is in a folder: the path rule

The June 2001 magazine demo (a 4.4 GB DVD) boots the magazine's menu `SCUS_971.58` from its root;
ICO's demo is one folder among the other games', `CUS97113/`, with its own ELF (`SCPS_000.00`,
5,774,664 bytes), its own modules (`IOPRP224.IMG`, `LIBSD.IRX`, `MCMAN.IRX`, `MCSERV.IRX`,
`PADMAN.IRX`, `SIO2MAN.IRX`, `SNDN2DRV.IRX`) and its own `DFDATAS/DATA.DF` (271,493,120 bytes, 49
members), read off the image with an ISO9660 walk. Its row's `game_dir` is `CUS97113`, `df_path`
`CUS97113/DFDATAS/DATA.DF`, and `elf_path` the game's ELF, not the menu. The rule:

- Every disc path keeps its folder: the archive stores `CUS97113/DFDATAS/DATA.DF` as
  `disc/CUS97113/DFDATAS/DATA.DF` with its disc LSN, and `meta.json` lists `CUS97113` and
  `CUS97113/DFDATAS` as directories. Nothing is moved to the root.
- The game spells the folder out itself (upstream's JUN26 arms: `cdrom0:\CUS97113\SNDN2DRV.IRX;1`
  in `FileManager.c`, `CUS97113/DFDATAS/DATA.DF` in `cdvd.c`'s unifile request). Those requests go
  through `sceCdSearchFile` (`cdvd_host.c`) and `ico_vfs_stat` like a root file's: the VFS
  normalizes the spelling and the backend walks the path a component at a time (the ISO9660
  backend) or finds it in `meta.json` (the archive). No game request is rewritten.
- The port's own reads of DATA.DF (`df_pack.c`, the movie reader `port/fmv/movie.c`, the
  extractor, the manifest printer) ask the VFS where it is: `ico_vfs_df_path` (`vfs.h`), set at
  mount from `SYSTEM.CNF`'s boot file and that row's `df_path` (`DFDATAS/DATA.DF` for a row
  without a folder, and for an image no row boots). `ico_vfs_game_dir` and `ico_vfs_game_path`
  give the folder and a file under it. A mount of such a disc logs once a run
  `vfs: the boot file SCUS_971.58 is SCUS-97158's menu; its game is in the folder CUS97113
  (DATA.DF: CUS97113/DFDATAS/DATA.DF)`.
- The extractor keeps, for a row with a `game_dir`: `SYSTEM.CNF`; the boot file listed in
  `meta.json` without a `data` member (a look-up answers with its disc LSN and size, so libcdvd's
  identify and `sceCdSearchFile` see the disc as it is; a read of its sectors fails); in the
  folder, the row's `elf_path`, every `*.IRX` and `DFDATAS/` whole. Not stored: the menu's bytes,
  the root's other files, `SCE_DD/` and the other games' folders, and `IOPRP224.IMG` (as on the
  retail discs; `sceSifRebootIop` is a stub). The demo's archive holds 9 files.
- The row has no image SHA-1 (`iso_sha1` NULL): the extractor does not hash the 4.4 GB image (no
  `hash` phase); it accepts the image by `elf-sha1+datadf-crc` alone and writes the game ELF's SHA-1
  as `meta.json`'s `source.sha1` (`IcoExtractResult.iso_hashed` 0). `ico_archive_info_acceptable`
  refuses `iso-sha1` for such a row. A start from the image (`use_iso`, `verify_iso`) checks the
  same two things with `ico_extract_verify_files` (the ELF's SHA-1 and DATA.DF read through
  against the manifest) instead of hashing the image.
- The ISO9660 backend's Korean shift compares a look-up with the row's `df_path`, so a shifted
  DATA.DF in a folder would be found the same way (no row has both).

The synthetic `archive_subfolder` test (`archive_test sub`) builds such an image with a menu at
the root, the game in `GAME/`, a root module and a `MODULES/` folder the archive must leave out,
and supplies its row with `ico_disc_set_test_row` (`archive.h`; the look-ups answer it after the
table's, as `ico_vfs_iso9660_force_df_shift` does for the shift). The demo's game ELF waits for a
product file `CUS97113\SCUS_971.13` and disk type 18 only after a disc error
(`iosCdvdDiskReadyBlock`, `iosCdvdBackGroundMgr`); the image has no such file.

## Disc images

`ico_vfs_iso9660` (`port/data/iso9660.c`) reads an image in place. It reads the primary volume
descriptor from LSN 16 and walks directories from the root record; it needs no path table and no
Joliet or Rock Ridge, and refuses a multi-extent entry. A directory's recorded length may be the
bytes in use rather than whole sectors (PS2 mastering tools write it so): the walk reads every
sector the length touches and stops at a zero length byte in each.

Every container gives the same logical bytes, the ISO's, through `IcoDiscImage`
(`ico_disc_image_open`, `ico_disc_image_read`, `ico_disc_image_bytes`):

| image | how it is read |
| --- | --- |
| `.iso` | as it is |
| `.chd`, DVD (`chdman createdvd`) | 2048-byte units in order through libchdr, one hunk cached |
| `.chd`, CD (`chdman createcd`) | 2448-byte frames (2352 sector bytes and 96 subcode bytes) described by the `CHT2` or `CHTR` track metadata; the first track must be data, and each frame's 2048 user bytes are taken where the track type puts them (`cd_data_offset`) |
| `.bin` | 2352-byte raw sectors, recognised by the 12-byte sync pattern at the start (`raw_sync`); byte 15 is the mode: mode 1 data starts at byte 16, mode 2 form 1 at byte 24; a form 2 sector is refused |
| `.cue` | parsed by `ico_cue_parse` for its first `FILE`, first `TRACK` type and `INDEX 01`; the `.bin` it names is opened from beside the sheet, and the sheet's type and the sector header must agree. A sheet with several files, or whose first track is audio, is refused |

A CHD that needs a parent image is refused. libchdr is compiled from the source tree
`tools/fetch_deps.sh` installs, with its bundled LZMA, miniz, zstd and FLAC decoders, so the program
needs no system zlib or zstd; `chdr_miniz_names.h`, forced into every libchdr file, gives its miniz
copy a `chdr_` prefix beside the port's own `ico_miniz` (`port/data/CMakeLists.txt`).

## The archives: ico.o2r, ico-jpn.o2r and ico-kor.o2r

Each disc's data goes to its own archive (`IcoDiscInfo.archive_name`: `ico.o2r` for the European
disc, `ico-jpn.o2r` for the Japanese one, `ico-kor.o2r` for the Korean one), so all can sit side by side in the user folder. An
archive (`port/data/archive.h`) is a ZIP whose entries are stored, not compressed:

| entry | holds |
| --- | --- |
| `disc/<PATH>` | the disc file `PATH` (`DFDATAS/DATA.DF`, `SCES_507.60`, `SCPS_110.03`), byte for byte |
| `tail/<PATH>` | the bytes after the file's end up to its last sector's end, only when they are not all zero |
| `meta.json` | the source image (SHA-1, size, the rule that accepted it), the extractor, the disc id, the volume's sector count, and for every disc file and directory the archive answers for: path, ISO name, first LSN, size and date |

`ico_vfs_archive` answers look-ups from `meta.json` and reads sectors by LSN: each stored file
covers the LSNs it had on the disc, so DATA.DF keeps one contiguous range and the game's LSN
arithmetic addresses the same bytes as on the disc. A sector no stored file covers reads as an
error. At mount, miniz reads the ZIP's central directory, `meta.json` is parsed (`port/data/json.h`)
and each entry's data offset is found from its local header; after that a sector read is a plain
`fseek` and `fread` (`port/data/archive.c`).

`ico_archive_read_info` opens an archive and checks its structure (every entry `meta.json` names is
stored, uncompressed, inside the file, at the size `meta.json` gives). `ico_archive_info_acceptable`
accepts it only for format version `ICO_ARCHIVE_VERSION` (1), the extractor string
`ICO_ARCHIVE_EXTRACTOR`, a disc id that is a row of `ico_discs`, the rule `iso-sha1` (with that
row's image SHA-1) or `elf-sha1+datadf-crc`, and that row's boot ELF SHA-1. `mount_archive` in
`port/platform/main_host.c` checks that the archive holds the disc it was opened for, reads the
row's boot ELF back through the mounted archive and checks its SHA-1 again before making it the
disc; the log line is `ico_pc: game data <path> (SCPS-11003, N files, N bytes; from an image with
SHA-1 ..., accepted by <rule>)`.

## The first run

`mount_game_data` (`port/platform/main_host.c`) first decides the disc
(`ico_host_choose_disc`, [PLATFORM.md](PLATFORM.md#the-disc-a-start-plays)), then looks for that
disc's archive in the
[user folder](ARCHITECTURE.md#terms) (`ico_host_pref_dir`: SDL's pref path in the
[window build](ARCHITECTURE.md#terms), the executable's folder in the
[headless build](ARCHITECTURE.md#terms), the portable folder in portable mode), then beside the
executable. The first usable one is mounted; an unusable one is logged
(`ico_pc: <path> is not usable: <why>`) and skipped. With none, the first run finds the disc image,
extracts it into the user folder, and mounts the result.

`find_iso` (`main_host.c`) looks for the image in this order: `--iso`; `Ico_PAL.iso`, `Ico_PAL.chd`,
`Ico_PAL.bin`, `Ico_PAL.cue`, `Ico_JPN.iso`, `Ico_JPN.chd`, `Ico_JPN.bin`, `Ico_JPN.cue`, `Ico_KOR.iso`, `Ico_KOR.chd`, `Ico_KOR.bin`, `Ico_KOR.cue` beside the
executable (`image_names`, `find_beside`: the pinned disc's, else with more than one there the disc played
last's, else the European one's); `iso=` in `ico-pc.ini` (a
relative path is taken from the executable's folder); the
`ICO_ISO` environment variable; the same twelve names under `baserom/` in the working folder; then a
file-open dialog (native on Windows, SDL3's in the other window builds), whose answer is saved as
`iso=` once it is verified. On iOS the executable's folder is the app's Documents folder
(`port/platform/host_config.c` (`ico_host_exe_dir`)), and with no image there the run ends with a
message box instead of a dialog. On Android the image comes from the system's file picker
(`find_iso_android`, below).

`ico_extract_archive` (`port/data/extract.h`) mounts the image with the ISO9660 backend, takes its
row of `ico_discs` from the boot file, and stores `SYSTEM.CNF`, the boot file its `BOOT2` line
names (the row's boot ELF), `DUMMY.TXT` when the disc has one, every `*.IRX` module in the root
(the port reads `SNDN2DRV.IRX`'s pitch table and `LIBSD.IRX`'s reverb presets and idle voice
block), and every file under `DFDATAS/`, DATA.DF whole (for a disc whose game is in a folder,
that folder's files instead, [above](#a-disc-whose-game-is-in-a-folder-the-path-rule)). It hashes
the whole image (not for a row with no image SHA-1), streams each file
into the ZIP with no compression (hashing the boot ELF and taking the CRC-32 of DATA.DF's
directory and members on the way), and writes `meta.json` last, once the image is accepted. The
rules, in order:

| rule | accepts when |
| --- | --- |
| `iso-sha1` | the image's logical SHA-1 is the row's `iso_sha1` (for a `.chd`, `.bin` or `.cue`, the SHA-1 of the ISO it holds) |
| `elf-sha1+datadf-crc` | the row's boot ELF hashes to its `elf_sha1`, and DATA.DF has that disc's size, directory and members: the CRC-32 of its directory and of each of its members (193 on the European disc, 183 on the Japanese and the Korean) match the row's manifest in `discs.c` (a re-dump with other padding or volume size passes) |

Anything else is refused and nothing is left behind; `IcoExtractResult` says whether the file was
`unreadable` (not an image this program reads) or the `wrong_disc` (none of the discs: another region
or edition, or a modified image), which `mount_game_data` turns into the message box the player
sees; the `why` text starts with that box's words and adds the boot file and hashes for the log.
`ICO_EXTRACT_NO_VERIFY` (tests on a synthetic image) writes the rule `unverified`, which
`ico_archive_info_acceptable` refuses. The archive is written to `<out>.tmp` and moved over `<out>`
once it is complete and reads back (`MoveFileEx` on Windows); a failure or a cancel removes the
`.tmp`. Progress runs through `IcoExtractProgressFn` in the phases `open`, `hash`, `extract`,
`finish`; the window build shows it in a small window and the log (`port/platform/main_host.c`
(`mount_game_data`)).

Each disc's DATA.DF manifest in `discs.c` (between its `BEGIN DATA.DF MANIFEST <slug>` and `END
DATA.DF MANIFEST <slug>` lines) holds DATA.DF's size, the directory's member count and CRC-32, and
each member's offset, size and CRC-32: no names and no bytes of the file. `archive_test manifest
--region <slug> <iso>` prints a block again (`ico_extract_print_datadf_manifest`; `manifest <iso>`
is the European one), and the `archive_manifest_jpn` and `archive_manifest_kor` tests compare the Japanese and Korean blocks with the
image.

Android has no file beside the program: `find_iso_android` (`main_host.c`) asks the system's file
picker, and `ico_iso_import` (`port/platform/android/iso_import.h`) copies the chosen `content://`
stream into the app's files folder as `Ico_PAL.iso`, `.chd` or `.bin` (the kind from the header,
`ico_iso_ext_for`; a `.cue` sheet is refused, `ico_iso_is_cue`, and the picker says to pick the
`.bin` beside it: the raw reader takes the sync pattern and mode from the `.bin`'s first sector, so
the `.bin` alone reads the same), after checking that the image and
`ICO_ISO_IMPORT_SPARE_BYTES` (1.2 GB for the extracted data and a margin) are free. The two boxes
about space (short before the copy, `ico_iso_space_text`; a failed copy of known size,
`ico_iso_copy_failed_text`) and the copy's progress line are the string tables'
`UI_STR_ISO_NEED_SPACE`, `UI_STR_ISO_COPY_FAILED`, `UI_STR_ISO_COPYING` and, for an image over
`ICO_ISO_LONG_COPY_BYTES` (2 GB: the 4.4 GB magazine demo, which needs 5.6 GB), the line that
says it takes a few minutes, `UI_STR_ISO_COPYING_LONG`; `find_iso_android` sets the port's language
from `[ui] language` before the copy (English when it is `auto`: the disc, which decides
otherwise, is not read yet), and the log line of a long copy says `(a large image: the copy takes
minutes)`. The extractor
then reads the copy like any file; once identified, a copy of another disc is renamed after its
row's image name (`Ico_JPN.<ext>`, `Ico_KOR.<ext>`, `Ico_JUN26.<ext>`; `android_name_image`), and `main_host.c` deletes it once the game data is mounted,
unless `keep_image=1` is in `ico-pc.ini`. A cancelled or refused copy is removed.

`use_iso=1` in `ico-pc.ini` (or `[dev] use_iso`) skips the archive and mounts the image directly
with `ico_cdvd_host_mount_iso`; it is the headless build's default, so
[trace](ARCHITECTURE.md#terms) runs and tests read the image (`use_iso_mode`). In that mode the
image is identified by its boot file and its SHA-1 checked against that disc's unless `verify=0` or
`--no-verify` (`verify_iso`; for a row with no image SHA-1, its game ELF and DATA.DF instead,
`ico_extract_verify_files`); the extractor always verifies. Either way the disc is selected
(`select_disc`) from the mounted image's boot file.

## libcdvd on the host

`port/data/cdvd_host.c` defines the `sceCd*` subset that `ico2/fumi/ios/cdvd.c` and
`ico2/seki/src/FileManager.c` call (`port/compat/libcdvd.h` declares them) over `ico_vfs_disc()`:
`sceCdInit`, `sceCdMmode`, `sceCdDiskReady`, `sceCdStatus`, `sceCdGetDiskType`, `sceCdGetError`,
`sceCdBreak`, `sceCdSearchFile`, `sceCdRead`, `sceCdReadIOPm`, `sceCdSync`, `sceCdSyncS`,
`sceCdReadClock` and the stream calls `sceCdSt*` and `sceCdStream`.

- `sceCdSearchFile` fills libcdvd's 0x24-byte record (LSN, size, name, the date in libcdvd's
  order). `sceCdGetDiskType` reports a PS2 disc only when the disc holds the boot file `SYSTEM.CNF`
  names (`ico_cdvd_host_boot_name`): its row's `cd_type`, a DVD (`0x14`) for the European and Korean discs and
  a CD (`0x12`) for the Japanese one, which its `cdvd.c` waits for. The drive never reports a tray-open or not-ready state while a
  disc is mounted.
- `sceCdRead` copies the sectors at once; `sceCdReadIOPm` copies them into `ico_iop_ram` at the IOP
  address it is given, and logs `cdvd: sceCdReadIOPm to 0x... (N sectors) is outside IOP RAM` for
  an address outside it.
- Completion follows the drive's timing at [vsync](ARCHITECTURE.md#terms) grain: the buffer is
  full when the call returns (no caller can see it before syncing), but a non-blocking
  `sceCdSync` (odd mode) reports the command busy until the next simulated vsync, and a blocking
  one (even mode) from a game thread blocks that thread until that vsync, as libcdvd's
  `sceCdDelayThread` loop does, so the other threads, the same-priority Main among them, run
  while the drive works. `ico_cdvd_host_vsync` completes the command; `sceCdInit` registers it with
  the host loop (`port/platform/host_loop.h`) when that exists, and tests call it themselves. From
  the [host context](ARCHITECTURE.md#terms) the wait completes at once.
- The stream calls read the requested sectors straight from the disc, so the ring is always full
  and `sceCdStStat` reports its size; pause and resume change nothing.
- `sceCdReadClock` reports the port clock in BCD: 2002-01-01 00:00:00 unless `ICO_FIXED_CLOCK`
  is `0`, and the host's local time then (`port/platform/clock.c` (`ico_clock_now`)).
  `ico_ini_export`, run by `ico_ini_load`, sets the variable from `[dev] fixed_clock`, whose
  default is the fixed clock in the headless build and in trace runs and local time otherwise
  (`port/platform/host_config.c` (`ico_ini_export`, `ico_host_fixed_clock`)). A test can set its
  own source with `ico_cdvd_host_set_clock_source`.
- With no disc in the slot, `sceCdInit` mounts `ico_cdvd_host_default_iso()`: `ICO_ISO`, else
  `baserom/Ico_PAL.iso` (tests and developer runs). `ico_cdvd_host_stats` feeds the diagnostics
  heartbeat.

The game's side keeps its own code with a few host lines in `ico2/fumi/ios/cdvd.c`: the stream
thread's record is sized for the host's wider `IOSThread`, the stream ring pointer and the request
record's address are pointer-wide (`ICO_WORD`). `file_Init` in `ico2/seki/src/FileManager.c` skips
the `IOPRP224.IMG` reboot and the IRX module loads (there is no IOP) but keeps `file_WaitDisc`, so
a missing disc stops boot there as an empty drive did.

## SIF and IOP RAM

There is no [IOP](ARCHITECTURE.md#terms). `port/data/sif_host.c` answers the
[EE](ARCHITECTURE.md#terms) side of the SIF (`port/compat/sifrpc.h`, `sifdev.h`):

- The IOP reboot and module loads (`sceSifRebootIop`, `sceSifResetIop`, `sceSifLoadModule`, ...)
  report success; `sceSifLoadModule` hands out increasing module ids.
- `sceSifAllocIopHeap` and `sceSifFreeIopHeap` use `ico_iop_heap_alloc` and `ico_iop_heap_free`
  (`port/data/iop_ram.h`): first fit, 16-byte rounding, between `ICO_IOP_HEAP_BASE` (0x10000; below
  it stands for the IOP kernel and modules) and the end of the 2 MB `ico_iop_ram`. Address 0 is
  never handed out, so the game's "0 means the allocation failed" tests hold.
- `sceSifSetDma` copies each record's bytes from the EE address to `ico_iop_ram` at once and
  returns a nonzero id; `sceSifDmaStat` always reports the transfer finished; a record outside IOP
  RAM fails the call (0).
- An RPC server is a host function registered with `ico_sif_register_server(sid, fn)` (eight at
  most). `sceSifBindRpc` answers with a token for a registered server, `sceSifCallRpc` runs the
  server synchronously and copies its reply into the receive buffer (zeros for a `NULL` reply), and
  `sceSifCheckStatRpc` always reports the call done. The one server is the sound driver,
  `ico_sndn2_host_serve` (`port/audio/sndn2_host.c`). `ico_sif_host_last_rpc` feeds the heartbeat.

IOP addresses are the game's: an `int` from `sceSifAllocIopHeap` handed to `sceCdReadIOPm`,
`sceSifSetDma`, `SgStAdpcmOpen` and the sound driver. `ico_iop_ptr` turns one into a host pointer
(after `ico_iop_phys` strips the kseg bits) and `ico_iop_addr` turns a pointer back.

`port/data/sifdev_host.c` gives the sifdev file calls (`sceOpen`, `sceClose`, `sceRead`,
`sceWrite`, `sceLseek`) the development kit's `host0:` device, which the development build's tools
used (way data, camera sets, particle files, stage settings, `debug.c`'s option table and
snapshots): `host0:object/stagesetting/st04a.ssb` is `<pref>/dev/object/stagesetting/st04a.ssb`.
Backslashes become slashes, a leading `./`, `/` or `\` and a `;1` suffix are dropped, a `..`
component is refused, and `SCE_CREAT` makes missing folders. Any other device fails with -1.
Descriptors are the lowest free number from 0, as the kit handed them out.

## The data tables

The game's 73 data tables are defined empty on the host (`port/data/gen/table_defs.c`) in one
layout, PAL's (the port's headers), and filled before the game starts by `load_tables`
(`main_host.c`), which calls `ico_tables_load_vfs(vfs, set, ...)` (`port/data/tables.h`): it reads
the set's boot ELF (`SCES_507.60`, `SCPS_110.03`) from the mounted disc and decodes every record
field by field into the host layout, as the disc's descriptor set describes it
(`port/data/gen/<slug>/table_desc.c`: `IcoTableSet`, `IcoTableRow`, `IcoTableRecord`,
`IcoTableField`):

| field kind | how it is filled |
| --- | --- |
| `ICO_TF_INT`, `ICO_TF_FLOAT`, `ICO_TF_BYTES` | copied (the EE is little-endian, as every host the port builds for; `tables.c` refuses to compile otherwise) |
| `ICO_TF_BITS` | extracted from its unit and stored through the field's setter |
| `ICO_TF_FUNC` | the host function the set's address registry (`port/data/gen/<slug>/ee_symbols.c`) names at that EE address |
| `ICO_TF_DATA` | the member's own string pool (`staffroll_dat`), else the registry's host object |
| `ICO_TF_BITMAP` | (Japanese set) a 4-byte host bit word built bit by bit: host bit i is bit `map[i]` of the disc's bit words, 0xFF leaves it 0 |
| `ICO_TF_DEFAULT` | (Japanese set) a PAL field the disc's record lacks: its declared value |

`ico_tables_load_elf` finds the `.data`, `.rodata` and `.sdata` sections, checks every row's range
and its CRC-32 against the set's manifest before writing anything, copies the string pools first
(zeroing the rest of the host buffer), then decodes the records, zeroing a host array's records past
the disc's count. An address the registry does not know fails the load with the table, index, field
and both addresses (`tables: <member> <symbol>[i].<field>: no function is known at 0x...`); another
disc's ELF fails on its first row's CRC, naming the disc the set reads (`... the ELF is not the boot
file of the Japanese disc (SCPS_110.03, SCPS-11003)`). `main_host.c` logs it and ends the run with
`Cannot read the game's data from <source>`. `main_host.c` (`load_tables`) loads the set of the
disc `select_disc` chose (`ico_table_sets[ico_region]`); a good load logs `ico_pc: N data table
rows (N records) loaded from SCPS_110.03 (the Japanese disc)`. It then checks the counts the port
names numbers by (`port/core/region.h`: `stage_count`, `layout_count`, `property_count`,
`anim_none` + 1 for `stageTable`, `mv_orient_rows`, the five sound tables) against
`ico_tables_count`, and ends the run with a box naming the disc when one differs (`ico_pc: the
data tables: <table> has N records on <disc>, the port expects M`). `ico_tables_count(ICO_TID_<symbol>)` gives the records a table
received (`texProperty` 436 on the European disc, 412 on the Japanese, 406 on the Korean), `ico_tables_loaded_set()`
the set. Each record is cleared before its fields are written (the record clear): what no field
writes is 0 whatever an earlier load left, which is what a host-only field the disc lacks, the
elements of an array past the disc's count and a bit-field's default take ([the map
rows](#the-map-rows)). [GAME_CODE.md](GAME_CODE.md#data-tables) covers the generator and the
maintainer's reference test.

### The descriptor sets

`ico_table_sets[]` (`port/data/gen/table_sets.c`) holds one `IcoTableSet` per row of
`config/regions.txt`, in its order: `ico_table_set_pal`, `ico_table_set_jpn` and
`ico_table_set_kor`, each with its boot
file, product name, rows and registry. `load_tables` takes row 0 until the disc identification
picks the row. Every set fills the same host arrays, each as large as the largest set's count of
that table (PAL's on the three discs before the USA one, whose `staffRollNameData` has 1072 entries,
`pairMotion` 33 and `motSyncPairs` 7 against PAL's 964, 31 and 5; the Japanese disc has 944 `objAction`, 3736 `objLayout`, 412 `texProperty`, 1621 `modelData`, 76
`texLayout`, 2 `jimakuFileName`, 5 `movieFile`, 104 `stageData`, 1357 `objTableScene`, 950
`stageTable`, 53 `texFile`); a string pool's buffer is the largest set's (`staffroll_dat`'s
`.rodata` is 3808 bytes on the Japanese disc, 3792 on the European).

The Japanese set maps each field of the disc's record **by path name** onto PAL's host struct
(`tools/gen_data_desc.py`): its EE offset and size come from `config/ee_layout.jpn.txt`, its host
offset from `offsetof` as for PAL. That file is written by `tools/gen_ee_layout.py --version jpn
--upstream <rev>` from upstream's headers at a recorded revision (`git show`), their
`#ifdef VERSION_JPN` blocks resolved (`gen_data_c.version_lines`): one `record` line per type, a
`field` line per leaf (`kind=int|float|bytes|bitword|bits|funcword|dataword`), a `bitfield` line per
named bit of a bit word. A Japanese field with no PAL name, a size or kind that differs, or a PAL
field with neither a Japanese field nor a default, stops the generator. Two records differ:
`MotionDef` (0x190 bytes, its words from 0x144 reordered, no `palSpeedRatio`) and `TexRec` (0x30,
no `cameraMove`). The PAL fields the disc lacks take the `default` rows of
`config/data_schema.jpn.txt`: `palSpeedRatio=1.0` (read only by PAL code under `systemStatus[0]`,
the 50 Hz mode, and the identity of the multiplication there) and `cameraMove=0` (compared only by
PAL code with `NonLinearCameraMove`, a word the Japanese ELF does not have).

`MotionDef`'s three bit words (`modeBits`, `flags`, `flags2`, at 0x184/0x188/0x18C on the Japanese
disc, 0x188/0x18C/0x190 on the European) keep their bits in other places, and one named bit moves
words (`dropNode4Turn`: `flags` bit 9 on the Japanese disc, `flags2` bit 9 on the European). Each
host word is an `ICO_TF_BITMAP` field whose map comes from the named bit-fields (by path, else by
name over the record's bit words) and from `config/motion_bits.jpn.txt`: the bits game code reads by
shift or mask (`modeBits.word >> 19 & 7`, `->flags2 >> 3 & 1`, a view's `modeBits.h.hi & 7`, the
sign test `(int)->flags >= 0`). `tools/gen_ee_layout.py --bits` writes that file from upstream's
sources: each `VERSION_JPN` block's reads are paired with its PAL arm's, read for read, a read
outside any block is the same bit on every disc, the few blocks it cannot pair are listed and
checked by hand (`UNPAIRED`), and a bit no code reads is mapped only by hand with its evidence
(`HAND_ROWS`). The European set copies the three unions' bytes as before. On the two ELFs the
mapped Japanese `motionKind` equals the European one in every copied field and every mapped bit of
all 1148 motions; only `palSpeedRatio` differs (1.0 against 0.85 in 6 motions).

The Korean set (`port/data/gen/kor/`) is the Japanese one at the Korean link's addresses. The
Korean build is the Japanese program (upstream's `docs/TARGETS.md`, "Korean retail"), so its record
layouts are the Japanese ones: `config/ee_layout.kor.txt` and `motion_bits.kor.txt` equal the
Japanese files but for their headers and recorded revision, and the one hand bit row (`flags` bit 2,
the disc's bit 6) holds because the Korean ELF's 1148 `flags` words equal the Japanese ELF's. Its
schema differs from the Japanese one in one count: `texProperty` has 406 rows (412 on the Japanese
disc), because `la_boot_no_memory_card` and `la_boot_no_free_area` hold two rows each where the
Japanese hold five, so every property row from the Japanese 11 on is 6 lower and
`texture-layout`'s 76 ranges and default items move with them. Of the 75 member rows, 72 hold the
Japanese bytes word for word (a pointer word compared as an address); the others are
`tex-property`, `texture-layout` and `staffroll_dat`'s `.rodata` (the copyright year 2002 for 2001).
Every address differs from the Japanese ELF's (`texProperty` 0x30A6D0, `texLayout` 0x530138,
`stageData` 0x5EFB10, `stageTable` 0x5FC998), and `hintTable` lies after `game-param`, as on the
European disc. `port/core/region_kor.c` names its rows by these tables.

`gen_data_desc.py --check` (the `data_desc_fresh` test) also audits the game sources: each read of
MotionDef's bit words by shift or mask in `ico2/` must name host bits every non-PAL set fills; a
shift by `PAL_JPN(` is an error (a bit position is a layout, and the host's is PAL's); and so is such
a read inside a region's own arm (`if (ICO_JPN) { ... }`, the arguments after the first of
`PAL_JPN(`), which would read the disc's position in the PAL host word.

### The map rows

A disc's record maps onto PAL's host record by path name (`tools/gen_ee_layout.py` `record_map`,
shared by the descriptor sets and by `gen_data_c.py --version`'s reference). Where the name alone
does not settle a field, the disc's `config/data_schema.<slug>.txt` says how in its port section,
one row per field; a row that settles nothing is refused, and so is a field no rule and no row
settles (the error names the row that would). The rules and rows, each with the USA disc's own case
(`config/data_schema.ntsc.txt`) as its worked example:

| case | rule or row | the loader | the USA disc |
| --- | --- | --- | --- |
| same path, kind, size and count | copied | `ICO_TF_INT` .. `ICO_TF_DATA` at the disc's offset | most fields |
| a struct bit-field of the same name and width elsewhere in its unit | copied from the disc's bit (reported) | `ICO_TF_BITS` with the disc's bit | `ActModeRec.carried`: bit 0 on PAL, 11 here; `LtProperty.masked`, `defaultMask` |
| a PAL field the disc lacks | `default <member> <field>=<value>` | `ICO_TF_DEFAULT`; a bit-field's or an array's default is 0, the record clear (no descriptor field) | `default tex-property ownerItem=-1` (PAL's "none"); `default motion-def eff[5].t=-1.0` (a frame no motion reaches) |
| a scalar array the disc holds fewer elements of | `default <member> <field>=0` | the disc's elements copied, the rest 0 (the record clear) | `default parallel-motion-tbl motion=0`: `motion[37]` here, `motion[44]` on PAL; `default texture-path path=0`: `char path[32]`, PAL's 48 |
| an integer the disc holds wider than PAL's field | `narrow <member> <field>` | `ICO_TF_BITS` of the host's width from the disc's word; `gen_data_desc.py --manifest` refuses an ELF one of whose values does not fit | `narrow motion-def dirFrames`: an `int` here, PAL's `short`; every one of the 1087 values fits (90, 360, small counts, -1) |
| a field the disc has and PAL lacks, which converted code reads | `extend <member> <field>`, the field in the port's header after PAL's own (`PC port: host-only`) | copied by name on the disc that has it; on every other disc (PAL's included) no descriptor field, 0 by the record clear | `extend model-path modelPath` and `shadowPath` (`PObjMdl`'s two 64-byte paths, `charFileManager.h`); `extend act-mode-def holdDir`, `saveInfo`, `pullUp`, `stopMail` (`ActModeRec`'s bits 1, 3, 6, 8 here, read by the USA arms of `ACTGame_BeforeFunc`, `ACTGame_SaveActorInformation`, `handoff_heroin` and a stopped enemy's mail, as upstream's header names them) |
| a field the disc has that no code the port compiles reads | `drop <member> <field>` (reported; the divergence is recorded below) | not loaded | `drop tex-property word10`, `word3C`; `drop act-mode-def bit4`, `bit13`; `drop motion-def path` |
| an unsigned struct bit-field the disc holds narrower | copied at the disc's width, the host's high bits 0 (reported) | `ICO_TF_BITS` with the disc's bit and width | none here (the June 2001 demo's `StgPre.kidnapSeconds`: 8 bits, PAL's 9) |
| a disc field PAL holds under another name | `rename <member> <field>=<PAL field>`; the PAL field may be one element of a scalar array, `path[i]`, and every element then needs a row (the disc's separate fields gathered) | copied into that field; a gathered array is one field per element (`rootOffset[0]` ..) | none here (the June 2001 demo's `MotionDef.word0FC`, its `word100`, and `rootOffsetX/Y/Z`, PAL's `rootOffset[0..2]`) |
| a table PAL has none of | `extra <member> <type> <count>` (`-` for a single object; the member's own schema row names its header and symbol) | the host array holds `count` records of `type`, defined after PAL's tables with an id after theirs; a disc without it loads none; a pointer word into one of the set's tables (no registry object) resolves to that record of its host array (`gen_data_desc.py --manifest` refuses a word off a record's start) | none here (the June 2001 demo's icon record and enemy sets use it) |

`extend` rows are read from every `config/data_schema.*.txt`, so the PAL set and every other set
know the host-only fields whatever `config/regions.txt` lists: PAL's own descriptors assert its
fields' EE offsets and the host size (`PObjMdl` 0x10C on the host, 0x8C on the disc; `ActModeRec`
0x54 and 0x50), its rows read records at the disc's size, and `gen_data_c.py`'s PAL reference
writes the host-only fields as 0 (`pal_maps`). A host-only field must follow every field of PAL's
record; `config/struct_classes.txt` gives both records their host size for the layout asserts. A
record another declaration views at a fixed size cannot grow this way: `MotionDef`'s three views
(`MotEnt` in `charFileManager.c`, `ScnPre` in `icoMisc.c`, `ChainParaRow` in `chain.c`, each 0x194
bytes) are why the USA disc's `MotionDef.path` is dropped, not extended.

The worked example of `extra`, checked on a scratch copy and not committed: with
`unmapped_00533FC0` (`MotSyncPair motSyncPairs[]`) taken out of PAL's, the Japanese and the
Korean schemas, members and manifests, the row `extra unmapped_00533FC0 MotSyncPair 8` in the USA
schema gives `MotSyncPair motSyncPairs[8];` in `table_defs.c` after PAL's tables (and `boy.h`
among its includes), `ICO_TID_motSyncPairs` after PAL's ids, and a USA row loading 7 records of
the host's 8; the other three sets have no row for it.

The divergences the USA disc's `drop` rows record: `LtProperty.word10` and `word3C` have no C
reader on any disc (upstream's header says so); `ActModeRec.bit4` and `bit13` are set in no USA
mode and read by nothing ("nothing in September reads it"); `MotionDef.path` (the motion file's
name, 64 bytes at 0x88) is read only by `_ACTDebugPrint`, the developer overlay, which shows no
path on this disc until the merge extends the record with its views.

A count-of row (`staffroll_dat`'s `staffRollNameDataNum`) is derived on the host from its array's
size as the PS2 build derived it while every set agrees with it; when a set's count of the array
differs from the host array's or its `less` from another set's (the USA disc's 1072 entries with
none less, PAL's 964 less two), every set loads the int from its own ELF instead: a record row of
`int` at the symbol's offset after the pool row, `ICO_TID_staffRollNameDataNum` among the ids, and
`int staffRollNameDataNum;` in `table_defs.c` without its derivation.

### The USA disc's set

`port/data/gen/ntsc/` is the North American disc's set (SCUS-97113, `SCUS_971.13`). It was
written before `config/regions.txt` listed its row; the three 2001 rows landed together with the
June 2001 demo's set, and `PENDING_ROWS` is empty since. The pre-commit hook's `gen_data_desc.py --check`
refuses a row without its set, and the rows of the three 2001 targets land together with the last
of them; until then `gen_data_c.py` `PENDING_ROWS` holds the three rows exactly as they will be
listed, and the generators take one of their slugs: `gen_ee_layout.py --version ntsc`,
`gen_data_desc.py --manifest --version ntsc`, `gen_data_desc.py --version ntsc` (only
`port/data/gen/ntsc/`, as the table with the pending rows up to `ntsc` gives it; the shared files
stay the listed rows'), `gen_data_desc.py --check --version ntsc` (the `data_desc_fresh_ntsc` test)
and `gen_data_c.py --version ntsc`. A slug `config/regions.txt` lists takes its row from there, so
the list has no effect once the rows land, and is removed then; the regenerated shared files
(`table_desc.h`, `table_defs.c`, `table_sets.c`, `tables.cmake`) and the other sets' pool sizes,
host counts and stubs then follow the six rows.

The USA build is a program of its own, older than the European one: 70 data members (no
`hintTable`, no `jimakuFileName`; `enemy-def` is committed source upstream, `enemyKind.c`, and no
set is written for it), eight record types laid out otherwise (`ParallelMotionRow`, `LtProperty`,
`ItemBreakRec`, `PObjMdl`, `MotionDef`, `LayoutClothDef`, `TexRec`, `ActModeRec`), and MotionDef's
bit words in other places (`config/motion_bits.ntsc.txt`, below). Its rows of the map:
`tex-property` defaults `ownerItem` -1 and `word14`, `word40`, `selectMode`, `selectable` 0 (read
only by PAL's arms) and drops `word10`, `word3C`; `item-kind-def` defaults `stayMode`, `breakMode`,
`hitMode` 0 (the USA record has none, so no USA code reads them);
`texture-path` takes its 32 path bytes and `cameraMove` 0; `model-path` extends the two paths;
`act-mode-def` defaults `bit1`, `attractWait`, `hintCount`, `bit5`, `bit7`, `bit9` 0 (the girl's
attract wait and hint count are PAL's; no hint system here), extends four bits and drops two;
`motion-def` defaults the effect slots 5..11 and the vibration slot 1 (-1.0 and 0: the USA record
holds 5 and 1), `palSpeedRatio` 1.0, `girlDirFrames` 0 (the USA record has none: its code reads
`dirFrames` for both actors), narrows `dirFrames` and drops `path`; `layout-cloth-def` defaults `pt[2]`, `pt[3]` 0 (two
cloth points here, four on PAL); `parallel-motion-tbl` takes its 37 motions (the USA readers loop
to 37).

`config/motion_bits.ntsc.txt`: the named bit-fields map by name (`carried` flags bit 7 on PAL, 10
here; `dropNode4Turn` PAL flags2 bit 9, flags bit 5 here; `fuchiMode`, `handIK`, `dirAdjust`, and
the rest); upstream's blocks pair the shift reads, and for this disc the pairing reads only the
reads whose statement names a motion record (`subject_reads`, as `motion_reads` already did for
the shared lines: a pad's or a texture's `flags & 8` in a block once paired with a motion's). The
blocks that do not pair one for one are function twins; each is listed with where its bits come
from (`UNPAIRED_BY_SLUG`), and the bits only they give are hand rows (`HAND_ROWS`), each with its
reader and the evidence of the two ELFs: of the 1042 motions whose names are unique on both discs,
the number that set the PAL bit and the USA bit alike (all 1042 for flags bits 2, 5, 9 and 25;
1037 for the sign bit, USA bit 28). Six PAL bits have no USA counterpart and are written 0
(`ntsc=-`): `chainHang` (flags 11), flags 12, 13, 14, 15 and `jumpHit` (flags 16), each read only by
PAL's twin of a function the USA disc writes otherwise; the audit counts such a bit as filled. Every
row was checked against that column comparison: no row disagrees with it, and where the column is
empty on both discs (modeBits 11, 12, 15; flags 30) the row is the code's pairing alone.

`port/core/region_ntsc.c` is its region file (its numbers resolved by name against this disc's
tables and sources).

### The August 2001 preview disc's set

`port/data/gen/aug6/` is the preview disc's set (`PROTO-20010806`; it boots `SCUS_971.13` like the
USA disc, and `ico_disc_identify` tells the two apart by the ELF's SHA-1), written before its row
is listed, with the same `--version aug6` commands and `data_desc_fresh_aug6` and
`ee_layout_fresh_aug6` tests as the USA disc's ([above](#the-usa-discs-set)). It is an earlier build of the game:
69 data members in 71 rows (`staffroll_dat` has two), none of PAL's
three unmapped runs (`hintTable`, `motSyncPairs`, `jimakuFileName`) and no `enemy-def` (source
upstream, as for the USA disc), 864 functions and 1 object in the manifest, 5339 function and 3001
data symbols (`--base-elf`'s byte check passed; 70 data-member symbols sit at their member-table
addresses).

Nine record types are laid out otherwise (the tenth, `EnemyDef`, is source upstream). Seven are
the USA disc's and take the USA disc's rows of the map, the same for the same field
(`parallel-motion-tbl`'s 37 motions, `item-kind-def`, `model-path`'s two extended paths,
`texture-path`'s 32 bytes, `layout-cloth-def`'s two points, `motion-def`'s five effect slots, its
`dirFrames` narrowed and its dropped `path`), with one difference in `tex-property`: the record is
the USA disc's 0x6C, but its word at 0x10 and the one at 0x3C are named `word14` and `word40` as
PAL's (no C reader of either), so they are copied by name and no `drop` row is needed;
`ownerItem` (-1), `selectMode` and `selectable` (0) are defaults as the USA disc's. Its
`motion-def` has the same layout as the USA disc's (`MotionDef` 0x190, the name at 0xC8) and 1089
rows (USA 1087).

The other two are this build's own:

- `stage-all`: `StgPre` is 0x190, PAL's fields to 0x15B, then the words after it in another order
  (copied by name) and no `ledgeRange`, which only PAL's arms of `act-env.c` read (`default
  stage-all ledgeRange=0.0`: a range of 0 never matches).
- `act-mode-def`: the flag word at 0x4C has nine bits (`escort` 0, `holdDir` 1, `skipMarked` 2,
  `saveInfo` 3, `bit9` 4, `softIk` 5, `onChain` 6, `bit1` 7, `carried` 8); `bit9` and `bit1` are
  PAL's bits of those names, moved, and the words at 0x44 and 0x48 are exchanged (copied by name).
  `holdDir` and `saveInfo` are two of the host-only bits the USA disc's rows extend (this disc has
  those two and neither of `pullUp` and `stopMail`, which are 0 here by the record clear). PAL's
  `attractWait`, `hintCount`, `bit5`, `keepHand`, `bit7`, `bit10`, `bit12` and `bit14` have no bit
  here and default 0.

`config/motion_bits.aug6.txt` has the same rows as the USA disc's, bit for bit. The unpaired blocks
are the same functions' twins (`UNPAIRED_BY_SLUG` carries the preview disc's own keys), and the
hand rows (`HAND_ROWS`) were checked on the two ELFs the same way: of the 1042 motions whose names
are unique on both, the number that set the PAL bit and the preview bit alike is all 1042 for
flags bits 2, 5, 9 and 25 (the preview disc's `_ACTCorrectMsg` reads bit 8 for PAL's 5 in cases 221
and 222, bit 12 for PAL's 9 in case 240, the landing test reads bit 19 for PAL's 25) and 1037 for
the sign bit (preview bit 28, `ACTGame_BeforeFunc`). The six PAL bits that have no preview
counterpart (`chainHang` 11, 12, 13, 14, 15 and `jumpHit` 16) are written 0; for `jumpHit` the
one preview bit that agrees in all 1042 motions (flags2 bit 3) is the counterpart of PAL's
flags2 bit 6, read in `boyact.c`, set in the same two motions, and the preview disc's
`actCommonJump` sends a mail and reads no bit.

`config/data_schema.aug6.txt` is upstream's, unchanged, with the port's rows appended;
`config/data_members.aug6.txt` is upstream's. `port/core/region_aug6.c` is its region file.

### The June 2001 magazine demo's set

`port/data/gen/jun26/` is the demo disc's set (`SCUS-97158`; its BOOT2 is the magazine's menu
`SCUS_971.58`, the game is `CUS97113/SCPS_000.00`, whose tables the set reads: the schema's
`elf CUS97113/SCPS_000.00` row gives the set's `boot_elf` where the BOOT2 file is not the game's;
[the path rule](#a-disc-whose-game-is-in-a-folder-the-path-rule)), written before its row is
listed, with the same `--version jun26` commands and the tests `data_desc_fresh_jun26` and
`ee_layout_fresh_jun26`. It is a program of its own, two months older than the USA one: 119 data
members in 121 rows (69 of PAL's, `staffroll_dat` with two; none of PAL's three unmapped runs;
no `enemy-def`, source upstream), 823 functions and 2 objects in the manifest (`enemymodel01`,
EnemyInit.c's own set list, and `scpDummyGObj`), 5115 function and 3144 data symbols
(`--base-elf`'s byte check passed; 120 data-member symbols sit at their member-table addresses).
`model-path` holds 1474 records of 0x14C (PAL 1637 of 0x8C).

Fifty members are tables PAL has none of (the `extra` rows):

- `mc-icon-sys` (`sceMcIconSys`, `mcdata.h`): mcdata.o's icon.sys record, built into the program.
  Upstream's code reads it nowhere: the demo's `iosMcIconWriteIconsys` writes the icon.sys its
  DATA.DF holds (`iconFile`'s `icon/icon.sys`, the DATA.DF's 49th member, 964 bytes, another
  record than this one). It is loaded for the port's save code to name, and nothing reads it yet.
- the 49 enemy-set tables of `EnemyInit.o` (`enemy-model02-kindlist` .. `enemy-set-table`,
  records `EnemyKindRec`, `EnemyModelSet`, `EnemySet` and the kind lists and position tables, all
  in `EnemyInit.h`): the demo's `enemy_Initialize` reads `enemySetTable`'s nine rows
  (`EnemyKindNum` 9, upstream EnemyInit.c's `VERSION_JUN26` arm). Row 0 is EnemyInit.c's own
  `enemymodel01` (a registry object); the other rows point at the set lists, the lists at the
  sets, the sets at their kind records and position tables, the kind records into the kind
  lists. Those pointers have no registry objects: the loader resolves each to the record of the
  host array it points at (`tables.c` `resolve_data`), and a kind list of several names is one
  host array named by its first (`enemymodel02Kind16[580]`; the reference writes it the same
  way, `&enemymodel02Kind16[2]` for `enemymodel02Kind17`). EnemyInit.c's PAL table keeps its own
  static `enemySetTable` (one row); the demo's is `table_defs.c`'s, which the merge's JUN26 arm
  reads.

Records laid out otherwise, and their rows (`config/data_schema.jun26.txt`'s port section, each
commented with its readers): `ParallelMotionRow`, `TexRec` and `LayoutClothDef` take the USA
disc's rows; `LtProperty` (0x58, from `texData`: PAL's `word0` (-1), `word4`, `word8`, `wordC`,
`word14`, `word40` and the PAL-only `ownerItem`, `selectMode`, `selectable` default, `word28`
dropped); `ItemBreakRec` (0x10: no modes and no `hitAnim`, which only `BreakItemWithAttackHit`
reads, a function the demo does not have); `PObjMdl` (the USA disc's two extended paths, then a
cloth name and a 32-byte name, dropped: no C reader); `ActModeRec` (the August 2001 preview's
record); `SeBank` (`hdPath[32]`, PAL 48); `StgPre` (0x18C: no `word148`, `word160`, `word17C` or
`ledgeRange`, read only by PAL's arms; `kidnapSeconds` 8 bits; the demo's `attrTopGirl`, read by
`ACTGame_SetActors_Debug` where PAL's arms read `flag0`, renamed onto `flag0`, and the demo's own
`flag0` dropped); `MotionDef` (0x184: five effect and five sound slots and one vibration slot,
the rest -1.0 and 0; `palSpeedRatio` 1.0, `girlDirFrames` 0, both read only by PAL's arms;
`dirFrames` narrowed; `rootOffsetX/Y/Z` gathered into `rootOffset[0..2]`, which the demo's arm of
`rootUpdateDirectPlay` reads exactly as PAL's reads the array; `word0FC` renamed onto `word100`,
the two arms of `CheckEnemyBrainMode` testing them alike; the Japanese `comment` and the motion
`path` dropped; no `flags2` word, its bits in `flags`).

`config/motion_bits.jun26.txt`: the scanner pairs most reads; `UNPAIRED_BY_SLUG` explains the
demo's twins in the blocks that do not pair (thirteen keys) and `HAND_ROWS` gives the bits only they give, checked on the two ELFs:
of the 1028 motions whose names are unique on both (the demo has 1168, PAL 1148), the number that
set the PAL bit and the demo's alike is 1026 for the call status field (PAL modeBits 16-17, the
demo's 27-28, a two-bit field: PAL's bit 18 has no counterpart), 1023 for the sign bit (the
demo's flags bit 14, `ACTGame_BeforeFunc`), all 1028 for the landing test (PAL flags 25, the
demo's 20) and 1027 for `findChainInJump`'s IK rate (PAL flags2 bit 5, the demo's
`chainIkRate`, flags bit 3). Fourteen PAL bits are written 0 (`jun26=-`), each read only by
PAL's twin of a function the demo writes otherwise: modeBits 18, flags 4, 5, 6, 8, 9, 10, 12, 13,
14, 15 and 16 (`jumpHit`) and flags2 6 and 8. Two of them are named on both discs and still not
the same bit: the bit upstream's header names `jumpHit` on the demo (flags 16) is PAL's flags bit
28 by `act-env.c`'s pair (all 1028 agree; the demo's `actCommonJump` reads no bit), so a zero row
takes PAL's `jumpHit` off the name mapping; and the demo's `chainHang` (flags 11), which its
`findChainInJump` reads where PAL reads flags2 bit 8 (all 1028 agree), fills the host's
`chainHang` by name, so flags2 bit 8 is 0 on this disc (a disc bit fills one host bit): the
merge keeps the demo's arm's named read. Named bits whose data differ on the two ELFs (the demo
is a prototype): `carried` (PAL flags 7, 3 motions; the demo's flags 7, 7 motions, the same 7 as
PAL's unnamed bit 2) and `handIK` (PAL modeBits 26-27, the demo's 29-30, 994 and 1011 agree);
each follows the demo's code, which reads its own bit for the purpose PAL's code reads PAL's.

`config/data_schema.jun26.txt` and `config/data_members.jun26.txt` are upstream's, unchanged, with
the port's rows appended to the schema. `port/core/region_jun26.c` is its region file.

### The Asian disc's set

`port/data/gen/cn/` is the Asian retail disc's set (`SCPS-55001`, `SCPS_550.01`, SYSTEM.CNF
`VER = 1.02`, a DVD), made with the commands of [the Korean set](#adding-a-targets-set)
(`--version cn`, upstream 9bd61a825, `baserom/cn/baseelf.elf`, the decompilation's `build/cn`
link for the symbols) with its row pending in `gen_data_c.py` `PENDING_ROWS`, and landed with
`config/regions.txt`'s seventh row. It is the Korean program with the Japanese records and data:
74 member rows in 72 members, no `enemy-def` (source upstream). Written by `gen_data_c.py
--version cn` in the PAL record's form, all 72 members' C equals the Japanese ELF's member for
member, and differs from the Korean ELF's in three (`tex-property` with the Japanese 412 rows,
`texture-layout`, `staffroll_dat`'s pool). So `config/ee_layout.cn.txt` is the Japanese layout
(71 record types), the schema's port section is the Japanese and Korean one (two `default` rows,
no map row), and `config/motion_bits.cn.txt` equals the Korean rows bit for bit: the `motion-def`
run is word for word the Korean ELF's (459,200 bytes, no differing word), the scan of upstream's
merged sources pairs all but four rows (PAL flags bits 2, 5, 9 and 25), and `HAND_ROWS` gives
those with the Korean disc's bits as their evidence.

The disc is a DVD like the Korean one and its `DATA.DF` has the same shift: the directory lists
`\DFDATAS\DATA.DF` at LSN 3157 and the archive header (`b7000000 "COMMON.DF"`) is 153,600
sectors on, so `df_lsn_shift` is 153,600 and `df_size` 552,847,360 (867,420,160 less the shift in
bytes); 183 members, directory CRC-32 `0xe3f7de26` (`build-host/tmp/kor/df_manifest.py`'s
block). The image is 873,889,792 bytes (SHA-1 `9bc77511b08a78dd9f7f3ddabde07fb5682c51b1`), the
boot ELF 5,489,952 bytes (`f3e784b6811af2de9570be5575d6db5962300bcd`, equal to the decompilation's
`build/cn/ico.elf`). The memory card directory is `BISCPS-55001ico`, the product file the
Japanese 0x1E8 record. The disc's menu sheets (`Tex_menu01`) are lettered in Traditional Chinese;
`region_cn.c`'s `ui_lang_default` (English) and `menu_sheet` (the Japanese sheets' look) are
stand-ins until the port has Chinese text and a measured sheet look.

`config/data_schema.cn.txt` and `config/data_members.cn.txt` are upstream's, unchanged, with the
port's two rows appended to the schema. Its row is the seventh and last of `config/regions.txt`
because upstream's `LN` takes its arguments in the order the targets were added (pal, jpn, kor,
ntsc, aug6, jun26, cn): `region_blocks.py rename --base cn=kor` took the sixth-row tree to seven
rows (`PAL_JPNKOR` is `PAL_JPNKORCN`, `ICO_JPNKOR` is `ICO_JPNKORCN`, and every `LN(...)` gained
the Korean value as its seventh argument, the Asian build having the Korean numbers but for
`pad.c`'s 18 Japanese ones, which the merge sets). `port/core/region_cn.c` is its region file.

### Adding a target's set

For a new row of `config/regions.txt`, the data step of adding a target ([GAME_CODE.md](GAME_CODE.md)
has the rest). A row the table does not list yet is first added to `gen_data_c.py` `PENDING_ROWS`
([the USA disc's set](#the-usa-discs-set)), and step 5 is then `gen_data_desc.py --version <slug>`:

1. `tools/gen_ee_layout.py --version <slug> --upstream <rev>` writes `config/ee_layout.<slug>.txt`
   (upstream's `config/data_schema.<slug>.txt` and `data_members.<slug>.txt` copied into
   `config/` first, unchanged, with the port's map rows appended to the schema: [the map
   rows](#the-map-rows); each error of step 5 names the row it needs).
2. `tools/gen_symbol_addrs.py --region <slug> --elf <decomp>/build/<slug>/ico.syms.elf --map
   <decomp>/build/<slug>/ico.<slug>.map --base-elf baserom/<slug>/baseelf.elf` writes
   `config/symbol_addrs.<slug>{,.data}.txt` (with a `TITLE` entry for the slug).
3. `tools/gen_data_desc.py --manifest --version <slug> --elf baserom/<slug>/baseelf.elf` writes
   `config/tables_manifest.<slug>.txt`.
4. If its bit words differ: `tools/gen_ee_layout.py --version <slug> --upstream <rev> --bits`
   writes `config/motion_bits.<slug>.txt`.
5. `tools/gen_data_desc.py` writes `port/data/gen/<slug>/` and the shared files; each error names
   the field, default or bit to settle.
6. `port/data/CMakeLists.txt`: the slug in the `foreach(_slug jpn kor)` lists, which build its
   reference (`gen_data_c.py --version <slug>` from `ICO_BASE_ELF_<SLUG>`, for the members its own
   `data_members.<slug>.txt` lists: `ICO_DATA_MEMBERS` is every link order's union) and add
   `tables_loader_<slug>`, `tables_manifest_<slug>` and `ee_layout_fresh_<slug>`; and its
   reference in `port/data/test/tables_test.c`'s `ref_sets[]`.

The Korean set was made with these commands (upstream c2f4ed40e):

```
tools/gen_ee_layout.py --version kor --upstream c2f4ed40e
tools/gen_ee_layout.py --version kor --upstream c2f4ed40e --bits
tools/gen_symbol_addrs.py --region kor --elf /primary/dev/ico/build/kor/ico.syms.elf \
    --map /primary/dev/ico/build/kor/ico.kor.map --base-elf baserom/kor/baseelf.elf
tools/gen_data_desc.py --manifest --version kor --elf baserom/kor/baseelf.elf
tools/gen_data_desc.py
```

The preview disc's set was made the same way with `--version aug6` and `baserom/aug6`, the June
2001 demo's with `--version jun26` and `baserom/jun26`, the Asian disc's with `--version cn` and
`baserom/cn` (upstream 9bd61a825), and the
USA set (upstream 3947e0641, its row pending):

```
git show 3947e0641:config/data_schema.ntsc.txt > config/data_schema.ntsc.txt   # + the port section
git show 3947e0641:config/data_members.ntsc.txt > config/data_members.ntsc.txt
tools/gen_ee_layout.py --version ntsc --upstream 3947e0641
tools/gen_ee_layout.py --version ntsc --upstream 3947e0641 --bits
tools/gen_symbol_addrs.py --region ntsc --elf /primary/dev/ico/build/ntsc/ico.syms.elf \
    --map /primary/dev/ico/build/ntsc/ico.ntsc.map --base-elf baserom/ntsc/baseelf.elf
tools/gen_data_desc.py --manifest --version ntsc --elf baserom/ntsc/baseelf.elf
tools/gen_data_desc.py --version ntsc
```

`gen_ee_layout.py --version pal --upstream <rev>` lays upstream's PAL arm out against the port's
headers, field by field, as a check at each merge (no PAL layout file is committed: the port's
headers are PAL's layout, asserted against the EE by `port/data/gen/pal/table_desc.c`).
`gen_ee_layout.py --version <slug> --check` (the `ee_layout_fresh_<slug>` tests) compares the
committed layout with its recorded revision when that revision is in the clone.

## DATA.DF for the port's own screens

DATA.DF (`DFDATAS/DATA.DF`) starts with a directory: a count, then 40-byte entries of a 32-byte
name, a byte offset and a byte size. The entries named `*.DF` are packs: a raw deflate stream whose
output is a 16-byte header (the member count first), 0x224-byte member entries (id, kind, a word,
size, name) and the members back to back (`ico2/fumi/ios/cdvd.c` (`iosCdvdMgrPackLoad`)). The
other entries are loose files (`.int` streams, `.smb`, `.pss`, `.jim`).

`port/data/df_pack.h` reads these outside the game's cdvd thread, for the music gallery
(`port/ui/gallery_play.c`) and the disc tests: `ico_df_has`, `ico_df_size` and `ico_df_read` for a
loose file; `ico_df_find_member` and `ico_df_read_member` for a pack member by its full name
(`sound/ICO_SE/com_v.hd`), first match in directory order. The members' index is built once per
volume, on the first look-up, by inflating each pack's header and directory; a member is read by
inflating its pack from the start to the member's end. These calls share one index, the game
thread's: passing them another volume starts it over.

A reader on another thread keeps an index of its own: `ico_df_index_open(vfs)` reads the directory
and indexes every pack at once into an `IcoDfIndex` that shares nothing with the global one or
another instance, and `ico_df_index_entries`, `_entry_name`, `_entry_size`, `_entry_find`,
`_read_entry`, `_pack_count`, `_member_count`, `_member_name`, `_member_at`, `_pack_members`,
`_find` and `_read_member` are the instance's versions of the calls above; members are numbered as
`ico_df_member_name` numbers them (the packs in directory order, each in its own order).
`ico_df_index_read_pack(idx, pack, fn, user)` inflates a pack once, start to end, and hands every
member to `fn` in order with its bytes (one buffer the size of the pack's largest member), where
reading each member alone would inflate the pack from the start once per member; `fn` returning
non-zero stops it (1), a read or inflate error returns -1. `ico_df_index_close` frees it. The
texture walk (`port/render/texpack_walk.h`) reads the disc this way, and the texture export runs it
on its own thread over `ico_vfs_reopen`'s mount ([PACKS.md](PACKS.md#dumping)). Two mounts of one
file are safe: the image backend's state (`Iso`, its `IcoDiscImage`, the CHD's `chd_file`) and the
archive's (`Arch` and its `FILE*`) are per mount, and the only shared static in the backends is
`ico_vfs_iso9660_force_df_shift`'s test override (`port/data/iso9660.c`, `port/data/archive.c`).

## JSON

`port/data/json.h` is a small RFC 8259 reader (library `ico_json`, no dependencies) for the port's
own files: the archive's `meta.json`, glTF model files (`port/render/gltf.c`) and a GPU driver
package's `meta.json` (`port/platform/gpu_driver.c`). `ico_json_parse` copies the text, so the
caller may free it at once; every node lives until `ico_json_free`, which is safe after a failed
parse and idempotent. The accessors (`ico_json_get`, `ico_json_at`, `ico_json_count`,
`ico_json_u64`, ...) accept `NULL` and return `NULL`, 0 or -1 for it, so look-ups chain without
checks. The grammar is strict: one value with optional whitespace and a leading UTF-8 byte order
mark, no comments, trailing commas, leading zeros, `NaN` or control characters, nesting at most
`ICO_JSON_DEPTH_MAX` deep; duplicate keys are kept and `ico_json_get` returns the first.

## Tests

| test | checks |
| --- | --- |
| `vfs_synthetic`, `vfs_disc`, `vfs_disc_jpn`, `vfs_disc_kor` | the VFS and ISO9660 backend on an image the test builds, and on each disc (`ICO_DISC_IMAGE`, `ICO_DISC_IMAGE_JPN`, `ICO_DISC_IMAGE_KOR`; skipped, 77, without it): the boot ELF's SHA-1, DATA.DF's directory, the disk type |
| `vfs_disc_jpn_bin`, `vfs_disc_jpn_cue`, `vfs_disc_jpn_chd` | the Japanese `.iso` against a MODE2/2352 `.bin`, its `.cue` (both made from the `.iso` at test time by `port/data/test/make_raw_cd.py` into the build folder, zero EDC/ECC) and a `chdman createcd` `.chd` of the `.cue` (skipped without `chdman` on `PATH`): the same directory records, boot ELF and DATA.DF SHA-1s, and image SHA-1 (`vfs_test same`, run by `port/data/test/disc_raw_test.cmake`) |
| `archive_synthetic`, `archive_disc`, `archive_disc_jpn`, `archive_disc_kor` | extraction into a temporary archive and its mount, compared with the ISO backend; the disc tests identify the image by its boot file, check both rules hold and `meta.json`'s disc id (skipped, 77, without the image) |
| `archive_manifest_jpn`, `archive_manifest_kor` | the Japanese and Korean DATA.DF manifests in `discs.c` against the images |
| `archive_shift` | the Korean disc's DATA.DF shift on a synthetic image (`archive_test shift`): the look-up returns the sector plus the shift and the size minus it, and refuses a directory not larger than the shift; no disc needed |
| `archive_subfolder` | a disc whose game is in a folder on a synthetic image (`archive_test sub`, the test's own row): without the row only the root's files; with it the folder's ELF, module and DATA.DF stored under their paths, the menu listed without bytes, the root's module and other folder left out, no `hash` phase, the ELF and DATA.DF rule with the ELF's SHA-1 as the source's, `iso-sha1` refused for the row, the game's spellings through `sceCdSearchFile` equal on the image and the archive, `ico_vfs_df_path` and `df_pack.h` reading the folder's DATA.DF, and a changed ELF byte or DATA.DF member refused by the extractor and `ico_extract_verify_files`; no disc needed |
| `iso_import` | the Android copy on Linux with SDL's memory streams: byte for byte, cancel, write failure, the space arithmetic, the 4.4 GB demo's space and copy-failed boxes in the seven string tables (Linux window build) |
| `json` | numbers, nesting and the depth limit, escapes, malformed input |
| `data_desc_fresh` | `port/data/gen/` is what `tools/gen_data_desc.py` writes, every set at once, and the audit of MotionDef's bit reads |
| `tables_loader`, `tables_manifest` | the loader against reference tables generated from the base ELF, and the manifest against the ELF (built only with `ICO_BASE_ELF` and pyelftools) |
| `tables_loader_jpn`, `tables_manifest_jpn`, `tables_loader_kor`, `tables_manifest_kor` | the Japanese (Korean) set against `ICO_BASE_ELF_JPN` (`ICO_BASE_ELF_KOR`), through `ICO_DISC_IMAGE_JPN` (`ICO_DISC_IMAGE_KOR`) when present: every table byte for byte against the reference `gen_data_c.py --version <slug>` writes in the PAL record's form, the records past the disc's count zero, and the disc's ELF and the PAL ELF each refused by the other's set, naming the disc expected; the disc's manifest against that ELF (skipped without it) |
| `ee_layout_fresh_jpn`, `ee_layout_fresh_kor` | `config/ee_layout.<slug>.txt` is what its recorded upstream revision gives (a note and exit 0 when the revision is not in the clone) |

All are registered in `port/data/CMakeLists.txt`.
