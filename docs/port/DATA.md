# Game data: the disc layer

How the port reads the game's disc: the VFS, the ISO9660 backend that reads
the user's image directly, the archive backend and its first-run extractor,
the libcdvd and SIF host layers under the game's unchanged cdvd manager, IOP
RAM, and the loader that fills the game's data tables from the boot ELF on
the disc. No disc data is in this repository (`docs/LEGAL.md`); everything
below is structure, sector numbers and hashes, and the tests read the
user's own image at run time.

## Layers

```
game     fumi/ios/cdvd.c        manager thread, request queue, 7-slot background
                                table, stream manager, 200-entry directory cache,
                                DATA.DF directory (unifile_read_func)
         seki/src/FileManager.c boot: file_Init, file_LoadCDFile
         fumi/sound/adpcm_init.c ADPCM streams into IOP memory (sceCdReadIOPm)
------------------------------- the seam: libcdvd / sifrpc / sifdev symbols
port     port/data/cdvd_host.c  sceCd* over the VFS disc
         port/data/sif_host.c   sceSif* (no IOP), IOP heap, SIF DMA, RPC to host servers
         port/data/iop_ram.c    ico_iop_ram (2 MB) and the IOP heap
         port/data/vfs.c        paths, byte reads, the disc slot
         port/data/iso9660.c    backend: the user's ISO (use_iso, and the extractor)
         port/data/archive.c    backend: the extracted archive ico.o2r
         port/data/extract.c    the first-run extractor that writes it
         port/data/tables.c     the 73 data tables, from the boot ELF on the disc
```

The seam is the SDK's own function names, as declared in
`port/compat/libcdvd.h`, `sifrpc.h` and `sifdev.h`: the game calls
`sceCdRead`, the host build links `port/data`'s definition. `cdvd.c` is not
edited; everything above the seam (the queue, the threads, the cache, the
retry and drive-recovery paths) runs as written. `FileManager.c` has one
host change (below).

## The VFS (`port/data/vfs.h`)

The game addresses the disc as libcdvd does: look a file up by ISO9660 path
and get its first logical sector number (LSN) and byte size, then read
whole 2048-byte sectors by LSN. It also does its own LSN arithmetic: the
DATA.DF directory gives byte offsets inside DATA.DF, and
`unifile_read_func` (`cdvd.c`) caches each member as
`DATA.DF's LSN + offset / 2048`. So a backend serves one flat LSN space
numbered as on the disc, and the VFS keeps sector semantics:

| call | what |
|---|---|
| `ico_vfs_mount(backend, location)` / `ico_vfs_unmount` | open a volume through a backend table |
| `ico_vfs_set_disc` / `ico_vfs_disc` | the volume the libcdvd layer reads |
| `ico_vfs_normalize` | any spelling below to `DIR/FILE` |
| `ico_vfs_stat`, `ico_vfs_open`, `ico_vfs_size` | look-up: LSN, size, ISO date, directory flag, name |
| `ico_vfs_read(file, offset, dst, len)` | bytes, clipped to the file |
| `ico_vfs_read_sectors(vfs, lsn, count, dst)` | whole sectors anywhere on the volume |
| `ico_vfs_lsn_to_offset`, `ico_vfs_offset_to_lsn`, `ico_vfs_size_to_sectors` | sector maths (2048 bytes) |

