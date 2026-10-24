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
| `port/data/gen/` | the generated table definitions (`table_defs.c`), descriptor types (`table_desc.h`), the sets' list (`table_sets.c`, `tables.cmake`) and one descriptor set per disc (`pal/`, `jpn/`, `kor/`: `table_desc.c`, `ee_symbols.c`, `tables.cmake`) |
| `port/data/df_pack.h`, `df_pack.c` | reads of DATA.DF's loose files and pack members for the port's own screens |
| `port/data/json.h`, `json.c` | the JSON reader (`ico_json`) |
| `port/data/chdr_miniz_names.h` | prefixes the second miniz copy inside libchdr |
| `port/data/CMakeLists.txt` | the libraries (`ico_port_data`, `ico_miniz`, `ico_libchdr`, `ico_json`) and the tests |
| `port/platform/android/iso_import.h`, `iso_import.c` | the Android first start's copy of the chosen image |
| `ico2/fumi/ios/cdvd.c`, `ico2/seki/src/FileManager.c` | the game's side: the cdvd manager and the boot loader |
| `config/tables_manifest.txt`, `tables_manifest.jpn.txt`, `tables_manifest.kor.txt` | per disc, the CRC-32 of every table row and the addresses the tables' pointers hold |
| `config/ee_layout.jpn.txt`, `motion_bits.jpn.txt`, `ee_layout.kor.txt`, `motion_bits.kor.txt` | the Japanese and Korean discs' record layouts and MotionDef's shift-read bits (`tools/gen_ee_layout.py`) |
| `config/symbol_addrs.jpn{,.data}.txt`, `symbol_addrs.kor{,.data}.txt` | the names at the Japanese and Korean ELFs' addresses (`tools/gen_symbol_addrs.py`) |

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
block), and every file under `DFDATAS/`, DATA.DF whole. It hashes the whole image, streams each file
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
`ICO_ISO_IMPORT_SPARE_BYTES` (1.2 GB for the extracted data and a margin) are free. The extractor
then reads the copy like any file; once identified, a copy of the Japanese or Korean disc is renamed
`Ico_JPN.<ext>` or `Ico_KOR.<ext>` (`android_name_image`), and `main_host.c` deletes it once the game data is mounted,
unless `keep_image=1` is in `ico-pc.ini`. A cancelled or refused copy is removed.

`use_iso=1` in `ico-pc.ini` (or `[dev] use_iso`) skips the archive and mounts the image directly
with `ico_cdvd_host_mount_iso`; it is the headless build's default, so
[trace](ARCHITECTURE.md#terms) runs and tests read the image (`use_iso_mode`). In that mode the
image is identified by its boot file and its SHA-1 checked against that disc's unless `verify=0` or
`--no-verify` (`verify_iso`); the extractor always verifies. Either way the disc is selected
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
the set. [GAME_CODE.md](GAME_CODE.md#data-tables) covers the generator and the maintainer's
reference test.

### The descriptor sets

`ico_table_sets[]` (`port/data/gen/table_sets.c`) holds one `IcoTableSet` per row of
`config/regions.txt`, in its order: `ico_table_set_pal`, `ico_table_set_jpn` and
`ico_table_set_kor`, each with its boot
file, product name, rows and registry. `load_tables` takes row 0 until the disc identification
picks the row. Every set fills the same host arrays: PAL's counts are the largest of every table
(the Japanese disc has 944 `objAction`, 3736 `objLayout`, 412 `texProperty`, 1621 `modelData`, 76
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

### Adding a target's set

For a new row of `config/regions.txt`, the data step of adding a target ([GAME_CODE.md](GAME_CODE.md)
has the rest):

1. `tools/gen_ee_layout.py --version <slug> --upstream <rev>` writes `config/ee_layout.<slug>.txt`
   (upstream's `config/data_schema.<slug>.txt` and `data_members.<slug>.txt` copied into
   `config/` first, unchanged, with the port's `default` rows appended to the schema).
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
   reference (`gen_data_c.py --version <slug>` from `ICO_BASE_ELF_<SLUG>`) and add
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
| `iso_import` | the Android copy on Linux with SDL's memory streams: byte for byte, cancel, write failure, the space arithmetic (Linux window build) |
| `json` | numbers, nesting and the depth limit, escapes, malformed input |
| `data_desc_fresh` | `port/data/gen/` is what `tools/gen_data_desc.py` writes, every set at once, and the audit of MotionDef's bit reads |
| `tables_loader`, `tables_manifest` | the loader against reference tables generated from the base ELF, and the manifest against the ELF (built only with `ICO_BASE_ELF` and pyelftools) |
| `tables_loader_jpn`, `tables_manifest_jpn`, `tables_loader_kor`, `tables_manifest_kor` | the Japanese (Korean) set against `ICO_BASE_ELF_JPN` (`ICO_BASE_ELF_KOR`), through `ICO_DISC_IMAGE_JPN` (`ICO_DISC_IMAGE_KOR`) when present: every table byte for byte against the reference `gen_data_c.py --version <slug>` writes in the PAL record's form, the records past the disc's count zero, and the disc's ELF and the PAL ELF each refused by the other's set, naming the disc expected; the disc's manifest against that ELF (skipped without it) |
| `ee_layout_fresh_jpn`, `ee_layout_fresh_kor` | `config/ee_layout.<slug>.txt` is what its recorded upstream revision gives (a note and exit 0 when the revision is not in the clone) |

All are registered in `port/data/CMakeLists.txt`.