Path spellings, all case-insensitive: `DFDATAS/DATA.DF`, `\DFDATAS\DATA.DF;1`
(what `chgFileName` in `cdvd.c` and `file_LoadCDFile` in `FileManager.c`
produce), `cdrom0:\SYSTEM.CNF;1` (SYSTEM.CNF and `FileManager.c`'s module
paths). Device prefix, leading separators and the `;version` suffix are
dropped, `\` becomes `/`.

The names the game looks up on the disc itself are few: `\DFDATAS\DATA.DF;1`
(the manager's first request), `\SCES_507.60;1` (the disc check), and
whatever `file_LoadCDFile` is handed. Every other `\DFDATAS\<NAME>;1` the
game asks for (packs, `.int` ADPCM, `.smb` stream motion, `.pss` movies)
resolves in `cdvd.c`'s own directory cache from DATA.DF's directory and
never reaches the VFS by name; a name missing from that cache falls through
to `sceCdSearchFile`, which fails on this disc exactly as on the PS2.

The port's own screens read DATA.DF without the game's cdvd thread through
`port/data/df_pack.h`: `ico_df_has` looks a name up in DATA.DF's directory,
and `ico_df_find_member` / `ico_df_read_member` find a member of the stage
packs (the `*.DF` entries, each a raw deflate stream of a header, the
0x224-byte member entries and the members, loader-census.md section 2) and
inflate it with miniz's tinfl. The index of the 68 packs' 22,808 members is
built at the first look-up by inflating each pack's header and directory.
The music gallery uses it for its sound banks (docs/port/MUSIC.md).

### Backend table

```c
typedef struct IcoVfsBackend {
    const char *name;
    int (*mount)(void **state, const char *location);
    void (*unmount)(void *state);
    int (*lookup)(void *state, const char *path, IcoVfsEntry *out);   /* "DIR/FILE" */
    int (*read_sectors)(void *state, uint32_t lsn, uint32_t count, void *dst);
    uint32_t (*volume_sectors)(void *state);
} IcoVfsBackend;
```

### Backend 1: ISO9660 (`port/data/iso9660.c`)

Read-only, over a host file, no external library. It reads the primary
volume descriptor (ECMA-119 8.4) and walks directories from the root
record (9.1); no path table, no Joliet or Rock Ridge, no multi-extent files
(refused with a message). 64-bit file offsets on every host (`fseeko` with
`_FILE_OFFSET_BITS=64`, `_fseeki64` on Windows). PS2 mastering records a
directory's length as the bytes in use, not a sector multiple; the walk
reads every sector the length touches and stops at a zero length byte in
each.

### Backend 2: the archive (`port/data/archive.c`)

The binary holds no disc data. The first run takes the user's image,
verifies it and extracts what the game reads, once, into a local archive,
`ico.o2r`; every later run mounts the archive (the model Ship of Harkinian
uses for its ROM). The archive exists so that the image is verified once
and then never needed again: the user may delete or move it, a run does not
hash 900 MB each start, and the file the game reads is a known, checked
copy. The window build uses it by default; `use_iso` (below) keeps reading
the image directly.

**Format.** A ZIP (PKWARE APPNOTE; written and indexed with miniz,
`port/third_party/miniz/`, docs/port/THIRD_PARTY.md) whose entries are all
stored, method 0, never deflated: the game reads by sector and the DATA.DF
packs are compressed already. Entries:

| entry | holds |
| --- | --- |
| `disc/<PATH>` | the disc file `PATH` byte for byte: `SYSTEM.CNF`, `SCES_507.60` (the boot file BOOT2 names; 5,515,680 bytes, read whole by the table loader), every root `*.IRX` (`SNDN2DRV.IRX` for the pitch table and `LIBSD.IRX` for the reverb presets, their work area sizes and the idle voice block, docs/port/AUDIO.md; the others are small and kept for later checks), `DUMMY.TXT`, and everything under `DFDATAS/` (`DFDATAS/DATA.DF` whole, 867,184,640 bytes) |
| `tail/<PATH>` | the bytes after `PATH`'s end to the end of its last sector, only when any is nonzero (the PAL disc has none: every selected file's tail is zero), so a sector read returns what the disc held |
| `meta.json` | below |

On the PAL image: 13 files, 872,906,360 bytes, archive 872,910,571 bytes
(4,211 bytes of ZIP headers, directory and meta.json).

`meta.json`:

```json
{
  "format": "ico.o2r", "version": 1, "extractor": "ico-pc extract 1",
  "disc_id": "SCES-50760",
  "source": {"sha1": "<image SHA-1>", "size": <bytes>, "accepted_by": "iso-sha1",
             "elf_sha1": "<SCES_507.60 SHA-1>", "datadf_manifest": true},
  "volume_sectors": 443216,
  "entries": [
    {"path": "", "name": "", "lsn": 261, "size": 1168, "dir": true, "date": [...]},
    {"path": "DFDATAS/DATA.DF", "name": "DATA.DF;1", "lsn": 19771, "size": 867184640,
     "date": [...], "data": "disc/DFDATAS/DATA.DF"},
    ...
  ]
}
```

Each entry is what `iso9660.c`'s look-up returned for that path on the
image (`IcoVfsEntry`: first LSN, size, the 7-byte ISO date, the directory
flag, the name with its `;1`). The root and `DFDATAS` directories are
listed without data, so `sceCdSearchFile` of a directory fails as on the
disc. `volume_sectors` is the image's primary volume descriptor value, so
the end-of-disc checks (`sceCdRead` past the end, the stream's clip) match.

**The backend.** `ico_vfs_mount_archive(path)` (= `ico_vfs_mount(&
ico_vfs_archive, path)`). At mount miniz reads the central directory over a
read callback on the archive file, `meta.json` is parsed (a small JSON
reader in `archive.c`), and every `data`/`tail` entry is checked: present,
stored, not encrypted, exactly the size `meta.json` gives, its data
(local header + name + extra) inside the file. A malformed, truncated or
foreign file fails the mount. Then:

1. `lookup` answers from the entry table (normalized path, exact match).
2. `read_sectors(lsn, count)` maps each run of sectors onto the stored file
   whose disc range (`lsn` .. `lsn + ceil(size / 2048)`) holds it and reads
   it with one `fseek`/`fread` on the archive file; the bytes past a file's
   end in its last sector come from its `tail/` entry or are zero. Runs may
   cross from one stored file into the next. DATA.DF is one stored file
   with its disc LSN, so it stays one contiguous LSN range and `DATA.DF's
   LSN + offset / 2048` (`cdvd.c`'s `unifile_read_func`) reads the same
   bytes as from the image.
3. A sector no stored file covers (the volume descriptors, the directory
   sectors, files not extracted such as `IOPRP224.IMG`, `MAIN.MAP`,
   `SRCFILE.TXT`, `TRFILE.TXT`, `TRTABLE.BIN`, and the 14 sectors after
   `DUMMY.TXT`) fails (`-1`), as does a range past the volume's end.
   `DUMMY.TXT` (LSN 443,201) starts where DATA.DF's last sector ends, so a
   read that runs over DATA.DF's end still finds the disc's bytes there. A
   look-up of a file not extracted fails, where the image would find it; the
   game never asks for one: it searches `\DFDATAS\DATA.DF;1` and
   `\SCES_507.60;1`, its other names resolve in `cdvd.c`'s directory cache
   from DATA.DF's directory, and `file_LoadCDFile` (through `file_LoadFile`)
   has no caller in `ico2/`.

The loader (`tables.c`), the libcdvd layer, `sndn2_host.c`'s pitch table
(`ico_vfs_open(ico_vfs_disc(), "SNDN2DRV.IRX")`), `libsd_irx.c`'s libsd
values (`"LIBSD.IRX"`) and the rest read through
`ico_vfs_disc()` and need no change.

**First run** (`port/platform/main_host.c`, `mount_game_data`):

1. Look for `ico.o2r` in the per-user folder (`ico_host_pref_dir`: the SDL
   pref path in the window build, the executable's folder headless), then
   beside the executable. A candidate is used when `meta.json` reads,
   `ico_archive_info_acceptable` passes (format version 1, extractor
   `ico-pc extract 1`, disc id `SCES-50760`, rule `iso-sha1` with the
   image SHA-1 `1017b53f...` or rule `elf-sha1+datadf-crc`, boot ELF SHA-1
   `da3644c5...`), it mounts, and its `SCES_507.60` read back through the
   VFS hashes to `da3644c5...` (5.5 MB, a few milliseconds). An unusable one
   is logged with the reason and extracted again.
2. Otherwise find the image (`Ico_PAL.iso` beside the exe, `iso=` /
   `[paths] iso`, `$ICO_ISO`, `baserom/Ico_PAL.iso` under the working
   folder, else a file dialog: the native one on Windows, SDL3's on the
   Linux window build) and extract it (`ico_extract_archive`) into the
   per-user folder. A path from the dialog is saved as `iso=` once
   extraction succeeds.
3. Mount the result as in step 1 and boot.

**Extraction** (`port/data/extract.c`): mounts the image with the ISO9660
backend, reads `SYSTEM.CNF`'s BOOT2, walks the root and `DFDATAS`
directories, then

1. hashes the whole image (SHA-1, `host_config.c`'s);
2. streams each selected file into `<pref>/ico.o2r.tmp` through
   `mz_zip_writer_add_read_buf_callback` with `MZ_NO_COMPRESSION`, hashing
   `SCES_507.60` and taking the CRC-32 of DATA.DF's directory and of each
   of its members on the way, and checks every file arrived whole;
3. accepts the image by the first rule that holds and logs which:
   - `iso-sha1`: the image's SHA-1 is `1017b53f6e80f41f823369b0be1d8c69f7e16dc6`;
   - `elf-sha1+datadf-crc`: `SCES_507.60` hashes to
     `da3644c54c26fe760f3b6a591a5fc2eab396ed2b` and DATA.DF matches the
     manifest compiled into `extract.c`: size 867,184,640, 193 members,
     the CRC-32 of the directory (4 + 40 x 193 bytes, which covers the
     member names) and each member's offset, size and CRC-32. A re-dump of
     the PAL disc with other padding, another volume size or other file
     placement passes this rule. The manifest is sizes, offsets and CRCs
     only, like `config/tables_manifest.txt` (docs/LEGAL.md);
     `archive_test manifest <iso>` regenerates the block;
   - neither: refused with both hashes and the DATA.DF mismatch, and the
     `.tmp` removed;
4. writes `meta.json` last, finishes the ZIP, re-opens the `.tmp` with the
   backend's checks and moves it over `ico.o2r` atomically (`rename`;
   Windows `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING |
   MOVEFILE_WRITE_THROUGH`). Any failure (a read error, disk full, a refused
   image, a cancel) removes the `.tmp`; nothing half-written is ever named
   `ico.o2r`. `main_host.c` reports a failure with `ico_host_fatal`: the log
   line and, on Windows, the message box naming the log; no console.

**The port's files.** Data the port derives from the disc is kept as a file
beside the archive in the per-user folder, so it is made once; the archive
itself is never written after the extraction. The only one is the game
face, `gamefont-<V>-<SHA-1>.bin` (about 1.6 MB; docs/port/UI.md, "The
font"): `<V>` is `UI_GF_VERSION` and `<SHA-1>` the disc image's, as
`meta.json` records it (`iso_sha1`). It needs the game's tables
(texProperty names each word rectangle's sheet), so it is made after them:
on every start the window build's `main_host.c` calls
`ui_GameFontPrepare(file)` once the tables are loaded, which reads the file
when it loads and otherwise builds the face from the mounted disc (under a
second) and writes it as the archive is written: `<file>.tmp`, flushed to
the disk, moved over `<file>` (`ico_rename_replace`), the `.tmp` removed on
any failure. So the first start writes it right after the extraction; a
build with a new format version, or another disc, reads its own name and
makes its own file; a corrupt or truncated file does not load and is
replaced; an interrupted write leaves the old file or none, never the
archive damaged. A folder that cannot be written is logged and the face is
built again at the next start. Older files (another version or disc) are
left in the folder. With `use_iso` the name takes the verified image's
SHA-1; with the check skipped the disc is unidentified, the face is built in
memory at each start and nothing is written. The headless build draws no
text and makes none. An archive from an earlier build may hold the item
`port/gamefont-1.bin`, which that build added in place; nothing reads it
now, and `meta.json` never listed it, so the archive mounts as before.

Progress goes to the log in tenths ("first run: 40% (680 of 1698 MB)",
the image hash and the copy counted together). The window build also opens
a small SDL window (SDL's 2D renderer: a bar, the percentage in the title)
for the extraction and closes it before the game's window opens; closing it
cancels the extraction and exits 0. Paths: UTF-8 throughout (see "Paths"
below).

### Paths

Every path the port builds or reads is UTF-8: the per-user folder
(`SDL_GetPrefPath`), the executable's folder (`ico_host_exe_dir`,
`GetModuleFileNameW` on Windows), the file dialog's answer
(`GetOpenFileNameW`) and the ini's values. Windows' narrow ("ANSI") file
calls cannot open a path with a character outside the system code page (a
Cyrillic or CJK user name on a Western Windows), so the port has two
measures, either sufficient on its own:

- `port/platform/host_fs.c` (`host_fs.h`, library `ico_host_fs`): `ico_fopen`,
  `ico_mkdir`, `ico_rmdir`, `ico_remove`, `ico_rename_replace`
  (`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`)
  and `ico_path_kind` (`_wstat64`) convert to UTF-16 and call the wide
  functions; a path that is not valid UTF-8 falls back to the narrow call.
  `host_config.c`, `archive.c`, `iso9660.c`, `mc_host.c` (with `_wopendir`
  for the card's listing), `trace_host.c` and `diag_host.c` (the crash log's
  `CreateFileW`) go through them; the log redirect uses `_wfreopen`, the
  error box `MessageBoxW`. Elsewhere they are the POSIX calls.
- `ico_pc.exe` embeds an application manifest
  (`port/platform/win/ico_pc.manifest`, compiled by the toolchain's
  `windres` from `ico_pc.rc`) with `activeCodePage` UTF-8, so on Windows 10
  1903 and later the remaining narrow calls (developer files such as the
  pad script, the WAV dump, the host0 device) also read UTF-8. Check:
  `llvm-readobj --coff-resources ico_pc.exe` lists one `MANIFEST (ID 24)`
  resource, id 1.

`host_fs_test` (ctest `host_fs`) makes a folder and files under a UTF-8 name
with Latin, Greek and Japanese characters in the build tree, moves one over
another and removes them.

Extraction takes about 15 s on Linux with the image on a local disk (the
image hash and the copy about half each).

**`use_iso`** (`[dev] use_iso` in config.toml, `use_iso=` in ico-pc.ini,
CONFIG.md): `true` mounts the image directly with the ISO9660 backend after
the SHA-1 check (`verify=0` skips that check; it has no effect on
extraction, which always verifies). Default: `true` in the headless build,
so trace runs and tests read the image without an extraction step; `false`
in the window build. Both backends serve the same sectors, so a headless
run writes the same trace either way.

## libcdvd on the host (`port/data/cdvd_host.c`)

The subset the game links (found with `nm -u` over `ico_game`):
`sceCdInit`, `sceCdMmode`, `sceFsReset`, `sceCdDiskReady`, `sceCdStatus`,
`sceCdGetDiskType`, `sceCdGetError`, `sceCdBreak`, `sceCdSearchFile`,
`sceCdRead`, `sceCdReadIOPm`, `sceCdSync`, `sceCdReadClock`,
`sceCdStInit`, `sceCdStStart`, `sceCdStRead`, `sceCdStStop`, plus
`sceCdStStat` (the PS2 movie player's), `sceCdStSeek`,
`sceCdStPause`, `sceCdStResume`, `sceCdStream` and `sceCdSyncS` for
completeness.

| call | host answer |
|---|---|
| `sceCdInit` | mounts the default image if no disc is set (`ICO_ISO`, else `baserom/Ico_PAL.iso`), registers the vsync completion once; 1 |
| `sceCdDiskReady` | 2 (complete) with a disc, 6 (not ready) without |
| `sceCdStatus` | 0x0A (pause, idle) with a disc, 0 without |
| `sceCdGetDiskType` | 0x14 (PS2 DVD, `cdvd.c`'s `cdDiskType`) when SYSTEM.CNF's BOOT2 names a file on the disc, else 0 (no disc) |
| `sceCdSearchFile` | VFS look-up; writes libcdvd's 0x24-byte record (lsn, size, name[16], date[8], flag), never more |
| `sceCdRead`, `sceCdReadIOPm` | transfer now, completion at the next vsync (below); 0 while a command is in flight |
| `sceCdSync` | even mode (blocking): from a game thread, waits (WaitSema) for the vsync that ends the command, then 0; from the host context, ends it at once. Odd mode (poll): 1 until the next vsync |
| `sceCdGetError` | 0, or 0x01 aborted, 0x12 no disc, 0x20 bad address, 0x30 read error, 0x32 past the end (the codes `FileManager.c` names) |
| `sceCdReadClock` | the port clock (`port/platform/clock.c`: local time, or a fixed time in the headless build, when tracing, or when `fixed_clock` says so; CONFIG.md) in BCD, stat 0; a test may install its own source with `ico_cdvd_host_set_clock_source` |
| stream calls | read straight from the disc at the stream cursor; `sceCdStStat` reports the ring as full |

### Timing

On the PS2 a read starts and `sceCdSync` waits for it. The host copies the
sectors when `sceCdRead` returns (no caller can look before it syncs) and
models completion so the game's control flow is the drive's. Every read
command finishes at the next simulated vsync, blocking or polled; drive seek
and transfer times are not modelled.

- a poll (`sceCdSync(1)`, the background reader in `iosCdvdBackGroundRead`
  and `...ReadIOPm`) sees the command busy until the next simulated vsync,
  so the reader's `cdWait` sleeps once, as it did while the drive worked.
  The completion is `ico_cdvd_host_vsync`, registered with
  `ico_host_on_vsync_register` (`port/platform/host_loop.h`).
  The host loop runs it after the vblank interrupt's handlers and before
  the woken threads run, so a cdvd thread woken by the vblank handler finds
  its read complete;
- a blocking wait (`sceCdSync(0)`: the stream manager, `file_LoadCDFile`)
  blocks the calling thread on a semaphore until the same vsync, as
  libcdvd's does (`sce/libcdvd/cdvd000.c` `sceCdSync`: it loops on
  `sceCdDelayThread`, which is `CreateSema`, `SetAlarm`, `WaitSema`), so
  the other threads run meanwhile, the same-priority Main among them. This
  matters: a blocking caller does not only wait. If the read completed on
  the spot, a stage load would run from start to end without a Main tick,
  and the load thread would clip against a collision list Main built before
  StageManager removed every object (a null `dobj` in `fieldCollision.c`
  `_Clip`). On the PS2 a load spans many vsyncs and Main rebuilds the list
  in each. From the host context (the unit tests) the wait completes at
  once.

**Disc identification.** `cdvd.c` keeps its check
(`iosCdvdDiskReadyBlock`, `cdWait`): drive type 20 and a search for
`\SCES_507.60;1`. On the host the type comes from the image's SYSTEM.CNF
(BOOT2 must name a file present on the disc), and the search goes through
the VFS, so the check identifies the image as it identified the disc. The
check only runs after the drive reports not-ready, which the host never
does with an image mounted; the authoritative check is the extractor's
(Backend 2 above).

**No disc.** With no image, `sceCdDiskReady` says not ready and
`sceCdStatus` stopped, and `file_Init` waits at its disc wait as a console
with an empty drive did. `cdvd_host.c` prints the path it tried once.

## Boot (`seki/src/FileManager.c`)

`file_Init` on the host keeps `sceSifInitRpc`, `sceCdInit`,
`sceCdMmode` and one disc wait, and leaves out the `IOPRP224.IMG` reboot and
the six `sceSifLoadModule` calls (SIO2MAN, PADMAN, MCMAN, MCSERV, LIBSD,
SNDN2DRV). `file_LoadCDFile`, the boot-time loader, is unchanged and reads
through `sceCdSearchFile` and `sceCdRead`. `file_Init` makes no SYSTEM.CNF
read of its own. The SIF calls also report success when called
(`port/data/sif_host.c`): reboot and sync 1, module loads a positive id.

## SIF and IOP RAM

`port/data/iop_ram.h`: `ico_iop_ram`, 2 MB, 64-byte aligned. An IOP address
(what `sceSifAllocIopHeap` returns and the game passes to `sceCdReadIOPm`,
`SgStAdpcmOpen`, `sceSifSetDma`) is an offset into it after stripping the
kseg bits (`ico_iop_phys`); `ico_iop_ptr` and `ico_iop_addr` convert both
ways. The IOP heap is first-fit with 16-byte granularity from 0x10000 to
the end of RAM (about 1.94 MB). That is more than the PS2 left free after
its modules, which is not modelled; the game's allocations at boot (the
sound area 0x78000, the ADPCM rings 0xB8800) and the movie stream buffer
(576 sectors + 16) are the sizes the tests exercise.

`sceSifSetDma` copies EE memory into IOP RAM at once; `sceSifDmaStat`
reports every transfer finished. RPC: `ico_sif_register_server(sid, fn)`
installs a host function as the IOP server for a service id;
`sceSifBindRpc` answers the bind (nonzero `serve`) only for a registered
id, `sceSifCallRpc` runs the server synchronously and copies its reply, and
`sceSifCheckStatRpc` reports every call finished. The sifdev calls
(`sceOpen`, `sceRead`, ...) served the dev kit's `host0:` and fail.

The other IOP-side devices have their own host layers: the pad
(`port/input/pad_host.c`, INPUT.md), the memory card
(`port/save/mc_host.c`, SAVES.md), sound (`port/audio/`, AUDIO.md) and
libscf (`port/config/sysconf.c`, CONFIG.md). What is still a stub is in
HEADLESS_STUBS.md.

## Facts about the PAL disc relied on

Read from the user's image (`baserom/Ico_PAL.iso`, SCES-50760) with the
reader above; sector numbers and sizes only.

| fact | value |
|---|---|
| image size | 907,706,368 bytes |
| image SHA-1 (extractor's verification value) | `1017b53f6e80f41f823369b0be1d8c69f7e16dc6` |
| volume descriptors | primary at LSN 16, terminator at LSN 17; no supplementary (no Joliet) |
| logical block size | 2048 |
| volume space | 443,216 sectors |
| root directory | LSN 261, recorded length 1,168 bytes (not a sector multiple) |
| `DFDATAS` directory | LSN 262, recorded length 152; one file, `DATA.DF;1` |
| `SYSTEM.CNF;1` | LSN 287, 56 bytes; BOOT2 names `SCES_507.60` |
| `SCES_507.60;1` | LSN 762, 5,515,680 bytes; read through the VFS it hashes to `da3644c5...` (`config/sha1sums.txt`, `baseelf.elf`) |
| `DATA.DF;1` | LSN 19,771, 867,184,640 bytes, one extent |
| DATA.DF directory | 193 entries, every one sector-aligned and inside DATA.DF. With DATA.DF's own entry that is 194 of the 200 slots in `iosCdvdSrhBuff`, which `unifile_read_func` fills without a bound |
| `IOPRP224.IMG`, `SIO2MAN.IRX`, `PADMAN.IRX`, `MCMAN.IRX`, `MCSERV.IRX`, `LIBSD.IRX`, `SNDN2DRV.IRX` | present in the root (not read on the host, except `SNDN2DRV.IRX` for its pitch table and `LIBSD.IRX` for its reverb presets and idle voice block) |

## The data tables (`port/data/tables.c`)

The game's 73 data-only members (`config/data_schema.pal.txt`: 74 schema
rows over 75 rows of `config/data_members.pal.txt`) are ELF data: actor
modes, motion and sound definitions, object layouts, stage lists, the staff
roll. The PS2 build compiles them as C written from the user's ELF
(`tools/gen_data_c.py`). The host binary holds none of their bytes: it
defines each table as an uninitialised array, and before the game starts
the loader fills them from the boot ELF on the user's disc.

**Generated, committed (`port/data/gen/`, by `tools/gen_data_desc.py`).**
Types, offsets, sizes, names and CRCs only:

| file | holds |
| --- | --- |
| `table_defs.c` | `T name[count];` for every table (in `.bss`), the string-pool buffers, and `staffRollNameDataNum` (derived as `sizeof` less 2, as the PS2 build derives it) |
| `table_desc.c` | per record type, a field list `{path, host_offset, host_size, ee_offset, ee_size, count, kind, signed, bit, width, setter}`; per row, its member, symbol, section, EE range, CRC-32, first record offset, count, record type and host array |
| `ee_symbols.c` | the registry: the 914 distinct EE addresses the tables' pointer words hold (913 functions, 1 object, `scpDummyGObj`), sorted, each with the host symbol |
| `table_desc.h` | the descriptor types and `ICO_TABLE_SYMBOLS(X)` |
| `tables.cmake` | the table names, for the test's renamed reference |

The field lists come from the same parse of the record headers
`gen_data_c.py` does (`gen_data_c.Header`, EE layout rules). Each record is
flattened: scalars and arrays of scalars are one field each (`count` for
the array), arrays of records are expanded by index (`ent[2].act`), a
pointer-free union is copied as its bytes, a bit-field is one field with its
bit position and a generated setter (`((SeDef *)r)->procRan = v`), so its
host placement is the compiler's. `host_offset` and `host_size` are
`offsetof`/`sizeof` expressions, evaluated by the host compiler. The EE
offsets and sizes are asserted in `table_desc.c`: for pointer-free records
on every host, for records with pointers only where pointers are 4 bytes
(`UINTPTR_MAX`, an EE-layout check that no current preset compiles), and
every scalar's host size equals its EE size everywhere.

Coverage: 73 members, 68 record types, 75 rows (72 table rows, the
`staffroll_dat` string pool in `.rodata` and the head of its `.sdata`, and
its `count-of` row), 29,701 records. Field kinds used: int (1/2/4 bytes,
signed or not, arrays), float, union bytes (`MotionDef`'s three flag
words: `modeBits`, `flags`, `flags2`), bit-fields (in 9 record types:
`ActModeRec`, `AttackKindEntry`, `EnemyDef`, `LtProperty`, `PObjMdl`, `SeBank`,
`SeDef`, `SeEnvDef`, `StgPre`), 913 function targets over 9
members' fields, 1 object target (`GenGeo.outGObj`), char pointers into the
member's own string pool (`staffRollNameData`). No 8-byte field exists; the
generator refuses one, a pointer inside a union, and a bit-field wider than
32 bits.

**The manifest (`config/tables_manifest.txt`).** Per row, the CRC-32 (zlib)
of the ELF's bytes in its range, and the registry's `func`/`obj` lines (the
address and the symbol the committed symbol lists name there). Written by
`tools/gen_data_desc.py --manifest`, the only mode that reads the ELF; the
CRCs and names are not disc data (`docs/LEGAL.md`).

**Loading.** `ico_pc` mounts the disc, then calls `ico_tables_load_vfs`
(port/platform/main_host.c) before the window, the pad, the trace and the
game's `main`: nothing has read a table yet. The loader reads `SCES_507.60`
through the VFS (either backend), finds
`.data`, `.rodata` and `.sdata` in its section headers, and:

1. checks every row's range against its section and its CRC-32 against the
   manifest, for all rows before writing any, so a wrong or modified ELF
   leaves the tables empty and `ico_pc` stops with the row, both CRCs and
   the reason (`ico_host_fatal`);
2. copies the string-pool rows into their buffers;
3. decodes each record field by field into the host array: integers,
   floats and unions copied (the EE and the hosts are little-endian; the
   loader refuses to compile on a big-endian host), bit-fields extracted from
   their EE unit and stored through the setter, function-pointer words
   looked up in the registry (binary search), object-pointer words resolved
   first into the member's own string pool (staffroll: a pointer to pool
   offset k becomes the host pool copy + k), then in the registry. A nonzero
   word the registry does not hold stops the load with the table, element,
   field, the address and the word's own address.

The 64-bit record layouts (8-byte function pointers in `SeDef`, `GenGeo`,
`ObjKindEnt` and the rest) are what the game's code is compiled against;
the loader writes exactly them. `StageAnimDef.data` and
`LtProperty.texData`, null in the ROM, are real host pointer slots, filled
at run time as before.

**const.** Every table is defined non-const: the loader writes them all,
and the game writes three of the PS2 build's const `.rodata` tables
(`stageTable` in `StageAnimation.c`, `motionLimitDef`
in `motionOrientManager.c`, `seDef` in `s_init.c`), which
worked on the EE (no page protection). Headers that declare
a table `extern const` (40 tables) are not edited: `table_defs.c` and
`table_desc.c` `#define` each such name to `<name>_header_decl` around their
`#include`s, so the header's declaration names an unused symbol and the
non-const definition does not conflict. Other translation units still see
`extern const`, which only stops them writing through that declaration; the
object itself is writable.

**`nodeLimit`.** On the EE `SetNodeRotationLimitDataTable` stores a pointer
to a `motionLimitDef` row in each 4-byte slot of a display object's
`nodeLimit` array (allocated `skelNodeNum << 2` in `common/src/DObj.c`). The
table lives in host `.bss`, outside the arena, so an EE word cannot name it;
on the host the slot holds the row's one-based index, and
`_getFinalMatrix` reads `motionLimitDef[word - 1]`. The allocation stays 4
bytes a slot.

**Tests.**

- `data_desc_fresh`: `tools/gen_data_desc.py --check` regenerates
  `port/data/gen/` in memory and compares; that mode never opens the ELF, so
  the files are a function of the committed text (schema, headers, symbol
  lists, manifest) and carry no disc bytes.
- `tables_manifest` (when the base ELF is present): the manifest equals a
  recomputation from `baserom/pal/baseelf.elf`.
- `tables_loader` (`port/data/test/tables_test.c`, built when the base ELF
  and pyelftools are present): `gen_data_c.py --symbol-map` writes the 73
  members into the build directory, compiled under renamed symbols
  (`ico_ref_<name>`); the registry is compiled with stub definitions of its
  914 symbols, so both sides resolve pointers to the same addresses. The
  loader reads `SCES_507.60` through the VFS from the disc image (or the
  ELF file without one) and every byte of every host table must equal the
  compiled table's (the staff roll's char pointers compare by string). Then
  a copy of the ELF with one byte of `seDef` flipped must be refused naming
  `sedef` and its CRC, with the tables unchanged, and a non-ELF refused.

**Regenerating.** After changing the schema, a record header, the symbol
lists or `config/data_members.pal.txt`: `tools/gen_data_desc.py --manifest`
(needs the ELF, if a range or pointer target changed) then
`tools/gen_data_desc.py`. The output is clang-formatted by the generator.

## Tests

`port/data/test/vfs_test.c` (ctest `vfs_synthetic`, `vfs_disc`),
`port/data/test/archive_test.c` (`archive_synthetic`, `archive_disc`) and
`port/null/test/null_devices_test.c` (`null_devices`):

- `vfs_synthetic` builds a 40-sector ISO9660 image in the build directory
  (a SYSTEM.CNF, a boot file, a DATA.DF with a three-entry directory, a
  directory spanning two sectors with an unaligned length) and runs the VFS,
  the libcdvd layer (search records, poll vs blocking completion through
  the registered vsync callback, IOP-memory reads, the stream, errors, the
  clock) and the SIF layer (IOP heap, DMA, boot calls) against it. Needs no
  disc.
- `vfs_disc` reads the user's image: finds SYSTEM.CNF, SCES_507.60, DATA.DF
  and the modules, hashes the ELF through the VFS against `config/
  sha1sums.txt`, walks DATA.DF's directory as `unifile_read_func` does, and
  checks the libcdvd layer identifies the disc. It exits 77 (skipped) when
  the image is absent. `ICO_DISC_IMAGE` (CMake cache) names another path.
- `archive_synthetic` (`port/data/test/archive_test.c`, which includes
  `vfs_test.c` for its synthetic image builder) extracts that image with a
  nonzero tail byte planted after the boot file: refused without
  `ICO_EXTRACT_NO_VERIFY` and nothing left behind; a cancel from the
  progress callback leaves nothing; then extracted unverified and compared
  with the ISO backend: look-ups (`IcoVfsEntry` byte for byte), every
  sector of every stored file, every run inside DATA.DF, runs across files,
  the tail entry, failures outside stored files and past the end, the
  DATA.DF walk, `sceCdSearchFile` records, disc identification; a truncated
  file and a non-ZIP are refused; an unverified archive is not acceptable;
  a second extraction replaces the archive. Needs no disc.
- `archive_disc` extracts the user's image into `$TMPDIR` (else the build
  folder), expects rule `iso-sha1` with the ELF hash and the DATA.DF
  manifest also matching (what the second rule checks), mounts it and
  checks: `SCES_507.60` read back hashes to `da3644c5...`, `volume_sectors`
  equal, look-ups and every sector of the small files equal, all of DATA.DF
  equal through both backends chunk by chunk (and its CRC-32), every sixth
  directory member (and the last) equal sector for sector by the LSN
  `cdvd.c` computes, 4,000 pseudo-random sector runs inside DATA.DF and one
  over its end into `DUMMY.TXT` equal, `sceCdSearchFile` records equal. 77
  without the image; about 25 s; the archive is deleted afterwards.
- `null_devices` checks the pad as an empty console answers it (no feed:
  state 0, `scePadRead` fails); the sound, card and libscf checks live in
  `sndn2_test`, `mc_test` and `config_test`.

## Divergences

Disc timing (every read finishes at the next vsync) is described under
"Timing" above. The disc layer has no other known difference from the PS2.
